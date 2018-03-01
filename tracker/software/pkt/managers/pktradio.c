/*
    Aerospace Decoder - Copyright (C) 2018 Bob Anderson (VK2GJ)

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*/

/**
 * @file        pktradio.c
 * @brief       Radio manager.
 *
 * @addtogroup  managers
 * @{
 */

#include "pktconf.h"

/*
 * The radio manager thread.
 */
THD_FUNCTION(pktRadioManager, arg) {
  dyn_objects_fifo_t *the_radio_fifo = arg;

  chDbgCheck(arg != NULL);

  bool rx_active = false;

  objects_fifo_t *radio_queue = chFactoryGetObjectsFIFO(the_radio_fifo);

  chDbgAssert(radio_queue != NULL, "no queue in radio manager FIFO");

  while(!chThdShouldTerminateX()) {
    /* Check for task requests. */
    radio_task_object_t *task_object;
    msg_t fifo_msg = chFifoReceiveObjectTimeout(radio_queue,
                         (void *)&task_object,
                         TIME_MS2I(100));
    if(fifo_msg == MSG_TIMEOUT)
      continue;

    /* Something to do. object pointer is in fifo_msg. */

    packet_svc_t *handler = task_object->handler;

    /* TODO: Use radio ID to determine handling... later. */
    switch(task_object->command) {
    case PKT_RADIO_OPEN: {
      /* Create the packet management services. */
      pktBufferManagerCreate(handler);
      pktCallbackManagerCreate(handler);
      switch(task_object->type) {
        case DECODE_AFSK: {
          /*
           * Create the AFSK decoder (includes PWM, filters, etc.).
           */
          AFSKDemodDriver *driver = pktCreateAFSKDecoder(handler);
          handler->link_controller = driver;
          chDbgCheck(driver != NULL);
          /* TODO: Implement thread events or callback for results. */
          if(driver == NULL) {
            break;
          }
          break;
        } /* End case. */

        case DECODE_NOT_SET:
        case DECODE_FSK: {
          break;
        }
      } /* End switch. */
      Si446x_conditional_init();
      break;
      }

    /* TODO: Switch on encoding. Tune radio to channel. */
    case PKT_RADIO_RX: {
      pktStartDecoder(handler);
      radio_squelch_t sq = task_object->squelch;
      radio_freq_t freq = task_object->base_frequency;
      Si446x_receive_noLock(freq, sq, MOD_AFSK);
      rx_active = true;
      break;
      }

    case PKT_RADIO_RX_STOP: {
      pktStopDecoder(handler);
      rx_active = false;
      break;
    }

    case PKT_RADIO_TX: {
      /* TODO: Switch on encoding. */
      if(rx_active)
        pktPauseDecoder(handler);

      /* Transmit here... */

      if(rx_active)
        pktResumeDecoder(handler);
      break;
    }

    case PKT_RADIO_CLOSE: {
      event_listener_t el;
      event_source_t *esp;
      thread_t *decoder = NULL;
      switch(task_object->type) {
      case DECODE_AFSK: {
        esp = pktGetEventSource((AFSKDemodDriver *)handler->link_controller);

        pktRegisterEventListener(esp, &el, USR_COMMAND_ACK, DEC_CLOSE_EXEC);

        decoder = ((AFSKDemodDriver *)(handler->link_controller))->decoder_thd;

        /* Send event to release AFSK resources and terminate thread. */
        chEvtSignal(decoder, DEC_COMMAND_CLOSE);

        /* Then release common services and thread heap. */
        break;
        }

      case DECODE_NOT_SET:
      case DECODE_FSK: {
        break;
        } /* End case DECODE_FSK. */
      } /* End switch on link_type. */
      if(decoder == NULL)
        /* No decoder processed. */
        break;

      /* Wait for the decoder to stop. */
      eventflags_t evt;
      do {
        chEvtWaitAny(USR_COMMAND_ACK);

        /* Wait for correct event at source.
         */
        evt = chEvtGetAndClearFlags(&el);
      } while (evt != DEC_CLOSE_EXEC);

      pktUnregisterEventListener(esp, &el);

      /*
       *  Release decoder thread heap when it terminates.
       */
      chThdWait(decoder);

      /* Release packet services. */
      pktBufferManagerRelease(handler);
      pktCallbackManagerRelease(handler);
      chBSemSignal(&handler->close_sem);
      break;
      } /*end case close. */
    } /* End switch on command. */

    if(task_object->callback != NULL)
      /* Perform the callback. */
      task_object->callback(handler);
    /* Return task object to free list. */
    chFifoReturnObject(radio_queue, (radio_task_object_t *)task_object);
  }
  chThdExit(MSG_OK);
}


void pktRadioManagerCreate(packet_svc_t *handler) {

  /* The radio associated with this packet handler. */
  radio_unit_t rid = handler->radio_config.radio_id;

  /* Create the radio manager name. */
  chsnprintf(handler->rtask_name, sizeof(handler->rtask_name),
             "%s%02i", PKT_RADIO_TASK_QUEUE_PREFIX, rid);

  dyn_objects_fifo_t *the_radio_fifo =
      chFactoryCreateObjectsFIFO(handler->rtask_name,
      sizeof(radio_task_object_t),
      RADIO_TASK_QUEUE_MAX, sizeof(msg_t));

  chDbgAssert(the_radio_fifo != NULL, "unable to create radio task queue");

  handler->the_radio_fifo = the_radio_fifo;

  dbgPrintf(DBG_INFO, "PKT  > radio manager thread created. FIFO @ 0x%x\r\n",
            the_radio_fifo);

  /* Start the task dispatcher thread. */
  handler->radio_manager = chThdCreateFromHeap(NULL,
              THD_WORKING_AREA_SIZE(PKT_RADIO_MANAGER_WA_SIZE),
              handler->rtask_name,
              NORMALPRIO - 10,
              pktRadioManager,
              the_radio_fifo);

  chDbgAssert(handler->radio_manager != NULL,
              "unable to create radio task thread");
}

/**
 * TODO: This needs review. Is it robust enough?
 */
void pktRadioManagerRelease(packet_svc_t *handler) {
  chThdTerminate(handler->radio_manager);
  chThdWait(handler->radio_manager);
  chFactoryReleaseObjectsFIFO(handler->the_radio_fifo);
}

/**
 * @brief   Get a radio command task object.
 * @post    A task object is returned ready for filling and submission.
 *
 * @param[in]   handler pointer to packet handler object.
 * @param[in]   timeout maximum time to wait for a task to be submitted.
 * @param[in]   rt      pointer to a task object pointer.
 *
 * @return  Status of the operation.
 * @retval  MSG_TIMEOUT an object could not be obtained within the timeout.
 * @retval  MSG_OK      an object has been fetched.
 *
 * @api
 */
msg_t pktGetRadioTaskObject(packet_svc_t *handler,
                            sysinterval_t timeout,
                            radio_task_object_t **rt) {
  dyn_objects_fifo_t *task_fifo =
      chFactoryFindObjectsFIFO(handler->rtask_name);
  chDbgAssert(task_fifo != NULL, "unable to find radio task fifo");

  objects_fifo_t *task_queue = chFactoryGetObjectsFIFO(task_fifo);
  chDbgAssert(task_queue != NULL, "no objects fifo list");

  *rt = chFifoTakeObjectTimeout(task_queue, TIME_MS2I(timeout));

  if(*rt == NULL) {
    /* Timeout waiting for object. */
    /* Release find reference to the FIFO (decrease reference count). */
    chFactoryReleaseObjectsFIFO(task_fifo);
    return MSG_TIMEOUT;
  }
  (*rt)->handler = handler;
  return MSG_OK;
}

/**
 * @brief   Submit a radio command to the task manager.
 * @post    A task object is created and submitted to the radio manager.
 *
 * @param[in]   handler pointer to packet handler object.
 * @param[in]   object  radio task object to be submitted.
 * @param[in]   cb      function to call with result (can be NULL).
 *
 * @api
 */
void pktSubmitRadioTask(packet_svc_t *handler,
                         radio_task_object_t *object,
                         radio_task_cb_t cb) {

  dyn_objects_fifo_t *task_fifo =
      chFactoryFindObjectsFIFO(handler->rtask_name);
  chDbgAssert(task_fifo != NULL, "unable to find radio task fifo");

  objects_fifo_t *task_queue = chFactoryGetObjectsFIFO(task_fifo);
  chDbgAssert(task_queue != NULL, "no objects fifo list");

  /* Populate the object with information from request. */

  /* TODO: Put command information into queue object.
   * Have to do this so that commands are not overwritten in handler.
   */
  object->handler = handler;
  object->callback = cb;
  // etc.
  /*
   * Submit the task to the queue.
   * The task thread will process the request.
   * The task object is returned to the free list.
   * If a callback is specified it is called after the task object is freed.
   */
  chFifoSendObject(task_queue, object);

  /* Release reference to the FIFO acquired earlier by find. */
  chFactoryReleaseObjectsFIFO(task_fifo);
}

/** @} */
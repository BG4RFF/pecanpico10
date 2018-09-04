/*
    Aerospace Decoder - Copyright (C) 2018 Bob Anderson (VK2GJ)

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*/

/**
 * @file    pktradio.h
 * @brief   Generic radio definitions.
 *
 * @addtogroup managers
 * @{
 */

#ifndef PKT_MANAGERS_PKTRADIO_H_
#define PKT_MANAGERS_PKTRADIO_H_

/*===========================================================================*/
/* Module constants.                                                         */
/*===========================================================================*/

/* Thread working area size. */
#define PKT_RADIO_MANAGER_WA_SIZE       (1 * 1024)

#define PKT_RADIO_TASK_QUEUE_PREFIX     "radm_"

/* The number of radio task object the FIFO has. */
#define RADIO_TASK_QUEUE_MAX            10

#define PKT_RADIO_MANAGER_TASK_KILL     TRUE

/* Set TRUE to use mutex instead of bsem. */
#define PKT_USE_RADIO_MUTEX             TRUE

/*===========================================================================*/
/* Module data structures and types.                                         */
/*===========================================================================*/

typedef enum radioIndicators {
  PKT_INDICATOR_DECODE,
  PKT_INDICATOR_SQUELCH,
  PKT_INDICATOR_FIFO,
  PKT_INDICATOR_OVERFLOW,
  PKT_INDICATOR_ERROR
} radio_indicator_t;

#include "pkttypes.h"

/**
 * @brief   Radio manager control commands.
 * @details Radio task requests execute these commands.
 */
typedef enum radioCommand {
  PKT_RADIO_RX_OPEN,
  PKT_RADIO_RX_START,
  PKT_RADIO_RX_STOP,
  PKT_RADIO_TX_SEND,
  PKT_RADIO_RX_CLOSE,
  PKT_RADIO_TX_DONE,
  PKT_RADIO_MGR_CLOSE,
  PKT_RADIO_RX_RSSI
} radio_command_t;

/**
 * Forward declare structure types.
 */
typedef struct radioTask radio_task_object_t;
typedef struct packetHandlerData packet_svc_t;
typedef struct radioConfig radio_config_t;
//typedef struct radioSettings radio_settings_t;
//typedef struct radioAction radio_action_t;

/**
 * @brief   Radio task notification callback type.
 *
 * @param[in] task_object  pointer to a @p radio task object
 */
typedef void (*radio_task_cb_t)(radio_task_object_t *task_object);

#include "ax25_pad.h"

typedef struct radioSettings {
  radio_mod_t               type;
  radio_freq_t              base_frequency;
  channel_hz_t              step_hz;
  radio_ch_t                channel;
  radio_squelch_t           squelch;
} radio_settings_t;

typedef struct radioAction {
  radio_command_t           command;
  radio_task_cb_t           callback;
  msg_t                     result;
  thread_t                  *thread;
  char                      tx_thd_name[16];
  packet_svc_t              *handler;
  packet_t                  packet_out;
} radio_action_t;

/*struct radioTaskx {
  radio_settings_t          settings;
  radio_action_t            action;
};*/

/**
 * @brief       Radio task object.
 * @details     queue object submitted via FIFO or radio task requests.
 */
struct radioTask {
  /* For safety keep clear - where pool stores its free link. */
  struct pool_header        link;
  radio_command_t           command;
  radio_mod_t               type;
  radio_freq_t              base_frequency;
  channel_hz_t              step_hz;
  radio_ch_t                channel;
  radio_squelch_t           squelch;
  radio_task_cb_t           callback;
  msg_t                     result;
  thread_t                  *thread;
  packet_svc_t              *handler;
  packet_t                  packet_out;
  radio_pwr_t               tx_power;
  uint32_t                  tx_speed;
  uint8_t                   tx_seq_num;
};

/*===========================================================================*/
/* Module macros.                                                            */
/*===========================================================================*/

/**
 * @brief   Alias of pktStopDecoder for convenience.
 *
 * @param[in] radio radio unit ID
 *
 * @api
 */
#define pktPauseDecoding(radio) pktStopDecoder(radio)

/**
 * @brief   Alias for convenience of pktStartDecoder.
 *
 * @param[in] radio radio unit ID
 *
 * @api
 */
#define pktResumeDecoding(radio) pktStartDecoder(radio)

/*===========================================================================*/
/* External declarations.                                                    */
/*===========================================================================*/

#ifdef __cplusplus
extern "C" {
#endif
  thread_t  		*pktRadioManagerCreate(const radio_unit_t radio);
  void      		pktRadioManagerRelease(const radio_unit_t radio);
  bool pktStartRadioReceive(const radio_unit_t radio,
                            radio_task_object_t *rto);
  bool pktStopRadioReceive(const radio_unit_t radio,
                           radio_task_object_t *rto);
  void      		pktRadioManager(void *arg);
  msg_t     		pktGetRadioTaskObject(const radio_unit_t radio,
                              const sysinterval_t timeout,
                              radio_task_object_t **rt);
  void      		pktSubmitRadioTask(const radio_unit_t radio,
                          radio_task_object_t *object,
                          radio_task_cb_t cb);
  void      		pktScheduleThreadRelease(const radio_unit_t radio,
                                thread_t *thread);
  msg_t     		pktLLDlockRadioTransmit(const radio_unit_t radio,
            		                const sysinterval_t timeout);
  void      		pktLLDunlockRadioTransmit(const radio_unit_t radio);
  const radio_config_t *pktGetRadioList(void);
  uint8_t           pktGetNumRadios(void);
  radio_band_t 		*pktCheckAllowedFrequency(const radio_unit_t radio,
                                        const radio_freq_t freq);
  radio_freq_t 		pktComputeOperatingFrequency(const radio_unit_t radio,
                                            radio_freq_t base_freq,
                                            channel_hz_t step,
                                            radio_ch_t chan,
                                            const radio_mode_t mode);
  radio_unit_t      pktSelectRadioForFrequency(const radio_freq_t freq,
                                          const channel_hz_t step,
                                          const radio_ch_t chan,
                                          const radio_mode_t mode);
  bool      		pktLLDradioStartReceive(const radio_unit_t radio,
                                radio_task_object_t *rto);
  void      		pktLLDradioStopReceive(const radio_unit_t radio);
  bool      		pktLLDradioResumeReceive(const radio_unit_t radio);
  bool      		pktLLDradioSendPacket(radio_task_object_t *rto);
  void      		pktLLDradioCaptureRSSI(const radio_unit_t radio);
  bool      		pktLLDradioInit(const radio_unit_t radio);
  void      		pktLLDradioStandby(const radio_unit_t radio);
  void      		pktLLDradioShutdown(const radio_unit_t radio);
  void      		pktLLDradioPauseDecoding(const radio_unit_t radio);
  void      		pktLLDradioResumeDecoding(const radio_unit_t radio);
  void      		pktLLDradioStartDecoder(const radio_unit_t radio);
  void      		pktLLDradioStopDecoder(const radio_unit_t radio);
  void      		pktRadioSendComplete(radio_task_object_t *rto,
                                thread_t *thread);
  ICUDriver         *pktLLDradioAttachStream(const radio_unit_t radio);
  void              pktLLDradioDetachStream(const radio_unit_t radio);
  const ICUConfig   *pktLLDradioStreamEnable(const radio_unit_t radio,
                                         palcallback_t cb);
  void              pktLLDradioStreamDisableI(const radio_unit_t radio);
  bool              pktLLDradioGetInProgress(const radio_unit_t radio);
  void      		pktStartDecoder(const radio_unit_t radio);
  void      		pktStopDecoder(const radio_unit_t radio);
  int       	 	pktDisplayFrequencyCode(radio_freq_t code, char *buf,
            	 	                        size_t size);
  const radio_config_t	*pktGetRadioData(radio_unit_t radio);
  uint8_t           pktLLDradioReadCCA(const radio_unit_t radio);
  void              pktLLDradioConfigIndicators(const radio_unit_t radio);
  void              pktLLDradioDeconfigIndicators(const radio_unit_t radio);
  void              pktLLDradioUpdateIndicator(const radio_unit_t radio,
                               radio_indicator_t ind);
#ifdef __cplusplus
}
#endif

/*===========================================================================*/
/* Module inline functions.                                                  */
/*===========================================================================*/

#endif /* PKT_MANAGERS_PKTRADIO_H_ */

/** @} */

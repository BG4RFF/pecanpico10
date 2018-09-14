/*
    Aerospace Decoder - Copyright (C) 2018 Bob Anderson (VK2GJ)

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*/


/**
 * @file    portab.c
 * @brief   Application portability module code.
 *
 * @addtogroup application_portability
 * @{
 */

#include "hal.h"
#include "chprintf.h"
#include "pkttypes.h"
#include "portab.h"
#include "console.h"
#include "types.h"
#include "si446x.h"
#include <stdarg.h>

/*===========================================================================*/
/* Module local definitions.                                                 */
/*===========================================================================*/

/*===========================================================================*/
/* Module exported variables.                                                */
/*===========================================================================*/

/* Definition of radio IO for radio on this board. */
const si446x_mcucfg_t radio1_cfg = {
		.gpio0 	= LINE_RADIO_GPIO0,
		.gpio1 	= LINE_RADIO_GPIO1,
		.gpio2 	= PAL_NOLINE,
		.gpio3 	= PAL_NOLINE,
		.nirq	= LINE_RADIO_NIRQ,
		.sdn	= LINE_RADIO_SDN,
		.cs		= LINE_RADIO_CS,
		.spi	= PKT_RADIO1_SPI,
        .icu    = PKT_RADIO1_ICU,
        .alt    = (PAL_MODE_INPUT | PAL_MODE_ALTERNATE(2)),
        .cfg    = {
                      ICU_INPUT_ACTIVE_HIGH,
                      ICU_COUNT_FREQUENCY,      /**< ICU clock frequency. */
                    #if LINE_PWM_MIRROR != PAL_NOLINE
                      pktRadioICUWidth,         /**< ICU width callback. */
                    #else
                      NULL,                     /**< ICU width callback. */
                    #endif
                      pktRadioICUPeriod,        /**< ICU period callback. */
                      pktRadioICUOverflow,      /**< ICU overflow callback. */
                      ICU_CHANNEL_1,            /**< Timer channel. */
                      0                         /**< DIER bits. */
                    }
};

/* Definition of radio driver specific data for radio on this board. */
si446x_data_t radio1_dat = {
        .lastTemp = 0x7FFF
        /* TODO: Move part and func structs into here
         * Add functions to set/get values
         */
};

/* List of bands in this radio. */
const radio_band_t *const radio_bands[] = {
                (radio_band_t *const)&band_2m,
                 NULL
};

/* Configuration objects for radios on this board. */
const radio_config_t radio_list[] = {
  { /* Radio #1 */
    .unit = PKT_RADIO_1,
    .type = SI446X,
    .pkt    = (packet_svc_t *const)&RPKTD1,
    .afsk   = (AFSKDemodDriver *const)&AFSKD1,
	.cfg	= (si446x_mcucfg_t *const)&radio1_cfg,
    .dat    = (si446x_data_t *)&radio1_dat,
    .bands  = (radio_band_t **const)radio_bands
  }, /* End radio1 */
  {
     .unit = PKT_RADIO_NONE
  }
};

/**
 * Debug serial port setting.
 */
const SerialConfig debug_config = {
  115200,
  0,
  0,
  0
};

/*===========================================================================*/
/* Module local types.                                                       */
/*===========================================================================*/

/*===========================================================================*/
/* Module local variables.                                                   */
/*===========================================================================*/

/*===========================================================================*/
/* Module local functions.                                                   */
/*===========================================================================*/

/*===========================================================================*/
/* Module exported functions.                                                */
/*===========================================================================*/

void pktConfigSerialDiag(void) {
#if ENABLE_EXTERNAL_I2C == FALSE
  /* USART3 TX.       */
  palSetLineMode(LINE_USART3_TX, PAL_MODE_ALTERNATE(7));
  /* USART3 RX.       */
  palSetLineMode(LINE_USART3_RX, PAL_MODE_ALTERNATE(7));
#endif
}

/*
 * Read GPIO that are used for:
 * a) general use
 *  or
 * b) UART and s/w I2C external.
 *
 * @return State of lines regardless of general or specific use.
 */
uint8_t pktReadIOlines() {
  return palReadLine(LINE_GPIO_PIN)
      | palReadLine(LINE_IO_TXD) << 1
      | palReadLine(LINE_IO_RXD) << 2;
}

void pktSerialStart(void) {
#if ENABLE_SERIAL_DEBUG == TRUE
  pktConfigSerialDiag();
  sdStart(SERIAL_CFG_DEBUG_DRIVER, &debug_config);
#endif
  /* Setup diagnostic resource access semaphore. */
  //extern binary_semaphore_t debug_out_sem;
  //chBSemObjectInit(&debug_out_sem, false);
}

void dbgWrite(uint8_t level, uint8_t *buf, uint32_t len) {
  (void)level;
#if ENABLE_SERIAL_DEBUG == TRUE
  chnWrite((BaseSequentialStream*)SERIAL_CFG_DEBUG_DRIVER, buf, len);
#else
  (void)buf;
  (void)len;
#endif
}

int dbgPrintf(uint8_t level, const char *format, ...) {
  (void)level;
#if ENABLE_SERIAL_DEBUG == TRUE
  va_list arg;
  int done;

  va_start(arg, format);
  done = chvprintf((BaseSequentialStream*)SERIAL_CFG_DEBUG_DRIVER, format, arg);
  va_end(arg);

  return done;
#else
  (void)format;
  return 0;
#endif
}

void pktWrite(uint8_t *buf, uint32_t len) {
#if ENABLE_SERIAL_DEBUG == TRUE
  chnWrite((BaseSequentialStream*)SERIAL_CFG_DEBUG_DRIVER, buf, len);
#else
  (void)buf;
  (void)len;
#endif
}

void pktConfigureCoreIO(void) {
  /* TODO: Put ALT mode selections in definitions. */
  /* Setup SPI3. */
  palSetLineMode(LINE_SPI_SCK, PAL_MODE_ALTERNATE(6)
                 | PAL_STM32_OSPEED_HIGHEST);     // SCK
  palSetLineMode(LINE_SPI_MISO, PAL_MODE_ALTERNATE(6)
                 | PAL_STM32_OSPEED_HIGHEST);    // MISO
  palSetLineMode(LINE_SPI_MOSI, PAL_MODE_ALTERNATE(6)
                 | PAL_STM32_OSPEED_HIGHEST);    // MOSI

  /* Setup I2C1. */
  palSetLineMode(LINE_I2C_SDA, PAL_MODE_ALTERNATE(4)
                 | PAL_STM32_OSPEED_HIGHEST
                 | PAL_STM32_OTYPE_OPENDRAIN); // SDA
  palSetLineMode(LINE_I2C_SCL, PAL_MODE_ALTERNATE(4)
                 | PAL_STM32_OSPEED_HIGHEST
                 | PAL_STM32_OTYPE_OPENDRAIN); // SCL

}

/** @} */


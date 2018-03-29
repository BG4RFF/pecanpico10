/**
 * Si446x driver specialized for APRS transmissions. The driver supports APRS
 * transmission and reception.
 * There can be either used the SLabs Si4463 or Si4464.
 */

#include "pktconf.h"
#ifndef PKT_IS_TEST_PROJECT
#include "debug.h"
#endif

#ifndef PKT_IS_TEST_PROJECT
#include "radio.h"
#endif

/*
 * Transmitter global variables.
 * Saved when setting band. */
//static uint32_t tx_frequency;
//static uint16_t tx_step;

// Si446x variables
static int16_t lastTemp = 0x7FFF;
//static bool radioInitialized;

// Receiver thread variables
/*static uint32_t rx_frequency;
static uint16_t rx_step;
static uint8_t rx_chan;
static uint8_t rx_rssi;
static mod_t rx_mod;*/

/* =================================================================== SPI communication ==================================================================== */

static const SPIConfig ls_spicfg = {
    .ssport = PAL_PORT(LINE_RADIO_CS),
    .sspad  = PAL_PAD(LINE_RADIO_CS),
    .cr1    = SPI_CR1_MSTR
};

static void Si446x_write(const uint8_t* txData, uint32_t len) {
    // Transmit data by SPI
  /* TODO: Add radio unit ID and get specific radio SPI driver. */
    uint8_t null_spi[len];

    /* Acquire bus and then start SPI. */
    spiAcquireBus(PKT_RADIO_SPI);
    spiStart(PKT_RADIO_SPI, &ls_spicfg);

    /* Poll for CTS. */
    uint8_t rx_ready[] = {Si446x_READ_CMD_BUFF, 0x00};
    do {
      spiSelect(PKT_RADIO_SPI);
      spiExchange(PKT_RADIO_SPI, 1, rx_ready, &rx_ready[1]);
      spiUnselect(PKT_RADIO_SPI);
    } while(rx_ready[1] != Si446x_COMMAND_CTS);

    /* Transfer data. Discard read back. */
    spiSelect(PKT_RADIO_SPI);
    spiExchange(PKT_RADIO_SPI, len, txData, null_spi);
    spiUnselect(PKT_RADIO_SPI);

    /* Stop SPI and relinquish bus. */
    spiStop(PKT_RADIO_SPI);
    spiReleaseBus(PKT_RADIO_SPI);
}

/**
 * Read data from Si446x. First CTS is polled.
 */
static void Si446x_read(const uint8_t* txData, uint32_t txlen, uint8_t* rxData, uint32_t rxlen) {
    // Transmit data by SPI
  /* TODO: Add radio unit ID and get SPI accordingly. */
    uint8_t null_spi[txlen];

    /* Acquire bus and then start SPI. */
    spiAcquireBus(PKT_RADIO_SPI);
    spiStart(PKT_RADIO_SPI, &ls_spicfg);

    /* Poll for CTS. */
    uint8_t rx_ready[] = {Si446x_READ_CMD_BUFF, 0x00};
    do {
      spiSelect(PKT_RADIO_SPI);
      spiExchange(PKT_RADIO_SPI, 1, rx_ready, &rx_ready[1]);
      spiUnselect(PKT_RADIO_SPI);
    } while(rx_ready[1] != Si446x_COMMAND_CTS);

    /* Write data. Discard read back. */
    spiSelect(PKT_RADIO_SPI);
    spiExchange(PKT_RADIO_SPI, txlen, txData, null_spi);

    /* Poll for read data. */
    do {
      spiUnselect(PKT_RADIO_SPI);
      spiSelect(PKT_RADIO_SPI);
      spiExchange(PKT_RADIO_SPI, rxlen, rx_ready, rxData);
    } while(rxData[1] != Si446x_COMMAND_CTS);

    /* Stop SPI and relinquish bus. */
    spiStop(PKT_RADIO_SPI);
    spiReleaseBus(PKT_RADIO_SPI);
}

static void Si446x_setProperty8(uint16_t reg, uint8_t val) {
    uint8_t msg[] = {0x11, (reg >> 8) & 0xFF, 0x01, reg & 0xFF, val};
    Si446x_write(msg, 5);
}

static void Si446x_setProperty16(uint16_t reg, uint8_t val1, uint8_t val2) {
    uint8_t msg[] = {0x11, (reg >> 8) & 0xFF, 0x02, reg & 0xFF, val1, val2};
    Si446x_write(msg, 6);
}

static void Si446x_setProperty24(uint16_t reg, uint8_t val1, uint8_t val2, uint8_t val3) {
    uint8_t msg[] = {0x11, (reg >> 8) & 0xFF, 0x03, reg & 0xFF, val1, val2, val3};
    Si446x_write(msg, 7);
}

static void Si446x_setProperty32(uint16_t reg, uint8_t val1, uint8_t val2, uint8_t val3, uint8_t val4) {
    uint8_t msg[] = {0x11, (reg >> 8) & 0xFF, 0x04, reg & 0xFF, val1, val2, val3, val4};
    Si446x_write(msg, 8);
}

/**
 * Initializes Si446x transceiver chip. Adjusts the frequency which is shifted by variable
 * oscillator voltage.
 * @param mv Oscillator voltage in mv
 */
static void Si446x_init(radio_unit_t radio) {

  TRACE_INFO("SI   > Init radio");

  packet_svc_t *handler = pktGetServiceObject(radio);

  chDbgAssert(handler != NULL, "invalid radio ID");

  pktConfigureRadioGPIO(radio);

    // Power up (send oscillator type)
    const uint8_t x3 = (Si446x_CCLK >> 24) & 0x0FF;
    const uint8_t x2 = (Si446x_CCLK >> 16) & 0x0FF;
    const uint8_t x1 = (Si446x_CCLK >>  8) & 0x0FF;
    const uint8_t x0 = (Si446x_CCLK >>  0) & 0x0FF;
    const uint8_t init_command[] = {0x02, 0x01, (Si446x_CLK_TCXO_EN & 0x1), x3, x2, x1, x0};
    Si446x_write(init_command, 7);
    chThdSleep(TIME_MS2I(25));

    // Set transceiver GPIOs
    uint8_t gpio_pin_cfg_command[] = {
        0x13,   // Command type = GPIO settings
        0x00,   // GPIO0        GPIO_MODE = DONOTHING
        0x15,   // GPIO1        GPIO_MODE = RAW_RX_DATA
        0x21,   // GPIO2        GPIO_MODE = RX_STATE
        0x20,   // GPIO3        GPIO_MODE = TX_STATE
        0x1B,   // NIRQ         NIRQ_MODE = CCA
        0x0B,   // SDO          SDO_MODE = SDO
        0x00    // GEN_CONFIG
    };
    Si446x_write(gpio_pin_cfg_command, 8);
    chThdSleep(TIME_MS2I(25));

    #if !Si446x_CLK_TCXO_EN
    Si446x_setProperty8(Si446x_GLOBAL_XO_TUNE, 0x00);
    #endif

    Si446x_setProperty8(Si446x_FRR_CTL_A_MODE, 0x00);
    Si446x_setProperty8(Si446x_FRR_CTL_B_MODE, 0x00);
    Si446x_setProperty8(Si446x_FRR_CTL_C_MODE, 0x00);
    Si446x_setProperty8(Si446x_FRR_CTL_D_MODE, 0x00);
    Si446x_setProperty8(Si446x_INT_CTL_ENABLE, 0x00);
    /* Set combined FIFO mode = 0x70. */
    //Si446x_setProperty8(Si446x_GLOBAL_CONFIG, 0x60);
    Si446x_setProperty8(Si446x_GLOBAL_CONFIG, 0x70);

    /* Clear FIFO. */
    const uint8_t reset_fifo[] = {0x15, 0x01};
    Si446x_write(reset_fifo, 2);
    /* No need to do this unreset... see si docs. */
    //const uint8_t unreset_fifo[] = {0x15, 0x00};
    //Si446x_write(unreset_fifo, 2);

    /*
     * TODO: Move the TX and RX settings out into the respective functions.
     * This would split up into AFSK and FSK for RX & TX.
     * Leave only common setup and init in here for selected base band frequency.
     */
    Si446x_setProperty8(Si446x_PREAMBLE_TX_LENGTH, 0x00);
    /* TODO: Use PREAMBLE_CONFIG_NSTD, etc. to send flags?
     * To do this with AFSK up-sampling requires a preamble pattern of 88 bits.
     * The 446x only has up to 32 pattern bits.
     * Why 88 bits? Due to the oversampling used to create AFSK at 13.2ksps.
     * Each HDLC bit takes 11 TX bit times.
     *
     * The alternative is to use TX_FIELDS.
     * Send preamble (HDLC flags) using FIELD_1 in a loop with fixed data 0x7E.
     * Field length can be 4096 bytes so up to 372 flags could be sent.
     * The flag bit stream uses 11 bytes per flag.
     * Using 200 flags would be 11 * 200 = 2200 bytes (17,600 stream bits).
     * Set FIELD_1 as 2,200 bytes and feed 200 x the bit pattern to the FIFO.
     * The transition to FIELD_2 is handled in the 446x packet handler.
     * Then FIELD_2 FIFO data is fed from the layer0 (bit stream) data buffer.
     */
    Si446x_setProperty8(Si446x_SYNC_CONFIG, 0x80);

    Si446x_setProperty8(Si446x_GLOBAL_CLK_CFG, 0x00);
    Si446x_setProperty8(Si446x_MODEM_RSSI_CONTROL, 0x00);
    /* TODO: Don't need this setting? */
    Si446x_setProperty8(Si446x_PREAMBLE_CONFIG_STD_1, 0x14);
    Si446x_setProperty8(Si446x_PKT_CONFIG1, 0x41);
    Si446x_setProperty8(Si446x_MODEM_MAP_CONTROL, 0x00);
    Si446x_setProperty8(Si446x_MODEM_DSM_CTRL, 0x07);
    Si446x_setProperty8(Si446x_MODEM_CLKGEN_BAND, 0x0D);

    Si446x_setProperty24(Si446x_MODEM_FREQ_DEV, 0x00, 0x00, 0x79);
    Si446x_setProperty8(Si446x_MODEM_TX_RAMP_DELAY, 0x01);
    Si446x_setProperty8(Si446x_PA_TC, 0x3D);
    Si446x_setProperty8(Si446x_FREQ_CONTROL_INTE, 0x41);
    Si446x_setProperty24(Si446x_FREQ_CONTROL_FRAC, 0x0B, 0xB1, 0x3B);
    Si446x_setProperty16(Si446x_FREQ_CONTROL_CHANNEL_STEP_SIZE, 0x0B, 0xD1);
    Si446x_setProperty8(Si446x_FREQ_CONTROL_W_SIZE, 0x20);
    Si446x_setProperty8(Si446x_FREQ_CONTROL_VCOCNT_RX_ADJ, 0xFA);
    Si446x_setProperty8(Si446x_MODEM_MDM_CTRL, 0x80);
    Si446x_setProperty8(Si446x_MODEM_IF_CONTROL, 0x08);
    Si446x_setProperty24(Si446x_MODEM_IF_FREQ, 0x02, 0x80, 0x00);
    Si446x_setProperty8(Si446x_MODEM_DECIMATION_CFG1, 0x70);
    Si446x_setProperty8(Si446x_MODEM_DECIMATION_CFG0, 0x10);
    Si446x_setProperty16(Si446x_MODEM_BCR_OSR, 0x01, 0xC3);
    Si446x_setProperty24(Si446x_MODEM_BCR_NCO_OFFSET, 0x01, 0x22, 0x60);
    Si446x_setProperty16(Si446x_MODEM_BCR_GAIN, 0x00, 0x91);
    Si446x_setProperty8(Si446x_MODEM_BCR_GEAR, 0x00);
    Si446x_setProperty8(Si446x_MODEM_BCR_MISC1, 0xC2);
    Si446x_setProperty8(Si446x_MODEM_AFC_GEAR, 0x54);
    Si446x_setProperty8(Si446x_MODEM_AFC_WAIT, 0x36);
    Si446x_setProperty16(Si446x_MODEM_AFC_GAIN, 0x80, 0xAB);
    Si446x_setProperty16(Si446x_MODEM_AFC_LIMITER, 0x02, 0x50);
    Si446x_setProperty8(Si446x_MODEM_AFC_MISC, 0x80);
    Si446x_setProperty8(Si446x_MODEM_AGC_CONTROL, 0xE2);
    Si446x_setProperty8(Si446x_MODEM_AGC_WINDOW_SIZE, 0x11);
    Si446x_setProperty8(Si446x_MODEM_AGC_RFPD_DECAY, 0x63);
    Si446x_setProperty8(Si446x_MODEM_AGC_IFPD_DECAY, 0x63);
    Si446x_setProperty8(Si446x_MODEM_FSK4_GAIN1, 0x00);
    Si446x_setProperty8(Si446x_MODEM_FSK4_GAIN0, 0x02);
    Si446x_setProperty16(Si446x_MODEM_FSK4_TH, 0x35, 0x55);
    Si446x_setProperty8(Si446x_MODEM_FSK4_MAP, 0x00);
    Si446x_setProperty8(Si446x_MODEM_OOK_PDTC, 0x2A);
    Si446x_setProperty8(Si446x_MODEM_OOK_CNT1, 0x85);
    Si446x_setProperty8(Si446x_MODEM_OOK_MISC, 0x23);
    Si446x_setProperty8(Si446x_MODEM_RAW_SEARCH, 0xD6);
    Si446x_setProperty8(Si446x_MODEM_RAW_CONTROL, 0x8F);
    Si446x_setProperty16(Si446x_MODEM_RAW_EYE, 0x00, 0x3B);
    Si446x_setProperty8(Si446x_MODEM_ANT_DIV_MODE, 0x01);
    Si446x_setProperty8(Si446x_MODEM_ANT_DIV_CONTROL, 0x80);
    Si446x_setProperty8(Si446x_MODEM_RSSI_COMP, 0x40);

    handler->radio_init = true;
}

void Si446x_conditional_init(radio_unit_t radio) {
// Initialize radio

  packet_svc_t *handler = pktGetServiceObject(radio);

  chDbgAssert(handler != NULL, "invalid radio ID");

  if(!handler->radio_init)
    Si446x_init(radio);
}

/*
 *
 */
bool Si446x_setBandParameters(radio_unit_t radio,
                              radio_freq_t freq,
                              channel_hz_t step) {

  /* Check band is in range. */
  if(freq < 144000000UL || freq > 900000000UL)
    return false;


  /* Set the output divider as recommended in Si446x data sheet. */
  uint32_t outdiv = 0;
  uint32_t band = 0;
  if(freq < 705000000UL) {outdiv = 6;  band = 1;}
  if(freq < 525000000UL) {outdiv = 8;  band = 2;}
  if(freq < 353000000UL) {outdiv = 12; band = 3;}
  if(freq < 239000000UL) {outdiv = 16; band = 4;}
  if(freq < 177000000UL) {outdiv = 24; band = 5;}

  Si446x_conditional_init(radio);

  /* Set the band parameter. */
  uint32_t sy_sel = 8;
  uint8_t set_band_property_command[] = {0x11, 0x20, 0x01, 0x51, (band + sy_sel)};
  Si446x_write(set_band_property_command, 5);

  /* Set the PLL parameters. */
  uint32_t f_pfd = 2 * Si446x_CCLK / outdiv;
  uint32_t n = ((uint32_t)(freq / f_pfd)) - 1;
  float ratio = (float)freq / (float)f_pfd;
  float rest  = ratio - (float)n;

  uint32_t m = (uint32_t)(rest * 524288UL);
  uint32_t m2 = m >> 16;
  uint32_t m1 = (m - m2 * 0x10000) >> 8;
  uint32_t m0 = (m - m2 * 0x10000 - (m1 << 8));

  uint32_t channel_increment = 524288 * outdiv * step / (2 * Si446x_CCLK);
  uint8_t c1 = channel_increment / 0x100;
  uint8_t c0 = channel_increment - (0x100 * c1);

  uint8_t set_frequency_property_command[] = {0x11, 0x40, 0x04, 0x00, n, m2, m1, m0, c1, c0};
  Si446x_write(set_frequency_property_command, 10);

  uint32_t x = ((((uint32_t)1 << 19) * outdiv * 1300.0)/(2*Si446x_CCLK))*2;
  uint8_t x2 = (x >> 16) & 0xFF;
  uint8_t x1 = (x >>  8) & 0xFF;
  uint8_t x0 = (x >>  0) & 0xFF;
  uint8_t set_deviation[] = {0x11, 0x20, 0x03, 0x0a, x2, x1, x0};
  Si446x_write(set_deviation, 7);
  return true;
}

/*static void Si446x_setShift(uint16_t shift)
{
    if(!shift)
        return;

    float units_per_hz = (( 0x40000 * outdiv ) / (float)Si446x_CCLK);

    // Set deviation for 2FSK
    uint32_t modem_freq_dev = (uint32_t)(units_per_hz * shift / 2.0 );
    uint8_t modem_freq_dev_0 = 0xFF & modem_freq_dev;
    uint8_t modem_freq_dev_1 = 0xFF & (modem_freq_dev >> 8);
    uint8_t modem_freq_dev_2 = 0xFF & (modem_freq_dev >> 16);

    uint8_t set_modem_freq_dev_command[] = {0x11, 0x20, 0x03, 0x0A, modem_freq_dev_2, modem_freq_dev_1, modem_freq_dev_0};
    Si446x_write(set_modem_freq_dev_command, 7);
}*/

static void Si446x_setPowerLevel(int8_t level)
{
    // Set the Power
    uint8_t set_pa_pwr_lvl_property_command[] = {0x11, 0x22, 0x01, 0x01, level};
    Si446x_write(set_pa_pwr_lvl_property_command, 5);
}



/* =========================================================== Radio specific modulation settings =========================================================== */

static void Si446x_setModemAFSK_TX(radio_unit_t radio) {
  /* TODO: Hardware mapping. */
  (void)radio;
    // Setup the NCO modulo and oversampling mode
    uint32_t s = Si446x_CCLK / 10;
    uint8_t f3 = (s >> 24) & 0xFF;
    uint8_t f2 = (s >> 16) & 0xFF;
    uint8_t f1 = (s >>  8) & 0xFF;
    uint8_t f0 = (s >>  0) & 0xFF;
    Si446x_setProperty32(Si446x_MODEM_TX_NCO_MODE, f3, f2, f1, f0);

    // Setup the NCO data rate for APRS
    Si446x_setProperty24(Si446x_MODEM_DATA_RATE, 0x00, 0x33, 0x90);

    // Use upsampled AFSK from FIFO (PH)
    Si446x_setProperty8(Si446x_MODEM_MOD_TYPE, 0x02);

    // Set AFSK filter
    const uint8_t coeff[] = {0x81, 0x9f, 0xc4, 0xee, 0x18, 0x3e, 0x5c, 0x70, 0x76};
    uint8_t i;
    for(i = 0; i < sizeof(coeff); i++) {
        uint8_t msg[] = {0x11, 0x20, 0x01, 0x17-i, coeff[i]};
        Si446x_write(msg, 5);
    }
}

static void Si446x_setModemAFSK_RX(radio_unit_t radio) {
  /* TODO: Hardware mapping. */
  (void)radio;
    // Setup the NCO modulo and oversampling mode
/*    uint32_t s = Si446x_CCLK;
    uint8_t f3 = (s >> 24) & 0xFF;
    uint8_t f2 = (s >> 16) & 0xFF;
    uint8_t f1 = (s >>  8) & 0xFF;
    uint8_t f0 = (s >>  0) & 0xFF;
    Si446x_setProperty32(Si446x_MODEM_TX_NCO_MODE, f3, f2, f1, f0);*/

    // Setup the NCO data rate for APRS
    //Si446x_setProperty24(Si446x_MODEM_DATA_RATE, 0x04, 0x07, 0x40);

    // Use 2FSK in DIRECT_MODE
    Si446x_setProperty8(Si446x_MODEM_MOD_TYPE, 0x0A);

    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE13_7_0, 0xFF);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE12_7_0, 0xC4);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE11_7_0, 0x30);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE10_7_0, 0x7F);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE9_7_0, 0x5F);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE8_7_0, 0xB5);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE7_7_0, 0xB8);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE6_7_0, 0xDE);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE5_7_0, 0x05);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE4_7_0, 0x17);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE3_7_0, 0x16);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE2_7_0, 0x0C);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE1_7_0, 0x03);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COE0_7_0, 0x00);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COEM0, 0x15);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COEM1, 0xFF);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COEM2, 0x00);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX1_CHFLT_COEM3, 0x00);

/*    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE13_7_0, 0xFF);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE12_7_0, 0xC4);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE11_7_0, 0x30);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE10_7_0, 0x7F);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE9_7_0, 0xF5);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE8_7_0, 0xB5);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE7_7_0, 0xB8);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE6_7_0, 0xDE);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE5_7_0, 0x05);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE4_7_0, 0x17);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE3_7_0, 0x16);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE2_7_0, 0x0C);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE1_7_0, 0x03);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COE0_7_0, 0x00);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COEM0, 0x15);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COEM1, 0xFF);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COEM2, 0x00);
    Si446x_setProperty8(Si446x_MODEM_CHFLT_RX2_CHFLT_COEM3, 0x00);*/
}

static void Si446x_setModem2FSK_TX(uint32_t speed)
{
    // Setup the NCO modulo and oversampling mode
    uint32_t s = Si446x_CCLK / 10;
    uint8_t f3 = (s >> 24) & 0xFF;
    uint8_t f2 = (s >> 16) & 0xFF;
    uint8_t f1 = (s >>  8) & 0xFF;
    uint8_t f0 = (s >>  0) & 0xFF;
    Si446x_setProperty32(Si446x_MODEM_TX_NCO_MODE, f3, f2, f1, f0);

    // Setup the NCO data rate for 2GFSK
    Si446x_setProperty24(Si446x_MODEM_DATA_RATE, (uint8_t)(speed >> 16), (uint8_t)(speed >> 8), (uint8_t)speed);

    // Use 2GFSK from FIFO (PH)
    Si446x_setProperty8(Si446x_MODEM_MOD_TYPE, 0x03);

    // Set 2GFSK filter (default per Si).
    const uint8_t coeff[] = {0x01, 0x03, 0x08, 0x11, 0x21, 0x36, 0x4d, 0x60, 0x67};
    uint8_t i;
    for(i = 0; i < sizeof(coeff); i++) {
        uint8_t msg[] = {0x11, 0x20, 0x01, 0x17-i, coeff[i]};
        Si446x_write(msg, 5);
    }
}


/* ====================================================================== Radio Settings ====================================================================== */

static uint8_t __attribute__((unused)) Si446x_getChannel(void) {
    const uint8_t state_info[] = {Si446x_REQUEST_DEVICE_STATE};
    uint8_t rxData[4];
    Si446x_read(state_info, sizeof(state_info), rxData, sizeof(rxData));
    return rxData[3];
}

/* ======================================================================= Radio FIFO ======================================================================= */

static void Si446x_writeFIFO(uint8_t *msg, uint8_t size) {
    uint8_t write_fifo[size+1];
    write_fifo[0] = 0x66;
    memcpy(&write_fifo[1], msg, size);
    Si446x_write(write_fifo, size+1);
}

static uint8_t Si446x_getTXfreeFIFO(void) {
    const uint8_t fifo_info[] = {Si446x_FIFO_INFO, 0x00};
    uint8_t rxData[4];
    Si446x_read(fifo_info, sizeof(fifo_info), rxData, sizeof(rxData));
    return rxData[3];
}

/* ====================================================================== Radio States ====================================================================== */

static uint8_t Si446x_getState(radio_unit_t radio) {
  /* TODO: add hardware mapping. */
  (void)radio;
    const uint8_t state_info[] = {Si446x_REQUEST_DEVICE_STATE};
    uint8_t rxData[4];
    Si446x_read(state_info, sizeof(state_info), rxData, sizeof(rxData));
    return rxData[2] & 0xF;
}

static void Si446x_setTXState(radio_unit_t radio, uint8_t chan, uint16_t size){
  /* TODO: add hardware mapping. */
  (void)radio;
    uint8_t change_state_command[] = {0x31, chan,
                                      (Si446x_STATE_READY << 4),
                                      (size >> 8) & 0x1F, size & 0xFF};
    Si446x_write(change_state_command, sizeof(change_state_command));
}

static void Si446x_setReadyState(radio_unit_t radio) {
  /* TODO: add hardware mapping. */
  (void)radio;
    const uint8_t change_state_command[] = {0x34, 0x03};
    Si446x_write(change_state_command, sizeof(change_state_command));
}

static void Si446x_setRXState(radio_unit_t radio, uint8_t chan){
  /* TODO: add hardware mapping. */
  (void)radio;
    const uint8_t change_state_command[] = {0x32, chan, 0x00, 0x00,
                                            0x00, 0x00, 0x08, 0x08};
    Si446x_write(change_state_command, sizeof(change_state_command));
}

void Si446x_shutdown(radio_unit_t radio) {
  TRACE_INFO("SI   > Shutdown radio %i", radio);
  packet_svc_t *handler = pktGetServiceObject(radio);

  chDbgAssert(handler != NULL, "invalid radio ID");

  pktDeconfigureRadioGPIO(radio);
  handler->radio_init = false;
}

/* ====================================================================== Radio TX/RX ======================================================================= */

static bool Si446x_isRadioInBand(radio_freq_t freq) {
  return (Si446x_MIN_FREQ <= freq && freq < Si446x_MAX_FREQ);
}

static bool Si446x_getLatchedCCA(radio_unit_t radio, uint8_t ms) {
  /* TODO: Hardware mapping. */
  (void)radio;
    uint16_t cca = 0;
    for(uint16_t i=0; i<ms*10; i++) {
        cca += Si446x_getCCA();
        /* FIXME: Using 5KHz systick lowest resolution is 200uS. */
        chThdSleep(TIME_US2I(100));
    }
    TRACE_INFO("SI   > CCA=%03d RX=%d", cca, cca > ms/10);
    return cca > ms; // Max. 1 spike per ms
}

/*
 * Wait for a clear time slot and initiate packet transmission.
 */
static bool Si446x_transmit(radio_unit_t radio,
                            radio_freq_t freq,
                            channel_hz_t step,
                            radio_ch_t chan,
                            radio_pwr_t power,
                            uint16_t size,
                            radio_squelch_t rssi,
                            sysinterval_t sql_timeout) {

  radio_freq_t op_freq = pktComputeOperatingFrequency(freq, step, chan);

    if(!Si446x_isRadioInBand(op_freq)) {
      TRACE_ERROR("SI   > Frequency out of range");
      TRACE_ERROR("SI   > abort transmission");
      return false;
    }

    // Switch to ready state
    if(Si446x_getState(radio) == Si446x_STATE_RX) {
      TRACE_INFO("SI   > Switch Si446x to ready state");
      Si446x_setReadyState(radio);
      chThdSleep(TIME_MS2I(1));
    }

    Si446x_setProperty8(Si446x_MODEM_RSSI_THRESH, rssi);
    /* Set band parameters. */
    Si446x_setBandParameters(radio, freq, step);     // Set frequency

    /* Listen on the TX frequency. */
    Si446x_setRXState(radio, chan);

    // Wait until nobody is transmitting (until timeout)

    if(Si446x_getState(radio) != Si446x_STATE_RX
        || Si446x_getLatchedCCA(radio, 50)) {

        TRACE_INFO( "SI   > Wait for clear channel on %d.%03d MHz",
                    op_freq/1000000, (op_freq%1000000)/1000);

        /* FIXME: Fix timeout. Using 5KHz systick lowest resolution is 200uS. */
        sysinterval_t t0 = chVTGetSystemTime();
        while((Si446x_getState(radio) != Si446x_STATE_RX
            || Si446x_getLatchedCCA(radio, 50))
            && chVTGetSystemTime() - t0 < sql_timeout)
            chThdSleep(TIME_US2I(100));
    }

    // Transmit
    TRACE_INFO("SI   > Tune Si446x (TX)");
    Si446x_setReadyState(radio);
    /* Set band parameters back to normal TX. */
    //Si446x_setBandParameters(radio, freq, step);     // Set frequency
    Si446x_setPowerLevel(power);        // Set power level
    Si446x_setTXState(radio, chan, size);

    // Wait until transceiver enters transmission state
    /* TODO: Make a function to handle timeout on fail to reach state. */
    while(Si446x_getState(radio) != Si446x_STATE_TX) {
        chThdSleep(TIME_US2I(500));
    }
    return true;
}

/*
 *
 */
bool Si446x_receiveNoLock(radio_unit_t radio,
                          radio_freq_t freq,
                          channel_hz_t step,
                          radio_ch_t channel,
                          radio_squelch_t rssi,
                          mod_t mod) {
  radio_freq_t op_freq = pktComputeOperatingFrequency(freq, step, channel);
  /* TODO: compute f + s*c. */
  if(!Si446x_isRadioInBand(op_freq)) {
    TRACE_ERROR("SI   > Frequency out of range");
    TRACE_ERROR("SI   > abort reception");
    return false;
  }

  uint16_t tot = 0;
  // Wait until transceiver finishes transmission (if there is any)
  while(Si446x_getState(radio) == Si446x_STATE_TX) {
    chThdSleep(TIME_MS2I(10));
    if(tot++ < 500)
      continue;
    /* Remove TX state. */
    Si446x_setReadyState(radio);

    TRACE_ERROR("SI   > Timeout waiting for TX state end");
    TRACE_ERROR("SI   > Attempt start of receive");

    break;
  }

  // Initialize radio
  if(mod == MOD_AFSK) {
      Si446x_setModemAFSK_RX(radio);
  } else {
      TRACE_ERROR("SI   > Modulation type not supported in receive");
      TRACE_ERROR("SI   > abort reception");

      return false;
  }

  // Preserve settings in case transceiver changes to TX state
/*  rx_rssi = rssi;
  rx_chan = channel;
  rx_mod = mod;*/

  TRACE_INFO("SI   > Tune Si446x (RX)");

  Si446x_setProperty8(Si446x_MODEM_RSSI_THRESH, rssi);

  Si446x_setRXState(radio, channel);

  // Wait for the receiver to start (because it is used as mutex)
  while(Si446x_getState(radio) != Si446x_STATE_RX)
      chThdSleep(TIME_MS2I(1));
  return true;
}

/*
 * Start or restore reception if it was paused for TX.
 * return true if RX was enabled and/or resumed OK.
 * return false if RX was not enabled succesfully.
 */
bool Si4464_resumeReceive(radio_unit_t radio,
                          radio_freq_t rx_frequency,
                          channel_hz_t rx_step,
                          radio_ch_t rx_chan,
                          radio_squelch_t rx_rssi,
                          mod_t rx_mod) {
  (void)radio;
  bool ret = true;


  radio_freq_t op_freq = pktComputeOperatingFrequency(rx_frequency,
                                                        rx_step,
                                                        rx_chan);

  TRACE_INFO( "SI   > Enable packet reception %d.%03d MHz (ch %d),"
              " RSSI %d, %s",
              op_freq/1000000, (op_freq % 1000000)/1000,
              rx_chan,
              rx_rssi, getModulation(rx_mod));

  /* Resume reception. */
  Si446x_setBandParameters(radio, rx_frequency, rx_step);
  ret = Si446x_receiveNoLock(radio, rx_frequency, rx_step,
                             rx_chan, rx_rssi, rx_mod);
  return ret;
}

/*
 *
 */
void Si446x_disableReceive(radio_unit_t radio) {
  /* FIXME: */
  if(Si446x_getState(radio) == Si446x_STATE_RX) {
    //rx_frequency = 0;
    Si446x_shutdown(radio);
  }
}

/*
 *
 */
void Si446x_pauseReceive(radio_unit_t radio) {
  /* FIXME: */
  if(Si446x_getState(radio) == Si446x_STATE_RX) {
    Si446x_setReadyState(radio);
    while(Si446x_getState(radio) == Si446x_STATE_RX);
  }
}

/* ==================================================================== AFSK Transmitter ==================================================================== */

#define PLAYBACK_RATE       13200
#define BAUD_RATE           1200                                    /* APRS AFSK baudrate */
#define SAMPLES_PER_BAUD    (PLAYBACK_RATE / BAUD_RATE)             /* Samples per baud (13200Hz / 1200baud = 11samp/baud) */
#define PHASE_DELTA_1200    (((2 * 1200) << 16) / PLAYBACK_RATE)    /* Delta-phase per sample for 1200Hz tone */
#define PHASE_DELTA_2200    (((2 * 2200) << 16) / PLAYBACK_RATE)    /* Delta-phase per sample for 2200Hz tone */

static uint32_t phase_delta;            // 1200/2200 for standard AX.25
static uint32_t phase;                  // Fixed point 9.7 (2PI = TABLE_SIZE)
static uint32_t packet_pos;             // Next bit to be sent out
static uint32_t current_sample_in_baud; // 1 bit = SAMPLES_PER_BAUD samples
static uint8_t current_byte;
//static uint8_t ctone = 0;

/*static bool Si446x_getBitAsNRZI(bool bit) {
    if((bit & 0x1) == 0)
        ctone = !ctone;
    return ctone;
}*/

/*
 * Create a bit stream of AFSK (NRZI & HDLC) encoded packet data.
 */
/*static uint32_t Si446x_encodeDataToAFSK(uint8_t *inbuf, uint32_t inlen,
                     uint8_t* buf, uint32_t buf_len, uint8_t pre_len) {
    memset(buf, 0, buf_len); // Clear buffer
    uint32_t blen = 0;

    // Preamble (HDLC flags)
    for(uint8_t i = 0; i < pre_len; i++) {
        for(uint8_t j = 0; j < 8; j++) {

            if(blen >> 3 >= buf_len) { // Buffer overflow

                TRACE_ERROR("SI   > Preamble too long");


                return 0;
            }

            buf[blen >> 3] |= Si446x_getBitAsNRZI((0x7E >> j) & 0x1) << (blen % 8);
            blen++;
        }
    }

    // Insert CRC to buffer
    uint16_t crc = calc_crc16(inbuf, 0, inlen);
    inbuf[inlen++] = crc & 0xFF;
    inbuf[inlen++] = crc >> 8;

    uint32_t pos = 0;
    uint8_t bitstuff_cntr = 0;

    while(pos < inlen*8)
    {
        if(blen >> 3 >= buf_len) { // Buffer overflow

          TRACE_ERROR("SI   > Packet too long");

            return 0;
        }

        bool bit;
        if(bitstuff_cntr < 5) { // Normal bit

            bit = (inbuf[pos >> 3] >> (pos%8)) & 0x1;
            if(bit == 1) {
                bitstuff_cntr++;
            } else {
                bitstuff_cntr = 0;
            }
            pos++;

        } else { // Fill stuffing bit

            bit = 0;
            bitstuff_cntr = 0;

        }

        // NRZ-I encode bit
        bool nrzi = Si446x_getBitAsNRZI(bit);

        buf[blen >> 3] |= nrzi << (blen % 8);
        blen++;
    }

    // Final flag
    for(uint8_t i=0; i<10; i++)
        for(uint8_t j=0; j<8; j++) {

            if(blen >> 3 >= buf_len) { // Buffer overflow


                TRACE_ERROR("SI   > Packet too long");

                return 0;
            }

            buf[blen >> 3] |= Si446x_getBitAsNRZI((0x7E >> j) & 0x1) << (blen % 8);
            blen++;
        }

    return blen;
}*/

static uint8_t Si446x_getUpsampledAFSKbits(uint8_t* buf/*, uint32_t blen*/)
{
  /* This function may be called with different bit stream sources.
   * These will have their own blen so checking is not valid.
   */
    //if(packet_pos == blen)
      /* Packet transmission finished already so just return a zero. */
      //return 0;

    uint8_t b = 0;
    for(uint8_t i = 0; i < 8; i++)
    {
        if(current_sample_in_baud == 0) {
            if((packet_pos & 7) == 0) { // Load up next byte
                current_byte = buf[packet_pos >> 3];
            } else { // Load up next bit
                current_byte = current_byte / 2;
            }
        }

        // Toggle tone (1200 <> 2200)
        phase_delta = (current_byte & 1) ? PHASE_DELTA_1200 : PHASE_DELTA_2200;
        /* Add delta-phase (bit count within SAMPLES_PER_BAUD). */
        phase += phase_delta;
        b |= ((phase >> 16) & 1) << i;  // Set modulation bit

        current_sample_in_baud++;

        if(current_sample_in_baud == SAMPLES_PER_BAUD) {    // Old bit consumed, load next bit
            current_sample_in_baud = 0;
            packet_pos++;
        }
    }
    return b;
}

static void __attribute__((unused)) Si446x_upsampleNRZIstream(uint8_t current_byte,
                                               uint8_t *buf,
                                               uint8_t upsample_rate) {
  uint8_t b = 0, i = 0, usr;
  for(usr = 0; usr < upsample_rate; usr++) {
    do {
      // Toggle tone (1200 <> 2200)
      phase_delta = (current_byte & 1) ? PHASE_DELTA_1200 : PHASE_DELTA_2200;
      /* Add delta-phase (bit count within SAMPLES_PER_BAUD). */
      phase += phase_delta;
      b |= ((phase >> 16) & 1) << i;  // Set modulation bit
      current_byte >>= (usr / upsample_rate) * 8;
    } while(++i < 8);
    i = 0;
    buf[usr] = b;
  }
}

#define SI446X_EVT_AFSK_TX_TIMEOUT      EVENT_MASK(0)

static void Si446x_transmitTimeoutI(thread_t *tp) {
  /* The tell the thread to terminate. */
  chEvtSignal(tp, SI446X_EVT_AFSK_TX_TIMEOUT);
}

/*
 * Simple AFSK send thread using minimized buffering.
 * Uses an iterator to size NRZI output and allocate suitably sized buffer.
 * Plan is to replace with a version using even less memory.
 *
 */
THD_FUNCTION(min_si_fifo_feeder_afsk, arg) {
    packet_t pp = arg;

    radio_unit_t radio = pp->radio;

    pktAcquireRadio(radio);

    /* Initialize radio. */
    Si446x_conditional_init(radio);

     Si446x_setBandParameters(radio, pp->base_frequency,
                              pp->radio_step);

     /* Set 446x back to READY. */
     Si446x_pauseReceive(radio);

     Si446x_setModemAFSK_TX(radio);

    /* Initialize variables for AFSK encoder. */
    virtual_timer_t send_timer;

    chVTObjectInit(&send_timer);

    tx_iterator_t iterator;

    /*
     * Set NRZI encoding format.
     * Iterator object.
     * Packet reference.
     * Preamble length (HDLC flags)
     * Postamble length (HDLC flags)
     * Tail length (HDLC zeros)
     * Scramble off/on
     */
    pktStreamIteratorInit(&iterator, pp, 30, 10, 10, false);

    uint16_t all = pktStreamEncodingIterator(&iterator, NULL, 0);

    TRACE_INFO("SI   > AFSK packet stream bytes %i", all);

    if(all == 0) {
      /* Nothing encoded. Release packet send object. */


      TRACE_DEBUG("SI   > AFSK TX no NRZI data encoded");

      // Free packet object memory
      pktReleaseSendObject(pp);

      /* Schedule thread memory release. */
      pktScheduleThreadRelease(radio, chThdGetSelfX());

      /* Exit thread. */
      chThdExit(MSG_RESET);
    }
    /* Allocate buffer and perform NRZI encoding. */
    uint8_t layer0[all];
    memset(layer0, 0, sizeof(layer0));
    pktStreamEncodingIterator(&iterator, layer0, all);

    all *= SAMPLES_PER_BAUD;
    /* Reset TX FIFO in case some remnant unsent data is left there. */
    const uint8_t reset_fifo[] = {0x15, 0x01};
    Si446x_write(reset_fifo, 2);

    /* Initialize variables for up sampler. */
    phase_delta = PHASE_DELTA_1200;
    phase = 0;
    packet_pos = 0;
    current_sample_in_baud = 0;
    current_byte = 0;

    /* Maximum amount of FIFO data when using combined TX+RX (safe size). */
    uint8_t localBuffer[Si446x_FIFO_COMBINED_SIZE];

    /* Get the FIFO buffer amount currently available. */
    uint8_t free = Si446x_getTXfreeFIFO();

    /* Calculate initial FIFO fill. */
    uint16_t c = (all > free) ? free : all;

    /*
     * Start transmission timeout timer.
     * If the 446x gets locked up we'll exit TX and release packet object.
     */
    chVTSet(&send_timer, TIME_S2I(10),
             (vtfunc_t)Si446x_transmitTimeoutI, chThdGetSelfX());

    /* The exit message if all goes well. */
    msg_t exit_msg = MSG_OK;

    /* Initial FIFO load. */
    for(uint16_t i = 0;  i < c; i++)
        localBuffer[i] = Si446x_getUpsampledAFSKbits(layer0);
    Si446x_writeFIFO(localBuffer, c);

    uint8_t lower = 0;

    /* Request start of transmission. */
    if(Si446x_transmit(radio,
                       pp->base_frequency,
                       pp->radio_step,
                       pp->radio_chan,
                       pp->radio_pwr,
                       all,
                       pp->cca_rssi,
                       TIME_S2I(10))) {
      /* Feed the FIFO while data remains to be sent. */
      while((all - c) > 0) {
        /* Get TX FIFO free count. */
        uint8_t more = Si446x_getTXfreeFIFO();
        /* Update the FIFO free low water mark. */
        lower = (more > lower) ? more : lower;

        /* If there is more free than we need for send use remainder only. */
        more = (more > (all - c)) ? (all - c) : more;

        /* Load the FIFO. */
        for(uint16_t i = 0; i < more; i++)
            localBuffer[i] = Si446x_getUpsampledAFSKbits(layer0);
        Si446x_writeFIFO(localBuffer, more); // Write into FIFO
        c += more;

        /*
         * Wait for a timeout event during up-sampled NRZI send.
         * Time delay allows ~SAMPLES_PER_BAUD bytes to be consumed from FIFO.
         * If no timeout event go back and load more data to FIFO.
         */
        eventmask_t evt = chEvtWaitAnyTimeout(SI446X_EVT_AFSK_TX_TIMEOUT,
                                   chTimeUS2I(833 * 8));
        if(evt) {
          /* Force 446x out of TX state. */
          Si446x_setReadyState(radio);
          exit_msg = MSG_TIMEOUT;
          break;
        }
      }
    } else {
      /* Transmit start failed. */
        TRACE_ERROR("SI   > Transmit start failed");
    }
    chVTReset(&send_timer);

    /*
     * If nothing went wrong wait for TX to finish.
     * Else don't wait.
     */
    while(Si446x_getState(radio) == Si446x_STATE_TX && exit_msg == MSG_OK) {
      /* Sleep for an AFSK byte time. */
      chThdSleep(chTimeUS2I(833 * 8));
      continue;
    }



    TRACE_INFO("SI   > AFSK TX FIFO lowest free level %i", lower);

    // Free packet object memory
    pktReleaseSendObject(pp);

    /* Schedule thread memory release. */
    pktScheduleThreadRelease(radio, chThdGetSelfX());

    /* Exit thread. */
    chThdExit(exit_msg);
}

/*
 *
 */
void Si446x_sendAFSK(packet_t pp) {

    thread_t *afsk_feeder_thd = NULL;

    /* Create a send thread name which includes the sequence number. */
    chsnprintf(pp->tx_thd_name, sizeof(pp->tx_thd_name),
               "446x_afsk_tx_%03i", pp->tx_seq);

    afsk_feeder_thd = chThdCreateFromHeap(NULL,
                THD_WORKING_AREA_SIZE(SI_AFSK_FIFO_MIN_FEEDER_WA_SIZE),
                pp->tx_thd_name,
                NORMALPRIO - 10,
                min_si_fifo_feeder_afsk,
                pp);


    if(afsk_feeder_thd == NULL) {
      /* Release packet object. */
      pktReleaseSendObject(pp);

      TRACE_ERROR("SI   > Unable to create AFSK transmit thread");
    }
    return;
}


/* ===================================================================== AFSK Receiver ====================================================================== */



void Si446x_stopDecoder(void) {
    // TODO: Nothing yet here
}

/* ========================================================================== 2FSK ========================================================================== */

/*
 * New 2FSK send thread using minimised buffer space.
 */
THD_FUNCTION(min_si_fifo_feeder_fsk, arg) {
  packet_t pp = arg;

  radio_unit_t radio = pp->radio;

  /* TODO: Check result. */
  pktAcquireRadio(radio);

  // Initialize radio
  Si446x_conditional_init(radio);

  /* Set 446x back to READY. */
  Si446x_pauseReceive(radio);

  Si446x_setBandParameters(radio, pp->base_frequency, pp->radio_step);

  /* Set parameters for 2FSK transmission.
   * TODO: Should we pass in 9600 or just set it here?
   * In any case we should have a define I guess. */
  Si446x_setModem2FSK_TX(9600);

  /* Initialize variables for 2FSK encoder. */

  virtual_timer_t send_timer;

  chVTObjectInit(&send_timer);

  tx_iterator_t iterator;

  pktStreamIteratorInit(&iterator, pp, 30, 10, 10, true);

  /* Compute size of NRZI stream. */
  uint16_t all = pktStreamEncodingIterator(&iterator, NULL, 0);

  TRACE_INFO("SI   > 2FSK packet stream bytes %i", all);

  if(all == 0) {
    /* Nothing encoded. Release packet send object. */


    TRACE_DEBUG("SI   > 2FSK TX no NRZI data encoded");

    // Free packet object memory
    pktReleaseSendObject(pp);

    /* Schedule thread memory release. */
    pktScheduleThreadRelease(radio, chThdGetSelfX());

    /* Exit thread. */
    chThdExit(MSG_RESET);
  }
  /* Allocate buffer and perform NRZI encoding. */
  uint8_t layer0[all];
  memset(layer0, 0, sizeof(layer0));
  pktStreamEncodingIterator(&iterator, layer0, all);

  /* Reset TX FIFO in case some remnant unsent data is left there. */
  const uint8_t reset_fifo[] = {0x15, 0x01};
  Si446x_write(reset_fifo, 2);

  /* Get the FIFO buffer amount currently available. */
  uint8_t free = Si446x_getTXfreeFIFO();

  /* Calculate initial FIFO fill. */
  uint16_t c = (all > free) ? free : all;

  /*
   * Start transmission timeout timer.
   * If the 446x gets locked up we'll exit TX and release packet object.
   */
  chVTSet(&send_timer, TIME_S2I(10),
           (vtfunc_t)Si446x_transmitTimeoutI, chThdGetSelfX());

  /* The exit message if all goes well. */
  msg_t exit_msg = MSG_OK;

  uint8_t *bufp = layer0;

  /* Initial FIFO load. */
  Si446x_writeFIFO(bufp, c);
  bufp += c;
  uint8_t lower = 0;

  /* Request start of transmission. */
  if(Si446x_transmit(radio,
                     pp->base_frequency,
                     pp->radio_step,
                     pp->radio_chan,
                     pp->radio_pwr,
                     all,
                     pp->cca_rssi,
                     TIME_S2I(10))) {
    /* Feed the FIFO while data remains to be sent. */
    while((all - c) > 0) {
      /* Get TX FIFO free count. */
      uint8_t more = Si446x_getTXfreeFIFO();
      /* Update the FIFO free low water mark. */
      lower = (more > lower) ? more : lower;

      /* If there is more free than we need for send use remainder only. */
      more = (more > (all - c)) ? (all - c) : more;

      /* Load the FIFO. */
      Si446x_writeFIFO(bufp, more); // Write into FIFO
      bufp += more;
      c += more;

      /*
       * Wait for a timeout event during up-sampled NRZI send.
       * Time delay allows ~10 bytes to be consumed from FIFO.
       * If no timeout event go back and load more data to FIFO.
       */
      eventmask_t evt = chEvtWaitAnyTimeout(SI446X_EVT_AFSK_TX_TIMEOUT,
                                 chTimeUS2I(104 * 8 * 10));
      if(evt) {
        /* Force 446x out of TX state. */
        Si446x_setReadyState(radio);
        exit_msg = MSG_TIMEOUT;
        break;
      }
    }
  } else {
    /* Transmit start failed. */
      TRACE_ERROR("SI   > 2FSK transmit start failed");
  }
  chVTReset(&send_timer);

  /*
   * If nothing went wrong wait for TX to finish.
   * Else don't wait.
   */
  while(Si446x_getState(radio) == Si446x_STATE_TX && exit_msg == MSG_OK) {
    /* Sleep for a 2FSK byte time. */
    chThdSleep(chTimeUS2I(104 * 8 * 10));
    continue;
  }

  TRACE_INFO("SI   > 2FSK TX FIFO lowest free level %i", lower);

  // Free packet object memory
  pktReleaseSendObject(pp);

  /* Schedule thread memory release. */
  pktScheduleThreadRelease(radio, chThdGetSelfX());

  /* Exit thread. */
  chThdExit(exit_msg);
}

/*
 *
 */
void Si446x_send2FSK(packet_t pp) {

  thread_t *fsk_feeder_thd = NULL;

  /* Create a send thread name which includes the sequence number. */
  chsnprintf(pp->tx_thd_name, sizeof(pp->tx_thd_name),
             "446x_2fsk_tx_%03i", pp->tx_seq);

  fsk_feeder_thd = chThdCreateFromHeap(NULL,
              THD_WORKING_AREA_SIZE(SI_FSK_FIFO_FEEDER_WA_SIZE),
              pp->tx_thd_name,
              NORMALPRIO - 10,
              min_si_fifo_feeder_fsk,
              pp);

  if(fsk_feeder_thd == NULL) {
    /* Release packet object. */
    pktReleaseSendObject(pp);

    TRACE_ERROR("SI   > Unable to create FSK transmit thread");
  }
  return;
}

/* ========================================================================== Misc ========================================================================== */

static int16_t Si446x_getTemperature(void) {
  const uint8_t txData[2] = {0x14, 0x10};
  uint8_t rxData[8];
  Si446x_read(txData, 2, rxData, 8);
  uint16_t adc = rxData[7] | ((rxData[6] & 0x7) << 8);
  return (89900*adc)/4096 - 29300;
}

int16_t Si446x_getLastTemperature(radio_unit_t radio) {
  if(lastTemp == 0x7FFF) { // Temperature was never measured => measure it now
    packet_svc_t *handler = pktGetServiceObject(radio);

    chDbgAssert(handler != NULL, "invalid radio ID");

    if(handler->radio_init) {
      pktAcquireRadio(radio);
      // Temperature readout
      lastTemp = Si446x_getTemperature();
      TRACE_INFO("SI   > Transmitter temperature %d degC\r\n", lastTemp/100);
      pktReleaseRadio(radio);
    } else {
      TRACE_INFO("SI   > Transmitter temperature not available");
      return 0;
    }
  }
  return lastTemp;
}

//#endif

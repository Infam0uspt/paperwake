#pragma once
// Stub: ESP-IDF 5.x removed driver/dac.h. Provide minimal typedefs so
// ESP8266Audio compiles; these DAC modes are never used (we use I2S).
// Note: These are never used - PaperWake uses external I2S DAC.

enum dac_channel_t : int {
  DAC_CHANNEL_1 = 1,
  DAC_CHANNEL_2 = 2,
  DAC_CHANNEL_BOTH = 3
};

static inline void dac_output_enable(dac_channel_t channel) {
  (void)channel;
}

static inline void dac_output_voltage(dac_channel_t channel, int voltage) {
  (void)channel;
  (void)voltage;
}
 

#include <driver/i2s.h>

#define I2S_SAMPLE_RATE 16000
#define I2S_BCLK 26
#define I2S_LRCLK 27
#define I2S_DIN 25

void setup() {
  // I2S configuration
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = I2S_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false
  };

  // I2S pin mapping
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRCLK,
    .data_out_num = I2S_DIN,
    .data_in_num = -1
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

void loop() {
  // Generate a simple alarm tone
  const int toneFreq = 1000; // 1 kHz alarm
  const int samples = 16000 / toneFreq;
  int16_t buffer[samples];

  for (int i = 0; i < samples; i++) {
    buffer[i] = (i % 2 == 0) ? 20000 : -20000; // square wave
  }

  size_t bytesWritten;
  i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &bytesWritten, portMAX_DELAY);
}

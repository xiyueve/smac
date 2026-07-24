#include "driver/i2s.h"
#include <math.h>

// ── I2S pins for MAX98357A ──
#define I2S_BCLK 12
#define I2S_LRC  14
#define I2S_DOUT 13

// ── Beep settings ──
#define SAMPLE_RATE   44100
#define BEEP_FREQ     1000
#define BEEP_ON_MS    400
#define BEEP_OFF_MS   200
#define VOLUME        0.5

bool alarmRinging = true; // starts ON automatically

void setupI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = true
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num   = I2S_BCLK,
    .ws_io_num    = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num  = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

void playBeep(int freq, int durationMs) {
  int samples = (SAMPLE_RATE * durationMs) / 1000;
  int16_t buffer[64];
  int bufIdx = 0;
  size_t bytesWritten;

  for (int i = 0; i < samples; i++) {
    float sample = sin(2.0 * M_PI * freq * i / SAMPLE_RATE);
    float fade = 1.0;
    int fadeLen = SAMPLE_RATE / 100;
    if (i < fadeLen) fade = (float)i / fadeLen;
    if (i > samples - fadeLen) fade = (float)(samples - i) / fadeLen;
    int16_t val = (int16_t)(sample * VOLUME * fade * 32767);
    buffer[bufIdx++] = val;
    buffer[bufIdx++] = val;
    if (bufIdx >= 64) {
      i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &bytesWritten, portMAX_DELAY);
      bufIdx = 0;
    }
  }
  if (bufIdx > 0) {
    i2s_write(I2S_NUM_0, buffer, bufIdx * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
  }
}

void playSilence(int durationMs) {
  int samples = (SAMPLE_RATE * durationMs) / 1000;
  int16_t buffer[64] = {0};
  size_t bytesWritten;
  int sent = 0;
  while (sent < samples * 2) {
    i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &bytesWritten, portMAX_DELAY);
    sent += 64;
  }
}

void alarmTask(void* param) {
  while (true) {
    if (alarmRinging) {
      playBeep(BEEP_FREQ, BEEP_ON_MS);
      playSilence(BEEP_OFF_MS);
      playBeep(BEEP_FREQ, BEEP_ON_MS);
      playSilence(BEEP_OFF_MS);
      playBeep(BEEP_FREQ, BEEP_ON_MS);
      playSilence(600);
    } else {
      vTaskDelay(100 / portTICK_PERIOD_MS);
    }
  }
}

void setup() {
  Serial.begin(115200);
  setupI2S();

  xTaskCreatePinnedToCore(
    alarmTask,
    "AlarmTask",
    4096,
    NULL,
    1,
    NULL,
    0
  );

  Serial.println("Beeping! Type 's' to stop, 'a' to start again.");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'a') { alarmRinging = true;  Serial.println("Alarm ON!"); }
    if (c == 's') { alarmRinging = false; Serial.println("Alarm OFF!"); }
  }
}

/*
  Dual Photodiode True Random Number Generator (TRNG) - Option B (DMA Continuous Mode)
  
  - Target Board: ESP32 Dev Module (Espressif Arduino Core 3.x / ESP-IDF v5)
  - Uses ESP32 Hardware SAR ADC1 in Continuous DMA Mode
  - Channels: GPIO 34 (ADC1_CHANNEL_6) & GPIO 35 (ADC1_CHANNEL_7)
  - Hardware DMA samples automatically in background at 60 kHz into DMA memory
  - Zero CPU analogRead() polling: CPU reads completed DMA frames from RAM
  - Streams 64-byte payload frames: [0xAA][0x55][64 raw bytes] over Serial at 921,600 baud
  - Whitening and debiasing performed on C++ backend via 32-bit Galois LFSR
*/

#include <Arduino.h>
#include "esp_adc/adc_continuous.h"

// Configuration constants
#define DMA_FRAME_SIZE 256
#define ADC_SAMPLE_FREQ_HZ 60000 // 60 kHz continuous hardware sampling

const int greenLED = 4;
const int redLED = 5;
const int BUTTON_PIN = 25;

bool running = true;
int buttonState;
int lastButtonState;
unsigned long lastDebounceTime = 0;
unsigned long lastLEDTime = 0;
const int ledDelay = 100;
uint8_t lastEntropyByte = 0;

adc_continuous_handle_t adcHandle = NULL;
uint8_t dmaBuffer[DMA_FRAME_SIZE];

// Attenuation and bit-width compatibility macros
#ifdef ADC_ATTEN_DB_12
  #define TRNG_ADC_ATTEN ADC_ATTEN_DB_12
#else
  #define TRNG_ADC_ATTEN ADC_ATTEN_DB_11
#endif

#ifdef SOC_ADC_DIGI_MAX_BITWIDTH
  #define TRNG_BIT_WIDTH SOC_ADC_DIGI_MAX_BITWIDTH
#else
  #define TRNG_BIT_WIDTH 12
#endif

// Output format macros for ESP32 (Type 1 format on ESP32)
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
  #define ADC_OUTPUT_TYPE ADC_DIGI_OUTPUT_FORMAT_TYPE1
  #define ADC_GET_CHANNEL(p) ((p)->type1.channel)
  #define ADC_GET_DATA(p) ((p)->type1.data)
#else
  #define ADC_OUTPUT_TYPE ADC_DIGI_OUTPUT_FORMAT_TYPE2
  #define ADC_GET_CHANNEL(p) ((p)->type2.channel)
  #define ADC_GET_DATA(p) ((p)->type2.data)
#endif

// 64-byte payload transmission packet
uint8_t packet[66];
int packetIndex = 0;

void initADCContinuous() {
  adc_continuous_handle_cfg_t adcConfig;
  memset(&adcConfig, 0, sizeof(adcConfig));
  adcConfig.max_store_buf_size = 2048;
  adcConfig.conv_frame_size = DMA_FRAME_SIZE;
  ESP_ERROR_CHECK(adc_continuous_new_handle(&adcConfig, &adcHandle));

  adc_digi_pattern_config_t adcPattern[2];
  memset(adcPattern, 0, sizeof(adcPattern));

  adcPattern[0].atten = TRNG_ADC_ATTEN;
  adcPattern[0].channel = ADC_CHANNEL_6; // GPIO 34
  adcPattern[0].unit = ADC_UNIT_1;
  adcPattern[0].bit_width = TRNG_BIT_WIDTH;

  adcPattern[1].atten = TRNG_ADC_ATTEN;
  adcPattern[1].channel = ADC_CHANNEL_7; // GPIO 35
  adcPattern[1].unit = ADC_UNIT_1;
  adcPattern[1].bit_width = TRNG_BIT_WIDTH;

  adc_continuous_config_t digConfig;
  memset(&digConfig, 0, sizeof(digConfig));
  digConfig.pattern_num = 2;
  digConfig.adc_pattern = adcPattern;
  digConfig.sample_freq_hz = ADC_SAMPLE_FREQ_HZ;
  digConfig.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  digConfig.format = ADC_OUTPUT_TYPE;

  ESP_ERROR_CHECK(adc_continuous_config(adcHandle, &digConfig));
  ESP_ERROR_CHECK(adc_continuous_start(adcHandle));
}

void setup() {
  Serial.setTxBufferSize(2048);
  Serial.begin(921600);

  pinMode(greenLED, OUTPUT);
  pinMode(redLED, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  delay(10);
  buttonState = digitalRead(BUTTON_PIN);
  lastButtonState = buttonState;

  // Initialize packet framing header [0xAA][0x55]
  packet[0] = 0xAA;
  packet[1] = 0x55;
  packetIndex = 0;

  // Initialize and start Hardware ADC Continuous DMA Mode
  initADCContinuous();
}

void loop() {
  // --- Button Edge Detection with 200ms Lockout ---
  int reading = digitalRead(BUTTON_PIN);

  if (reading == LOW && buttonState == HIGH && (millis() - lastDebounceTime > 200)) {
    running = !running;
    buttonState = LOW;
    lastDebounceTime = millis();
  } else if (reading == HIGH && buttonState == LOW && (millis() - lastDebounceTime > 200)) {
    buttonState = HIGH;
    lastDebounceTime = millis();
  }

  // Paused state behavior
  if (!running) {
    digitalWrite(greenLED, LOW);
    digitalWrite(redLED, LOW);
    // Drain DMA buffer during pause so it doesn't accumulate stale readings
    uint32_t discardBytes = 0;
    adc_continuous_read(adcHandle, dmaBuffer, sizeof(dmaBuffer), &discardBytes, 10);
    return;
  }

  // --- Read Continuous ADC Samples directly from DMA RAM ---
  uint32_t bytesRead = 0;
  esp_err_t ret = adc_continuous_read(adcHandle, dmaBuffer, sizeof(dmaBuffer), &bytesRead, 20);

  if (ret == ESP_OK && bytesRead > 0) {
    static int val1 = -1;
    static int val2 = -1;
    static int nibbleCount = 0;
    static uint8_t currentByte = 0;

    for (int i = 0; i < bytesRead; i += sizeof(adc_digi_output_data_t)) {
      adc_digi_output_data_t *p = (adc_digi_output_data_t *)&dmaBuffer[i];
      uint32_t channel = ADC_GET_CHANNEL(p);
      uint32_t data = ADC_GET_DATA(p);

      if (channel == ADC_CHANNEL_6) {
        val1 = data;
      } else if (channel == ADC_CHANNEL_7) {
        val2 = data;
      }

      // When we have a matched pair of readings from both photodiodes:
      if (val1 >= 0 && val2 >= 0) {
        // Extract 4-bit noise nibble from differential shot noise
        uint8_t noiseNibble = (uint8_t)((val1 ^ val2) & 0x0F);
        val1 = -1;
        val2 = -1;

        if (nibbleCount == 0) {
          currentByte = noiseNibble;
          nibbleCount = 1;
        } else {
          currentByte = (noiseNibble << 4) | currentByte;
          nibbleCount = 0;

          // Place byte in transmission packet
          packet[2 + packetIndex] = currentByte;
          lastEntropyByte = currentByte;
          packetIndex++;

          // When full 64-byte payload is ready, transmit frame
          if (packetIndex >= 64) {
            Serial.write(packet, 66);
            packetIndex = 0;
          }
        }
      }
    }
  }

  // LED visualization (taps stream with zero extra ADC reads)
  if (millis() - lastLEDTime >= ledDelay) {
    lastLEDTime = millis();
    int bit = lastEntropyByte & 1;
    if (bit == 1) {
      digitalWrite(greenLED, HIGH);
      digitalWrite(redLED, LOW);
    } else {
      digitalWrite(greenLED, LOW);
      digitalWrite(redLED, HIGH);
    }
  }
}


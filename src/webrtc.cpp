#include <driver/gpio.h>
#include <driver/i2s.h>
#include <esp_event.h>
#include <esp_log.h>
#include <opus.h>
#include <string.h>

#include "display.h"
#include "main.h"
#include "wakeword.h"

#define USER_BUTTON GPIO_NUM_41
#define DEBOUNCE_MS 300

#define TICK_INTERVAL 15

PeerConnection *peer_connection = NULL;
static volatile bool webrtc_running = false;
static volatile bool button_pressed = false;

static TickType_t last_button_time = 0;
static StaticTask_t task_buffer;
static StackType_t *audio_task_stack = NULL;
static TaskHandle_t audio_task_handle = NULL;

void oai_send_audio_task(void *user_data) {
  oai_init_audio_encoder();

  while (webrtc_running) {
    oai_send_audio(peer_connection);
    vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
  }

  ESP_LOGI(LOG_TAG, "Audio send task stopped");
  audio_task_handle = NULL;
  vTaskDelete(NULL);
}

static void oai_onconnectionstatechange_task(PeerConnectionState state,
                                             void *user_data) {
  ESP_LOGI(LOG_TAG, "PeerConnectionState: %s",
           peer_connection_state_to_string(state));

  if (state == PEER_CONNECTION_DISCONNECTED ||
      state == PEER_CONNECTION_CLOSED) {
    ESP_LOGI(LOG_TAG, "WebRTC disconnected");
  } else if (state == PEER_CONNECTION_CONNECTED) {
    oai_display_set_state(DISPLAY_STATE_CONNECTED);
#if CONFIG_OPENAI_BOARD_ESP32_S3
    audio_task_stack = (StackType_t *)heap_caps_malloc(
        20000 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    audio_task_handle = xTaskCreateStaticPinnedToCore(oai_send_audio_task, "audio_publisher", 20000,
                                  NULL, 7, audio_task_stack, &task_buffer, 0);
#elif CONFIG_OPENAI_BOARD_M5_ATOMS3R
    // Because we change the sampling rate to 16K, so we need increased the
    // memory size, if not will overflow :)
    audio_task_stack = (StackType_t *)heap_caps_malloc(
        40000 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    audio_task_handle = xTaskCreateStaticPinnedToCore(oai_send_audio_task, "audio_publisher", 40000,
                                  NULL, 7, audio_task_stack, &task_buffer, 0);
#endif
  }
}

static void oai_on_icecandidate_task(char *description, void *user_data) {
  char local_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};
  oai_http_request(description, local_buffer);
  peer_connection_set_remote_description(peer_connection, local_buffer);
}

void oai_webrtc() {
  PeerConfiguration peer_connection_config = {
      .ice_servers = {},
      .audio_codec = CODEC_OPUS,
      .video_codec = CODEC_NONE,
      .datachannel = DATA_CHANNEL_NONE,
      .onaudiotrack = [](uint8_t *data, size_t size, void *userdata) -> void {
        oai_audio_decode(data, size);
      },
      .onvideotrack = NULL,
      .on_request_keyframe = NULL,
      .user_data = NULL,
  };

  peer_connection = peer_connection_create(&peer_connection_config);
  if (peer_connection == NULL) {
    ESP_LOGE(LOG_TAG, "Failed to create peer connection");
    esp_restart();
  }

  peer_connection_oniceconnectionstatechange(peer_connection,
                                             oai_onconnectionstatechange_task);
  peer_connection_onicecandidate(peer_connection, oai_on_icecandidate_task);
  peer_connection_create_offer(peer_connection);

  webrtc_running = true;
  button_pressed = false;

  while (webrtc_running) {
    if (button_pressed) {
      button_pressed = false;
      ESP_LOGI(LOG_TAG, "Button pressed: stopping WebRTC");
      webrtc_running = false;
      break;
    }
    peer_connection_loop(peer_connection);
    vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
  }

  // Wait for audio task to finish
  while (audio_task_handle != NULL) {
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  // Free audio task stack
  if (audio_task_stack != NULL) {
    heap_caps_free(audio_task_stack);
    audio_task_stack = NULL;
  }

  // Close and destroy peer connection
  peer_connection_close(peer_connection);
  peer_connection_destroy(peer_connection);
  peer_connection = NULL;

  ESP_LOGI(LOG_TAG, "WebRTC connection stopped");
  oai_display_set_state(DISPLAY_STATE_DISCONNECTED);
}

static void IRAM_ATTR button_isr_handler(void *arg) {
  TickType_t now = xTaskGetTickCountFromISR();
  if ((now - last_button_time) > pdMS_TO_TICKS(DEBOUNCE_MS)) {
    last_button_time = now;
    button_pressed = true;
  }
}

void oai_init_button(void) {
  gpio_config_t io_conf = {};
  io_conf.pin_bit_mask = (1ULL << USER_BUTTON);
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  io_conf.intr_type = GPIO_INTR_NEGEDGE;
  gpio_config(&io_conf);

  gpio_install_isr_service(0);
  gpio_isr_handler_add(USER_BUTTON, button_isr_handler, NULL);

  ESP_LOGI(LOG_TAG, "Button initialized on GPIO%d", USER_BUTTON);
}

void oai_webrtc_loop(void *user_data) {
  while (1) {
    // Enter listening mode - wait for wake word or button press
    oai_display_set_state(DISPLAY_STATE_LISTENING);
    oai_wakeword_clear();
    oai_wakeword_start();

    ESP_LOGI(LOG_TAG, "Listening for wake word 'Hi ESP' or button press...");
    button_pressed = false;

    // Wait for wake word detection or button press
    while (!oai_wakeword_detected() && !button_pressed) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Stop wake word detection
    oai_wakeword_stop();

    if (oai_wakeword_detected()) {
      ESP_LOGI(LOG_TAG, "Wake word detected! Connecting to WebRTC...");
    } else {
      ESP_LOGI(LOG_TAG, "Button pressed! Connecting to WebRTC...");
    }
    button_pressed = false;

    // Connect to WebRTC
    oai_display_set_state(DISPLAY_STATE_INITIALIZING);
    oai_webrtc();

    // oai_webrtc() returns when disconnected (button press during connection)
    ESP_LOGI(LOG_TAG, "WebRTC session ended, returning to listening mode...");
  }
}

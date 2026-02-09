#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2s.h>
#include <string.h>

#include "wakeword.h"
#include "main.h"

#if CONFIG_OPENAI_BOARD_M5_ATOMS3R

#include "esp_wn_iface.h"
#include "esp_wn_models.h"

#define WAKEWORD_TASK_STACK_SIZE 8192
#define I2S_PORT I2S_NUM_1

static const esp_wn_iface_t *wakenet = NULL;
static model_iface_data_t *model_data = NULL;
static TaskHandle_t wakeword_task_handle = NULL;
static volatile bool wakeword_running = false;
static volatile bool wake_detected = false;

static int16_t *audio_buffer = NULL;
static int audio_chunksize = 0;

static void wakeword_task(void *arg) {
    size_t bytes_read = 0;

    ESP_LOGI(LOG_TAG, "Wake word detection task started");

    while (wakeword_running) {
        // Read audio from I2S
        esp_err_t ret = i2s_read(I2S_PORT, audio_buffer,
                                  audio_chunksize * sizeof(int16_t),
                                  &bytes_read, portMAX_DELAY);

        if (ret != ESP_OK || bytes_read == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Run wake word detection
        wakenet_state_t state = wakenet->detect(model_data, audio_buffer);

        if (state == WAKENET_DETECTED) {
            wake_detected = true;
            ESP_LOGI(LOG_TAG, "Wake word detected!");
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(LOG_TAG, "Wake word detection task stopped");
    wakeword_task_handle = NULL;
    vTaskDelete(NULL);
}

int oai_init_wakeword(void) {
    // Get WakeNet handle
    wakenet = esp_wn_handle_from_name(WAKENET_MODEL_NAME);
    if (wakenet == NULL) {
        ESP_LOGE(LOG_TAG, "Failed to get WakeNet handle for model: %s", WAKENET_MODEL_NAME);
        return -1;
    }

    // Create model instance
    model_data = wakenet->create(WAKENET_MODEL_NAME, DET_MODE_90);
    if (model_data == NULL) {
        ESP_LOGE(LOG_TAG, "Failed to create WakeNet model");
        return -1;
    }

    // Get audio parameters
    audio_chunksize = wakenet->get_samp_chunksize(model_data);
    int sample_rate = wakenet->get_samp_rate(model_data);

    ESP_LOGI(LOG_TAG, "WakeNet initialized: chunk_size=%d, sample_rate=%d",
             audio_chunksize, sample_rate);

    // Allocate audio buffer
    audio_buffer = (int16_t *)heap_caps_malloc(audio_chunksize * sizeof(int16_t),
                                                MALLOC_CAP_SPIRAM);
    if (audio_buffer == NULL) {
        ESP_LOGE(LOG_TAG, "Failed to allocate audio buffer");
        wakenet->destroy(model_data);
        return -1;
    }

    // Log available wake words
    int word_num = wakenet->get_word_num(model_data);
    for (int i = 1; i <= word_num; i++) {
        char *word_name = wakenet->get_word_name(model_data, i);
        ESP_LOGI(LOG_TAG, "Wake word %d: %s", i, word_name);
    }

    return 0;
}

void oai_wakeword_start(void) {
    if (wakeword_task_handle != NULL) {
        ESP_LOGW(LOG_TAG, "Wake word task already running");
        return;
    }

    wakeword_running = true;
    wake_detected = false;

    xTaskCreatePinnedToCore(wakeword_task, "wakeword", WAKEWORD_TASK_STACK_SIZE,
                            NULL, 5, &wakeword_task_handle, 1);
}

void oai_wakeword_stop(void) {
    wakeword_running = false;

    // Wait for task to finish
    while (wakeword_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

bool oai_wakeword_detected(void) {
    return wake_detected;
}

void oai_wakeword_clear(void) {
    wake_detected = false;
}

#else

// Stub implementations for non-AtomS3R boards
int oai_init_wakeword(void) { return -1; }
void oai_wakeword_start(void) {}
void oai_wakeword_stop(void) {}
bool oai_wakeword_detected(void) { return false; }
void oai_wakeword_clear(void) {}

#endif

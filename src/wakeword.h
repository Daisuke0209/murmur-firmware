#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize wake word detection
 * @return 0 on success, -1 on failure
 */
int oai_init_wakeword(void);

/**
 * @brief Start wake word detection task
 */
void oai_wakeword_start(void);

/**
 * @brief Stop wake word detection task
 */
void oai_wakeword_stop(void);

/**
 * @brief Check if wake word was detected (non-blocking)
 * @return true if wake word detected, false otherwise
 */
bool oai_wakeword_detected(void);

/**
 * @brief Clear wake word detection flag
 */
void oai_wakeword_clear(void);

#ifdef __cplusplus
}
#endif

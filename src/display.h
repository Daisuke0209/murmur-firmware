#pragma once

typedef enum {
    DISPLAY_STATE_LISTENING,     // Waiting for wake word
    DISPLAY_STATE_INITIALIZING,  // Connecting
    DISPLAY_STATE_CONNECTED,     // Connected
} DisplayState;

void oai_init_display(void);
void oai_display_set_state(DisplayState state);

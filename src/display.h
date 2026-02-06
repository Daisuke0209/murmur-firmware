#pragma once

typedef enum {
    DISPLAY_STATE_INITIALIZING,  // Blue
    DISPLAY_STATE_CONNECTED,     // Green
    DISPLAY_STATE_DISCONNECTED,  // Red
} DisplayState;

void oai_init_display(void);
void oai_display_set_state(DisplayState state);

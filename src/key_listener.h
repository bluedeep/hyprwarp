/*
 * key_listener.h - Key listener mode after hint selection
 * 
 * After a hint is selected, enters a key listening mode
 * that logs key presses for further processing.
 */

#ifndef KEY_LISTENER_H
#define KEY_LISTENER_H

#include "config.h"

/* Initialize key listener mode */
void key_listener_init(void);

/* Check if we are in key listener mode */
int key_listener_is_active(void);

/* Activate key listener mode */
void key_listener_activate(int x, int y);

/* Deactivate key listener mode */
void key_listener_deactivate(void);

/* Handle a key press in listener mode, returns 1 if handled, 0 otherwise */
int key_listener_handle_key(const char *key_name, const struct hyprwarp_config *cfg);

#endif /* KEY_LISTENER_H */
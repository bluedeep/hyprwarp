/*
 * key_listener.c - Key listener mode after hint selection
 */

#include <stdio.h>
#include <string.h>

#include "key_listener.h"

/* Key listener state */
static int listener_active = 0;
static int listener_x = 0;
static int listener_y = 0;

void key_listener_init(void)
{
    listener_active = 0;
    listener_x = 0;
    listener_y = 0;
}

int key_listener_is_active(void)
{
    return listener_active;
}

void key_listener_activate(int x, int y)
{
    listener_active = 1;
    listener_x = x;
    listener_y = y;
    fprintf(stderr, "[key_listener] Activated at position (%d, %d)\n", x, y);
}

void key_listener_deactivate(void)
{
    fprintf(stderr, "[key_listener] Deactivated\n");
    listener_active = 0;
}

int key_listener_handle_key(const char *key_name, const struct hyprwarp_config *cfg)
{
    if (!listener_active) return 0;
    
    fprintf(stderr, "[key_listener] Key pressed: %s\n", key_name);
    
    if (strcmp(key_name, cfg->move_up_key) == 0) {
        fprintf(stderr, "[key_listener]   -> configured as: move_up_key\n");
    } else if (strcmp(key_name, cfg->move_down_key) == 0) {
        fprintf(stderr, "[key_listener]   -> configured as: move_down_key\n");
    } else if (strcmp(key_name, cfg->move_left_key) == 0) {
        fprintf(stderr, "[key_listener]   -> configured as: move_left_key\n");
    } else if (strcmp(key_name, cfg->move_right_key) == 0) {
        fprintf(stderr, "[key_listener]   -> configured as: move_right_key\n");
    } else if (strcmp(key_name, cfg->click_left_key) == 0) {
        fprintf(stderr, "[key_listener]   -> configured as: click_left_key\n");
    } else if (strcmp(key_name, cfg->click_middle_key) == 0) {
        fprintf(stderr, "[key_listener]   -> configured as: click_middle_key\n");
    } else if (strcmp(key_name, cfg->click_right_key) == 0) {
        fprintf(stderr, "[key_listener]   -> configured as: click_right_key\n");
    } else if (strcmp(key_name, cfg->scroll_up_key) == 0) {
        fprintf(stderr, "[key_listener]   -> configured as: scroll_up_key\n");
    } else if (strcmp(key_name, cfg->scroll_down_key) == 0) {
        fprintf(stderr, "[key_listener]   -> configured as: scroll_down_key\n");
    }
    
    return 1;
}
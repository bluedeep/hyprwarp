/*
 * config.h - Configuration management
 * 
 * Handles loading and saving configuration from/to file.
 */

#ifndef CONFIG_H
#define CONFIG_H

/* Configuration structure */
struct hyprwarp_config {
    char bgcolor[16];
    char fgcolor[16];
    int hint_size;
    int hint_radius;
    char hint_chars[32];
    char exit_key[16];
    char move_up_key[16];
    char move_down_key[16];
    char move_left_key[16];
    char move_right_key[16];
    char click_left_key[16];
    char click_middle_key[16];
    char click_right_key[16];
    char scroll_up_key[16];
    char scroll_down_key[16];
    char on_select_cmd[256];
    char on_exit_cmd[256];
};

/* Default configuration values */
extern const struct hyprwarp_config default_config;

/* Load configuration from file, returns 0 on success */
int load_config(struct hyprwarp_config *cfg);

/* Save default configuration to file, returns 0 on success */
int save_default_config(const char *path);

#endif /* CONFIG_H */
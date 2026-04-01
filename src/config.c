/*
 * config.c - Configuration loading and saving
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"

/* Default configuration values */
const struct hyprwarp_config default_config = {
    .bgcolor = "#ff555560",
    .fgcolor = "#ffffffff",
    .hint_size = 18,
    .hint_radius = 25,
    .hint_chars = "asdfghjklqwertzxv",
    .exit_key = "Escape",
    .move_up_key = "k",
    .move_down_key = "j",
    .move_left_key = "h",
    .move_right_key = "l",
    .click_left_key = "space",
    .click_middle_key = "w",
    .click_right_key = "e",
    .scroll_up_key = "bracketleft",
    .scroll_down_key = "bracketright",
    .on_select_cmd = "hyprctl dispatch movecursor {global_x} {global_y}",
    .on_exit_cmd = "hyprctl notify 2 3600000 \"rgb(ff0000)\" \"ON MOUSE MODE\"; hyprctl keyword cursor:inactive_timeout 0; hyprctl keyword cursor:hide_on_key_press false; hyprctl dispatch submap cursor"
};

int save_default_config(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    
    fprintf(f, "# hyprwarp config\n");
    fprintf(f, "hint_bgcolor=%s\n", default_config.bgcolor);
    fprintf(f, "hint_fgcolor=%s\n", default_config.fgcolor);
    fprintf(f, "hint_size=%d\n", default_config.hint_size);
    fprintf(f, "hint_radius=%d\n", default_config.hint_radius);
    fprintf(f, "hint_chars=%s\n", default_config.hint_chars);
    fprintf(f, "exit_key=%s\n", default_config.exit_key);
    fprintf(f, "# Mouse movement keys (after hint selection)\n");
    fprintf(f, "move_up_key=%s\n", default_config.move_up_key);
    fprintf(f, "move_down_key=%s\n", default_config.move_down_key);
    fprintf(f, "move_left_key=%s\n", default_config.move_left_key);
    fprintf(f, "move_right_key=%s\n", default_config.move_right_key);
    fprintf(f, "# Mouse click keys\n");
    fprintf(f, "click_left_key=%s\n", default_config.click_left_key);
    fprintf(f, "click_middle_key=%s\n", default_config.click_middle_key);
    fprintf(f, "click_right_key=%s\n", default_config.click_right_key);
    fprintf(f, "# Scroll wheel keys\n");
    fprintf(f, "scroll_up_key=%s\n", default_config.scroll_up_key);
    fprintf(f, "scroll_down_key=%s\n", default_config.scroll_down_key);
    fprintf(f, "on_select_cmd=%s\n", default_config.on_select_cmd);
    fprintf(f, "on_exit_cmd=%s\n", default_config.on_exit_cmd);
    
    fclose(f);
    return 0;
}

int load_config(struct hyprwarp_config *cfg)
{
    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%s/.config/hyprwarp/config", getenv("HOME"));
    
    FILE *f = fopen(config_path, "r");
    if (!f) {
        char dir_path[512];
        snprintf(dir_path, sizeof(dir_path), "%s/.config/hyprwarp", getenv("HOME"));
        mkdir(dir_path, 0755);
        
        if (save_default_config(config_path) < 0) {
            *cfg = default_config;
            return 0;
        }
        
        memcpy(cfg, &default_config, sizeof(*cfg));
        return 0;
    }
    
    *cfg = default_config;
    
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        line[strcspn(line, "\n")] = 0;
        
        char *eq = strchr(line, '=');
        if (!eq) continue;
        
        *eq = '\0';
        char *key = line;
        char *value = eq + 1;
        
        if (strcmp(key, "hint_bgcolor") == 0) {
            strncpy(cfg->bgcolor, value, sizeof(cfg->bgcolor) - 1);
        } else if (strcmp(key, "hint_fgcolor") == 0) {
            strncpy(cfg->fgcolor, value, sizeof(cfg->fgcolor) - 1);
        } else if (strcmp(key, "hint_size") == 0) {
            cfg->hint_size = atoi(value);
            if (cfg->hint_size < 8) cfg->hint_size = 8;
            if (cfg->hint_size > 64) cfg->hint_size = 64;
        } else if (strcmp(key, "hint_radius") == 0) {
            cfg->hint_radius = atoi(value);
            if (cfg->hint_radius < 0) cfg->hint_radius = 0;
            if (cfg->hint_radius > 100) cfg->hint_radius = 100;
        } else if (strcmp(key, "hint_chars") == 0) {
            strncpy(cfg->hint_chars, value, sizeof(cfg->hint_chars) - 1);
        } else if (strcmp(key, "exit_key") == 0) {
            strncpy(cfg->exit_key, value, sizeof(cfg->exit_key) - 1);
        } else if (strcmp(key, "move_up_key") == 0) {
            strncpy(cfg->move_up_key, value, sizeof(cfg->move_up_key) - 1);
        } else if (strcmp(key, "move_down_key") == 0) {
            strncpy(cfg->move_down_key, value, sizeof(cfg->move_down_key) - 1);
        } else if (strcmp(key, "move_left_key") == 0) {
            strncpy(cfg->move_left_key, value, sizeof(cfg->move_left_key) - 1);
        } else if (strcmp(key, "move_right_key") == 0) {
            strncpy(cfg->move_right_key, value, sizeof(cfg->move_right_key) - 1);
        } else if (strcmp(key, "click_left_key") == 0) {
            strncpy(cfg->click_left_key, value, sizeof(cfg->click_left_key) - 1);
        } else if (strcmp(key, "click_middle_key") == 0) {
            strncpy(cfg->click_middle_key, value, sizeof(cfg->click_middle_key) - 1);
        } else if (strcmp(key, "click_right_key") == 0) {
            strncpy(cfg->click_right_key, value, sizeof(cfg->click_right_key) - 1);
        } else if (strcmp(key, "scroll_up_key") == 0) {
            strncpy(cfg->scroll_up_key, value, sizeof(cfg->scroll_up_key) - 1);
        } else if (strcmp(key, "scroll_down_key") == 0) {
            strncpy(cfg->scroll_down_key, value, sizeof(cfg->scroll_down_key) - 1);
        } else if (strcmp(key, "on_select_cmd") == 0) {
            strncpy(cfg->on_select_cmd, value, sizeof(cfg->on_select_cmd) - 1);
        } else if (strcmp(key, "on_exit_cmd") == 0) {
            strncpy(cfg->on_exit_cmd, value, sizeof(cfg->on_exit_cmd) - 1);
        }
    }
    fclose(f);
    return 0;
}
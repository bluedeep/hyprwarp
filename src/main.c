/*
 * hyprwarp - A hint mode implementation for Hyprland
 * 
 * This application displays a grid of hint tags on the screen, allowing users
 * to quickly move the mouse cursor to a specific location by typing the
 * corresponding two-character label.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <math.h>

#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include <xkbcommon/xkbcommon.h>
#include <cairo/cairo.h>

#include "layer-shell.h"
#include "xdg-output.h"
#include "virtual-pointer.h"

/* Maximum number of hint tags (supports up to 32x32 grid) */
#define MAX_HINTS 1024

/* Single hint tag structure */
struct hint {
    int x, y;           /* Position on screen */
    int w, h;           /* Width and height */
    char label[8];      /* Two-character label (e.g., "as", "df") */
};

/* Screen/output information from Wayland */
struct screen_info {
    int x, y;                           /* Logical position */
    int w, h;                           /* Width and height in pixels */
    struct wl_output *wl_output;        /* Wayland output object */
    struct wl_surface *wl_surface;      /* Wayland surface for rendering */
    struct zwlr_layer_surface_v1 *layer_surface;  /* Layer shell surface */
    struct wl_shm_pool *wl_pool;        /* Shared memory pool for buffer */
    size_t stride;                      /* Buffer stride (bytes per row) */
    void *data;                         /* Pointer to shared memory data */
};

/* Wayland global objects */
struct wl_info {
    struct wl_display *dpy;                     /* Wayland display connection */
    struct wl_shm *shm;                         /* Shared memory manager */
    struct wl_seat *seat;                       /* Input seat */
    struct wl_compositor *compositor;           /* Compositor */
    struct zwlr_virtual_pointer_v1 *ptr;        /* Virtual pointer for mouse movement */
    struct zwlr_layer_shell_v1 *layer_shell;    /* Layer shell for overlay */
    struct zxdg_output_manager_v1 *xdg_output_manager;  /* XDG output manager */
    struct wl_registry *registry;               /* Global registry */
};

/* Wayland global state */
static struct wl_info wl = {0};
static struct screen_info screen = {0};
static struct xkb_context *xkb_ctx = NULL;
static struct xkb_state *xkb_state = NULL;

/* Configuration variables (loaded from config file) */
static char bgcolor[16] = "#ff555560";      /* Hint tag background color (ARGB hex) */
static char fgcolor[16] = "#ffffffff";      /* Hint tag foreground/text color */
static const char *font_family = "monospace";  /* Font family for hint labels */
static int hint_size = 18;                  /* Font size in pixels */
static int hint_radius = 25;                /* Corner radius as percentage of height */
static char hint_chars[32] = "asdfghjklqwertzxv";  /* Characters used for labels */
static char on_select_cmd[256] = {0};       /* Command to execute when selecting a hint */
static char on_exit_cmd[256] = {0};         /* Command to execute after selection */

/* Hint tag arrays */
static struct hint hints[MAX_HINTS];        /* All generated hints */
static struct hint matched[MAX_HINTS];      /* Hints matching current input */
static size_t nr_hints = 0;                 /* Total number of hints */
static size_t nr_matched = 0;               /* Number of matching hints */

/* Input and state */
static char input_buf[32] = {0};            /* Current keyboard input buffer */
static struct wl_buffer *global_buffer;     /* Current Wayland buffer */
static int layer_configured = 0;            /* Flag: layer surface configured */

/* Selection state */
static int running = 1;                     /* Main loop flag */
static int selected_x = 0;                  /* Selected X coordinate */
static int selected_y = 0;                  /* Selected Y coordinate */
static int selection_made = 0;              /* Flag: user made a selection */

static void load_config(void)
{
    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%s/.config/hyprwarp/config", getenv("HOME"));
    
    FILE *f = fopen(config_path, "r");
    if (!f) {
        char dir_path[512];
        snprintf(dir_path, sizeof(dir_path), "%s/.config/hyprwarp", getenv("HOME"));
        mkdir(dir_path, 0755);
        
        f = fopen(config_path, "w");
        if (!f) return;
        
        fprintf(f, "# hyprwarp config\n");
        fprintf(f, "hint_bgcolor=#ff555560\n");
        fprintf(f, "hint_fgcolor=#ffffffff\n");
        fprintf(f, "hint_size=18\n");
        fprintf(f, "hint_radius=25\n");
        fprintf(f, "hint_chars=asdfghjklqwertzxv\n");
        fprintf(f, "on_select_cmd=echo mouseto {scale_x} {scale_y} | dotool\n");
        fprintf(f, "on_exit_cmd=hyprctl notify 2 3600000 \"rgb(ff0000)\" \"ON MOUSE MODE\"; hyprctl keyword cursor:inactive_timeout 0; hyprctl keyword cursor:hide_on_key_press false; hyprctl dispatch submap cursor");
        fclose(f);
        strncpy(on_select_cmd, "echo mouseto {scale_x} {scale_y} | dotool", sizeof(on_select_cmd) - 1);
        strncpy(on_exit_cmd, "hyprctl notify 2 3600000 \"rgb(ff0000)\" \"ON MOUSE MODE\"; hyprctl keyword cursor:inactive_timeout 0; hyprctl keyword cursor:hide_on_key_press false; hyprctl dispatch submap cursor", sizeof(on_exit_cmd) -1);
        return;
    }
    
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        line[strcspn(line, "\n")] = 0;
        
        char *eq = strchr(line, '=');
        if (!eq) continue;
        
        *eq = '\0';
        char *key = line;
        char *value = eq + 1;
        
        if (strcmp(key, "hint_bgcolor") == 0) {
            strncpy(bgcolor, value, sizeof(bgcolor) - 1);
        } else if (strcmp(key, "hint_fgcolor") == 0) {
            strncpy(fgcolor, value, sizeof(fgcolor) - 1);
        } else if (strcmp(key, "hint_size") == 0) {
            hint_size = atoi(value);
            if (hint_size < 8) hint_size = 8;
            if (hint_size > 64) hint_size = 64;
        } else if (strcmp(key, "hint_radius") == 0) {
            hint_radius = atoi(value);
            if (hint_radius < 0) hint_radius = 0;
            if (hint_radius > 100) hint_radius = 100;
        } else if (strcmp(key, "hint_chars") == 0) {
            strncpy(hint_chars, value, sizeof(hint_chars) - 1);
        } else if (strcmp(key, "on_select_cmd") == 0) {
            strncpy(on_select_cmd, value, sizeof(on_select_cmd) - 1);
        } else if (strcmp(key, "on_exit_cmd") == 0) {
            strncpy(on_exit_cmd, value, sizeof(on_exit_cmd) - 1);
        }
    }
    fclose(f);
}

static void hex_to_rgba(const char *hex, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    int len = strlen(hex);
    if (len == 7 || len == 9) {
        sscanf(hex+1, "%2hhx%2hhx%2hhx", r, g, b);
        *a = len == 9 ? (uint8_t)strtol(hex+7, NULL, 16) : 255;
    } else {
        *r = *g = *b = 0;
        *a = 255;
    }
}

static void draw_text(cairo_t *cr, const char *s, int x, int y, int w, int h)
{
    cairo_text_extents_t extents;

    cairo_select_font_face(cr, font_family, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, hint_size);
    cairo_text_extents(cr, s, &extents);

    cairo_move_to(cr, x + (w - extents.width) / 2, y - extents.y_bearing + (h - extents.height) / 2);
    cairo_show_text(cr, s);
}

static void render_hints(void)
{
    uint8_t r, g, b, a;
    cairo_t *cr;

    if (!screen.data)
        return;

    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        (unsigned char *)screen.data, CAIRO_FORMAT_ARGB32,
        screen.w, screen.h, screen.stride);

    cr = cairo_create(surface);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);

    for (size_t i = 0; i < nr_matched; i++) {
        hex_to_rgba(bgcolor, &r, &g, &b, &a);
        cairo_set_source_rgba(cr, r/255.0, g/255.0, b/255.0, a/255.0);
        
        int radius = matched[i].h * hint_radius / 100;
        int x = matched[i].x;
        int y = matched[i].y;
        int w = matched[i].w;
        int h = matched[i].h;
        
        if (radius > 0) {
        
        cairo_move_to(cr, x + radius, y);
        cairo_arc(cr, x + w - radius, y + radius, radius, -M_PI_2, 0);
        cairo_arc(cr, x + w - radius, y + h - radius, radius, 0, M_PI_2);
        cairo_arc(cr, x + radius, y + h - radius, radius, M_PI_2, M_PI);
        cairo_arc(cr, x + radius, y + radius, radius, M_PI, 3 * M_PI_2);
        cairo_close_path(cr);
        cairo_fill(cr);
        } else {
            cairo_rectangle(cr, x, y, w, h);
            cairo_fill(cr);
        }
        hex_to_rgba(fgcolor, &r, &g, &b, &a);
        cairo_set_source_rgba(cr, r/255.0, g/255.0, b/255.0, a/255.0);

        draw_text(cr, matched[i].label, matched[i].x, matched[i].y, matched[i].w, matched[i].h);
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    /* Commit buffer when rendering */
    if (screen.wl_surface && global_buffer) {
        wl_buffer_destroy(global_buffer);
        global_buffer = wl_shm_pool_create_buffer(screen.wl_pool, 0,
                                                  screen.w, screen.h,
                                                  screen.stride,
                                                  WL_SHM_FORMAT_ARGB8888);
        if (global_buffer) {
            wl_surface_attach(screen.wl_surface, global_buffer, 0, 0);
            wl_surface_damage(screen.wl_surface, 0, 0, screen.w, screen.h);
            wl_surface_commit(screen.wl_surface);
        }
    }
}

static void filter_hints(void)
{
    nr_matched = 0;
    for (size_t i = 0; i < nr_hints; i++) {
        if (strncmp(hints[i].label, input_buf, strlen(input_buf)) == 0) {
            matched[nr_matched++] = hints[i];
        }
    }
}

static void generate_hints(void)
{
    char unique_chars[32] = {0};
    int unique_len = 0;
    
    for (int i = 0; i < (int)strlen(hint_chars); i++) {
        int found = 0;
        for (int j = 0; j < unique_len; j++) {
            if (hint_chars[i] == unique_chars[j]) {
                found = 1;
                break;
            }
        }
        if (!found) {
            unique_chars[unique_len++] = hint_chars[i];
        }
    }
    
    int nr = unique_len;
    int nc = unique_len;

    int hint_w = hint_size * 2;
    int hint_h = (int)(hint_size * 1.5);

    int colgap = screen.w / nc - hint_w;
    int rowgap = screen.h / nr - hint_h;

    int x_offset = (screen.w - nc * hint_w - (nc - 1) * colgap) / 2;
    int y_offset = (screen.h - nr * hint_h - (nr - 1) * rowgap) / 2;

    nr_hints = 0;

    for (int i = 0; i < nc; i++) {
        for (int j = 0; j < nr; j++) {
            if (nr_hints >= MAX_HINTS) break;

            struct hint *h = &hints[nr_hints++];
            h->x = x_offset + i * (hint_w + colgap);
            h->y = y_offset + j * (hint_h + rowgap);
            h->w = hint_w;
            h->h = hint_h;
            h->label[0] = unique_chars[i];
            h->label[1] = unique_chars[j];
            h->label[2] = 0;
        }
    }

    filter_hints();
}

static int init_shm(void)
{
    char shm_name[256];
    snprintf(shm_name, sizeof(shm_name), "/hyprwarp-%d", getpid());
    
    int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        fprintf(stderr, "shm_open failed: %s\n", strerror(errno));
        return -1;
    }

    size_t size = screen.w * screen.h * 4;
    if (ftruncate(fd, size) < 0) {
        fprintf(stderr, "ftruncate failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    screen.data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (screen.data == MAP_FAILED) {
        fprintf(stderr, "mmap failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    screen.wl_pool = wl_shm_create_pool(wl.shm, fd, size);
    close(fd);
    shm_unlink(shm_name);

    screen.stride = screen.w * 4;
    return 0;
}

static void output_handle_geometry(void *data, struct wl_output *output,
                                   int32_t x, int32_t y, int32_t physical_width,
                                   int32_t physical_height, int32_t subpixel,
                                   const char *make, const char *model,
                                   int32_t transform)
{
}
static void output_handle_done(void *data, struct wl_output *output) {}
static void output_handle_mode(void *data, struct wl_output *output,
                              uint32_t flags, int width, int height, int refresh)
{
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        struct screen_info *scr = data;
        scr->w = width;
        scr->h = height;
    }
}
static void output_handle_scale(void *data, struct wl_output *output, int32_t factor)
{
}

static const struct wl_output_listener output_listener = {
    .geometry = output_handle_geometry,
    .mode = output_handle_mode,
    .done = output_handle_done,
    .scale = output_handle_scale
};

static void xdg_output_handle_logical_position(void *data, struct zxdg_output_v1 *xdg_output, int32_t x, int32_t y) {
    struct screen_info *scr = data;
    scr->x = x;
    scr->y = y;
}
static void xdg_output_handle_logical_size(void *data, struct zxdg_output_v1 *xdg_output, int32_t w, int32_t h) {
    struct screen_info *scr = data;
    scr->w = w;
    scr->h = h;
}
static void xdg_output_handle_done(void *data, struct zxdg_output_v1 *xdg_output) {}
static void xdg_output_handle_name(void *data, struct zxdg_output_v1 *xdg_output, const char *name) {}
static void xdg_output_handle_description(void *data, struct zxdg_output_v1 *xdg_output, const char *description) {}

static const struct zxdg_output_v1_listener xdg_output_listener = {
    .logical_position = xdg_output_handle_logical_position,
    .logical_size = xdg_output_handle_logical_size,
    .done = xdg_output_handle_done,
    .name = xdg_output_handle_name,
    .description = xdg_output_handle_description,
};

static void global_handler(void *data, struct wl_registry *registry,
                          uint32_t name, const char *interface, uint32_t version)
{
    struct wl_info *wl = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        wl->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        wl->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        wl->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        screen.wl_output = wl_registry_bind(registry, name, &wl_output_interface, 1);
        wl_output_add_listener(screen.wl_output, &output_listener, &screen);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        wl->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
    } else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
        wl->xdg_output_manager = wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, 1);
    } else if (strcmp(interface, zwlr_virtual_pointer_v1_interface.name) == 0) {
        wl->ptr = wl_registry_bind(registry, name, &zwlr_virtual_pointer_v1_interface, 1);
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = global_handler,
    .global_remove = NULL
};

static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
                                   uint32_t format, int fd, uint32_t size)
{
    if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        char *keymap_str = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (keymap_str != MAP_FAILED) {
            struct xkb_keymap *keymap = xkb_keymap_new_from_string(xkb_ctx, keymap_str,
                                                            XKB_KEYMAP_FORMAT_TEXT_V1,
                                                            XKB_KEYMAP_COMPILE_NO_FLAGS);
            if (keymap) {
                xkb_state = xkb_state_new(keymap);
                xkb_keymap_unref(keymap);
            }
            munmap(keymap_str, size);
        }
        close(fd);
    }
}

static void keyboard_handle_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {}
static void keyboard_handle_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface) {}
static void keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {}

static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
                                uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return;

    if (!xkb_state) {
        if (key == 1) { running = 0; }
        else if (key == 14) {
            size_t len = strlen(input_buf);
            if (len > 0) input_buf[len - 1] = 0;
            filter_hints();
            render_hints();
        } else if (key >= 16 && key <= 25) {
            char c = 'a' + (key - 16);
            if (strlen(input_buf) < sizeof(input_buf) - 1) {
                strncat(input_buf, &c, 1);
                filter_hints();

                if (nr_matched == 1) {
                    selected_x = matched[0].x + matched[0].w / 2;
                    selected_y = matched[0].y + matched[0].h / 2;
                    selection_made = 1;
                    running = 0;
                } else if (nr_matched == 0) {
                    size_t len = strlen(input_buf);
                    if (len > 0) input_buf[len - 1] = 0;
                    filter_hints();
                }
                render_hints();
            }
        }
        return;
    }

    xkb_keycode_t keycode = key + 8;
    xkb_keysym_t keysym = xkb_state_key_get_one_sym(xkb_state, keycode);
    char name[64];
    xkb_keysym_get_name(keysym, name, sizeof(name));

    if (strcmp(name, "Escape") == 0) {
        running = 0;
    } else if (strcmp(name, "BackSpace") == 0) {
        size_t len = strlen(input_buf);
        if (len > 0) input_buf[len - 1] = 0;
        filter_hints();
        render_hints();
    } else {
        char utf8[8] = {0};
        int utf8_len = xkb_keysym_to_utf8(keysym, utf8, sizeof(utf8));
        if (utf8_len > 0 && strlen(input_buf) < sizeof(input_buf) - 1) {
            if (strchr(hint_chars, utf8[0]) != NULL) {
                strncat(input_buf, utf8, 1);
                filter_hints();

                if (nr_matched == 1) {
                    selected_x = matched[0].x + matched[0].w / 2;
                    selected_y = matched[0].y + matched[0].h / 2;
                    selection_made = 1;
                    running = 0;
                } else if (nr_matched == 0) {
                    size_t len = strlen(input_buf);
                    if (len > 0) input_buf[len - 1] = 0;
                    filter_hints();
                }
                render_hints();
            }
        }
    }
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_handle_keymap,
    .enter = keyboard_handle_enter,
    .leave = keyboard_handle_leave,
    .key = keyboard_handle_key,
    .modifiers = keyboard_handle_modifiers,
};

static void layer_surface_handle_configure(void *data, struct zwlr_layer_surface_v1 *layer_surface,
                                           uint32_t serial, uint32_t width, uint32_t height)
{
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    layer_configured = 1;
    
    (void)data;
    (void)width;
    (void)height;
}

static void layer_surface_handle_closed(void *data, struct zwlr_layer_surface_v1 *layer_surface)
{
    running = 0;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_handle_configure,
    .closed = layer_surface_handle_closed,
};

static int create_layer_surface(void)
{
    screen.wl_surface = wl_compositor_create_surface(wl.compositor);
    if (!screen.wl_surface) return -1;

    screen.layer_surface = zwlr_layer_shell_v1_get_layer_surface(wl.layer_shell, screen.wl_surface,
                                              screen.wl_output, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
                                              "hyprwarp");
    if (!screen.layer_surface) {
        wl_surface_destroy(screen.wl_surface);
        return -1;
    }

    zwlr_layer_surface_v1_set_size(screen.layer_surface, screen.w, screen.h);
    zwlr_layer_surface_v1_set_anchor(screen.layer_surface,
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(screen.layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(screen.layer_surface, 
                                                     ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    zwlr_layer_surface_v1_add_listener(screen.layer_surface, &layer_surface_listener, NULL);

    wl_surface_commit(screen.wl_surface);
    return 0;
}

static void replace_str(char *str, size_t size, const char *old, const char *replacement)
{
    char buf[512];
    char *p;
    
    while ((p = strstr(str, old)) != NULL) {
        size_t prefix_len = p - str;
        size_t old_len = strlen(old);
        size_t new_len = strlen(replacement);
        size_t suffix_len = strlen(p + old_len);
        
        if (prefix_len + new_len + suffix_len >= size) return;
        
        strncpy(buf, str, prefix_len);
        buf[prefix_len] = '\0';
        strcat(buf, replacement);
        strcat(buf, p + old_len);
        strcpy(str, buf);
    }
}

static void expand_cmd(const char *cmd, char *output, size_t size, int x, int y)
{
    snprintf(output, size, cmd);
    
    char buf[64];
    
    snprintf(buf, sizeof(buf), "%d", screen.w);
    replace_str(output, size, "{screen_w}", buf);
    
    snprintf(buf, sizeof(buf), "%d", screen.h);
    replace_str(output, size, "{screen_h}", buf);
    
    snprintf(buf, sizeof(buf), "%d", x);
    replace_str(output, size, "{x}", buf);
    
    snprintf(buf, sizeof(buf), "%d", y);
    replace_str(output, size, "{y}", buf);
    
    snprintf(buf, sizeof(buf), "%.3f", (double)x / screen.w);
    replace_str(output, size, "{scale_x}", buf);
    
    snprintf(buf, sizeof(buf), "%.3f", (double)y / screen.h);
    replace_str(output, size, "{scale_y}", buf);
}

static void move_mouse(int x, int y)
{
    if (wl.ptr) {
        zwlr_virtual_pointer_v1_motion_absolute(wl.ptr, 0, 
            (uint32_t)x, (uint32_t)y, 
            (uint32_t)screen.w, (uint32_t)screen.h);
        zwlr_virtual_pointer_v1_frame(wl.ptr);
    } else {
        if (on_select_cmd[0] != '\0') {
            char cmd[512];
            expand_cmd(on_select_cmd, cmd, sizeof(cmd), x, y);
            system(cmd);
        } else {
            fprintf(stderr, "Error: on_select_cmd not set. Please set on_select_cmd in the config file.\n");
        }
    }
}

int main(int argc, char **argv)
{
    load_config();
    
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);

    screen.w = 1920;
    screen.h = 1080;

    wl.dpy = wl_display_connect(NULL);
    if (!wl.dpy) return 1;

    wl.registry = wl_display_get_registry(wl.dpy);
    wl_registry_add_listener(wl.registry, &registry_listener, &wl);
    wl_display_roundtrip(wl.dpy);

    if (!wl.compositor || !wl.shm || !wl.layer_shell) return 1;

    if (screen.wl_output && wl.xdg_output_manager) {
        struct zxdg_output_v1 *xdg_output = zxdg_output_manager_v1_get_xdg_output(
            wl.xdg_output_manager, screen.wl_output);
        zxdg_output_v1_add_listener(xdg_output, &xdg_output_listener, &screen);
    }

    wl_display_roundtrip(wl.dpy);

    if (init_shm() < 0) return 1;

    xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkb_ctx) return 1;

    if (create_layer_surface() < 0) return 1;

    global_buffer = wl_shm_pool_create_buffer(screen.wl_pool, 0,
                                              screen.w, screen.h,
                                              screen.stride,
                                              WL_SHM_FORMAT_ARGB8888);
    if (!global_buffer) return 1;

    /* Get keyboard */
    if (wl.seat) {
        struct wl_keyboard *keyboard = wl_seat_get_keyboard(wl.seat);
        if (keyboard) wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
    }

    /* Wait for initial configure */
    while (!layer_configured && wl_display_dispatch(wl.dpy) != -1) {}

    generate_hints();
    render_hints();

    printf("Hint mode started. Type to filter, ESC to cancel.\n");

    while (running && wl_display_dispatch(wl.dpy) != -1) {
        if (layer_configured && screen.wl_surface) {
            layer_configured = 0;
        }
    }

    if (selection_made) {
        nr_matched = 0;
        render_hints();
        wl_display_roundtrip(wl.dpy);
        printf("Selected: (%d, %d)\n", selected_x, selected_y);
        fflush(stdout);
        move_mouse(selected_x, selected_y);
        if (on_exit_cmd[0] != '\0') {
            char cmd[256];
            printf("Running on_exit_cmd: %s\n", on_exit_cmd);
            expand_cmd(on_exit_cmd, cmd, sizeof(cmd), selected_x, selected_y);
            system(cmd);
        }
    } else {
        printf("Cancelled\n");
    }

    if (global_buffer) wl_buffer_destroy(global_buffer);
    if (screen.wl_surface) wl_surface_destroy(screen.wl_surface);
    if (xkb_state) xkb_state_unref(xkb_state);
    if (xkb_ctx) xkb_context_unref(xkb_ctx);
    wl_display_disconnect(wl.dpy);

    return 0;
}

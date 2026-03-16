/*
 * hyprwarp - A hint-based window focus/ mouse warp tool for Wayland
 * 
 * Displays a grid of hint tags on all screens, allowing users to quickly
 * move the mouse cursor by typing the corresponding label.
 * 
 * Multi-screen support: Each screen shows hints with a unique prefix character.
 * Single-screen: Uses two-character labels directly.
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
#include <poll.h>

#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include <xkbcommon/xkbcommon.h>
#include <cairo/cairo.h>

#include "layer-shell.h"
#include "xdg-output.h"
#include "virtual-pointer.h"

#define MAX_HINTS 1024
#define MAX_SCREENS 8

/* Single hint tag structure */
struct hint {
    int x, y;           /* Position on screen */
    int w, h;           /* Width and height */
    char label[8];      /* Label (e.g., "as", "df") */
};

/* Screen/output information from Wayland */
struct screen_info {
    int x, y;                           /* Logical position */
    int w, h;                           /* Width and height in pixels */
    int scale;                          /* Output scale factor */
    struct wl_output *wl_output;       /* Wayland output object */
    struct zxdg_output_v1 *xdg_output;  /* XDG output for logical coords */
    struct wl_surface *wl_surface;     /* Wayland surface for rendering */
    struct zwlr_layer_surface_v1 *layer_surface;  /* Layer shell surface */
    struct wl_shm_pool *wl_pool;       /* Shared memory pool for buffer */
    size_t stride;                      /* Buffer stride (bytes per row) */
    void *data;                        /* Pointer to shared memory data */
    uint32_t output_name;              /* Wayland global name for output */
    int configured;                     /* Whether output is configured */
    struct hint hints[MAX_HINTS];      /* Hints for this screen */
    size_t nr_hints;                    /* Number of hints */
};

/* Wayland global objects */
struct wl_info {
    struct wl_display *dpy;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_compositor *compositor;
    struct zwlr_virtual_pointer_v1 *ptr;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct zxdg_output_manager_v1 *xdg_output_manager;
    struct wl_registry *registry;
};

/* Global state */
static struct wl_info wl = {0};
static struct screen_info screens[MAX_SCREENS];
static int nr_screens = 0;
static struct screen_info *screen = NULL;
static struct xkb_context *xkb_ctx = NULL;
static struct xkb_state *xkb_state = NULL;
static int current_screen_index = -1;
static struct wl_pointer *pointer = NULL;
static int pointer_x = 0;
static int pointer_y = 0;

/* Global surface dimensions (combined area of all screens) */
static int global_surface_w = 0;
static int global_surface_h = 0;
static int global_origin_x = 0;
static int global_origin_y = 0;

/* Configuration (loaded from config file) */
static char bgcolor[16] = "#ff555560";
static char fgcolor[16] = "#ffffffff";
static const char *font_family = "monospace";
static int hint_size = 18;
static int hint_radius = 25;
static char hint_chars[32] = "asdfghjklqwertzxv";
static char on_select_cmd[256] = {0};
static char on_exit_cmd[256] = {0};

/* Hint matching state */
static struct hint matched[MAX_HINTS];
static size_t nr_matched = 0;

/* Input buffer and state */
static char input_buf[32] = {0};
static int layer_configured = 0;
static int running = 1;
static int selected_x = 0;
static int selected_y = 0;
static int selection_made = 0;

/* ============================================================================
 * Configuration Loading
 * ============================================================================ */

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
        fprintf(f, "on_select_cmd=hyprctl dispatch movecursor {global_x} {global_y}\n");
        fprintf(f, "on_exit_cmd=hyprctl notify 2 3600000 \"rgb(ff0000)\" \"ON MOUSE MODE\"; hyprctl keyword cursor:inactive_timeout 0; hyprctl keyword cursor:hide_on_key_press false; hyprctl dispatch submap cursor");
        fclose(f);
        strncpy(on_select_cmd, "hyprctl dispatch movecursor {global_x} {global_y}", sizeof(on_select_cmd) - 1);
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

/* ============================================================================
 * Rendering Functions
 * ============================================================================ */

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

static void render_hints_for_screen(int screen_idx)
{
    if (screen_idx < 0 || screen_idx >= nr_screens) return;
    
    struct screen_info *scr = &screens[screen_idx];
    
    uint8_t r, g, b, a;
    cairo_t *cr;

    if (!scr->data)
        return;

    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        (unsigned char *)scr->data, CAIRO_FORMAT_ARGB32,
        scr->w, scr->h, scr->stride);

    cr = cairo_create(surface);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    
    /* Determine screen prefix (used for multi-screen) */
    char screen_prefix = '\0';
    if (nr_screens > 1 && screen_idx < (int)strlen(hint_chars)) {
        screen_prefix = hint_chars[screen_idx];
    }
    
    /* Multi-screen with prefix input: render only matching screen */
    if (nr_screens > 1 && strlen(input_buf) >= 1) {
        /* Check if first input char matches current screen prefix */
        if (input_buf[0] != screen_prefix) {
            /* No match, clear this screen */
            if (scr->wl_surface && scr->wl_pool) {
                struct wl_buffer *buf = wl_shm_pool_create_buffer(scr->wl_pool, 0,
                                                                  scr->w, scr->h,
                                                                  scr->stride,
                                                                  WL_SHM_FORMAT_ARGB8888);
                if (buf) {
                    wl_surface_attach(scr->wl_surface, buf, 0, 0);
                    wl_surface_damage(scr->wl_surface, 0, 0, scr->w, scr->h);
                    wl_surface_commit(scr->wl_surface);
                    wl_buffer_destroy(buf);
                }
            }
            cairo_destroy(cr);
            cairo_surface_destroy(surface);
            return;
        }
    }
    
    /* Filter and render from current screen's hints */
    size_t local_matched = 0;
    int input_len = strlen(input_buf);
    
    for (size_t i = 0; i < scr->nr_hints; i++) {
        struct hint *h = &scr->hints[i];
        
        /* Check if matches user input */
        int match = 0;
        if (nr_screens > 1) {
            /* Multi-screen: skip screen prefix then match */
            if (input_len <= 1) {
                match = 1;  /* Only prefix input or no input, match all */
            } else {
                /* Match characters after prefix */
                if (strncmp(h->label, input_buf + 1, input_len - 1) == 0) {
                    match = 1;
                }
            }
        } else {
            /* Single screen: direct match */
            if (strncmp(h->label, input_buf, input_len) == 0) {
                match = 1;
            }
        }
        
        if (!match) continue;
        
        local_matched++;
        hex_to_rgba(bgcolor, &r, &g, &b, &a);
        cairo_set_source_rgba(cr, r/255.0, g/255.0, b/255.0, a/255.0);
        
        int radius = h->h * hint_radius / 100;
        int hx= h->x;
        int hy = h->y;
        int hw = h->w;
        int hh = h->h;
        
        if (radius > 0) {
        cairo_move_to(cr, hx + radius, hy);
        cairo_arc(cr, hx + hw - radius, hy + radius, radius, -M_PI_2, 0);
        cairo_arc(cr, hx + hw - radius, hy + hh - radius, radius, 0, M_PI_2);
        cairo_arc(cr, hx + radius, hy + hh - radius, radius, M_PI_2, M_PI);
        cairo_arc(cr, hx + radius, hy + radius, radius, M_PI, 3 * M_PI_2);
        cairo_close_path(cr);
        cairo_fill(cr);
        } else {
            cairo_rectangle(cr, hx, hy, hw, hh);
            cairo_fill(cr);
        }
        hex_to_rgba(fgcolor, &r, &g, &b, &a);
        cairo_set_source_rgba(cr, r/255.0, g/255.0, b/255.0, a/255.0);

        /* Build label with screen prefix */
        char full_label[8];
        if (screen_prefix) {
            full_label[0] = screen_prefix;
            strncpy(full_label + 1, h->label, sizeof(full_label) - 2);
            full_label[sizeof(full_label) - 1] = '\0';
        } else {
            strncpy(full_label, h->label, sizeof(full_label) - 1);
            full_label[sizeof(full_label) - 1] = '\0';
        }
        
        draw_text(cr, full_label, hx, hy, hw, hh);
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    if (scr->wl_surface && scr->wl_pool) {
        struct wl_buffer *buf = wl_shm_pool_create_buffer(scr->wl_pool, 0,
                                                          scr->w, scr->h,
                                                          scr->stride,
                                                          WL_SHM_FORMAT_ARGB8888);
        if (buf) {
            wl_surface_attach(scr->wl_surface, buf, 0, 0);
            wl_surface_damage(scr->wl_surface, 0, 0, scr->w, scr->h);
            wl_surface_commit(scr->wl_surface);
            wl_buffer_destroy(buf);
        }
    }
}

static void render_hints(void)
{
    /* Render all screens */
    for (int i = 0; i < nr_screens; i++) {
        render_hints_for_screen(i);
    }
}

static void filter_hints(void)
{
    nr_matched = 0;
    
    int input_len = strlen(input_buf);
    
    /* Determine which screen's hints to filter from */
    int filter_screen = -1;  /* -1 means all screens */
    if (nr_screens > 1 && input_len >= 1) {
        /* Multi-screen: first char is screen prefix */
        for (int s = 0; s < nr_screens && s < (int)strlen(hint_chars); s++) {
            if (input_buf[0] == hint_chars[s]) {
                filter_screen = s;
                break;
            }
        }
    } else if (nr_screens == 1) {
        filter_screen = 0;
    }
    
    /* Filter from corresponding screen's hints */
    if (filter_screen >= 0) {
        /* Filter from specified screen's hints */
        struct screen_info *scr = &screens[filter_screen];
        for (size_t i = 0; i < scr->nr_hints; i++) {
            const char *label = scr->hints[i].label;
            int label_len = (int)strlen(label);
            
            /* Multi-screen: skip first char (screen prefix), match from second char */
            int compare_len = input_len;
            const char *compare_start = input_buf;
            
            if (nr_screens > 1) {
                compare_len = input_len - 1;
                compare_start = input_buf + 1;
            }
            
            if (label_len >= compare_len && compare_len >= 0) {
                if (strncmp(label, compare_start, compare_len) == 0) {
                    matched[nr_matched++] = scr->hints[i];
                }
            }
        }
    } else {
        /* No screen prefix input, show all hints from all screens */
        for (int s = 0; s < nr_screens; s++) {
            struct screen_info *scr = &screens[s];
            for (size_t i = 0; i < scr->nr_hints; i++) {
                matched[nr_matched++] = scr->hints[i];
            }
        }
    }
}

/* ============================================================================
 * Hint Generation
 * ============================================================================ */

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

    /* Multi-screen uses 3-char labels, need wider hint */
    int hint_w = hint_size * (nr_screens > 1 ? 3 : 2);
    int hint_h = (int)(hint_size * 1.5);

    /* Generate hints for each screen */
    for (int s = 0; s < nr_screens; s++) {
        struct screen_info *scr = &screens[s];
        
        /* Calculate cell size for even hint distribution */
        int cell_w = scr->w / nc;
        int cell_h = scr->h / nr;
        
        int colgap = cell_w - hint_w;
        int rowgap = cell_h - hint_h;
        
        if (colgap < 0) colgap = 0;
        if (rowgap < 0) rowgap = 0;

        int x_offset = (scr->w - nc * hint_w - (nc - 1) * colgap) / 2;
        int y_offset = (scr->h - nr * hint_h - (nr - 1) * rowgap) / 2;

        scr->nr_hints = 0;
        for (int i = 0; i < nc; i++) {
            for (int j = 0; j < nr; j++) {
                if (scr->nr_hints >= MAX_HINTS) break;

                struct hint *h = &scr->hints[scr->nr_hints++];
                h->x = x_offset + i * (hint_w + colgap);
                h->y = y_offset + j * (hint_h + rowgap);
                h->w = hint_w;
                h->h = hint_h;
                h->label[0] = unique_chars[i];
                h->label[1] = unique_chars[j];
                h->label[2] = 0;
            }
        }
    }
    
    filter_hints();
}

/* ============================================================================
 * Wayland Initialization
 * ============================================================================ */

static int init_shm(void)
{
    char shm_name[256];
    
    /* Create shared memory for each screen */
    for (int i = 0; i < nr_screens; i++) {
        snprintf(shm_name, sizeof(shm_name), "/hyprwarp-%d-%d", getpid(), i);
        
        int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0600);
        if (fd < 0) {
            fprintf(stderr, "shm_open failed for screen %d: %s\n", i, strerror(errno));
            return -1;
        }

        size_t size = screens[i].w * screens[i].h * 4;
        if (ftruncate(fd, size) < 0) {
            fprintf(stderr, "ftruncate failed for screen %d: %s\n", i, strerror(errno));
            close(fd);
            return -1;
        }

        screens[i].data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (screens[i].data == MAP_FAILED) {
            fprintf(stderr, "mmap failed for screen %d: %s\n", i, strerror(errno));
            close(fd);
            return -1;
        }

        screens[i].wl_pool = wl_shm_create_pool(wl.shm, fd, size);
        close(fd);
        shm_unlink(shm_name);

        screens[i].stride = screens[i].w * 4;
    }
    
    return 0;
}

/* ============================================================================
 * Wayland Output Handlers
 * ============================================================================ */

static void output_handle_geometry(void *data, struct wl_output *output,
                                   int32_t x, int32_t y, int32_t physical_width,
                                   int32_t physical_height, int32_t subpixel,
                                   const char *make, const char *model,
                                   int32_t transform)
{
    (void)data; (void)output;
    (void)x; (void)y; (void)physical_width; (void)physical_height; (void)subpixel;
    (void)make; (void)model; (void)transform;
}
static void output_handle_done(void *data, struct wl_output *output)
{
    (void)data; (void)output;
}
static void output_handle_mode(void *data, struct wl_output *output,
                              uint32_t flags, int width, int height, int refresh)
{
    (void)data; (void)output; (void)refresh;
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        struct screen_info *scr = data;
        scr->w = width;
        scr->h = height;
    }
}
static void output_handle_scale(void *data, struct wl_output *output, int32_t factor)
{
    (void)output;
    struct screen_info *scr = data;
    scr->scale = factor;
}

static const struct wl_output_listener output_listener = {
    .geometry = output_handle_geometry,
    .mode = output_handle_mode,
    .done = output_handle_done,
    .scale = output_handle_scale
};

static void xdg_output_handle_logical_position(void *data, struct zxdg_output_v1 *xdg_output, int32_t x, int32_t y) {
    (void)xdg_output;
    struct screen_info *scr = data;
    scr->x = x;
    scr->y = y;
}
static void xdg_output_handle_logical_size(void *data, struct zxdg_output_v1 *xdg_output, int32_t w, int32_t h) {
    (void)xdg_output;
    struct screen_info *scr = data;
    scr->w = w;
    scr->h = h;
}
static void xdg_output_handle_done(void *data, struct zxdg_output_v1 *xdg_output)
{
    (void)data; (void)xdg_output;
}
static void xdg_output_handle_name(void *data, struct zxdg_output_v1 *xdg_output, const char *name)
{
    (void)data; (void)xdg_output; (void)name;
}
static void xdg_output_handle_description(void *data, struct zxdg_output_v1 *xdg_output, const char *description)
{
    (void)data; (void)xdg_output; (void)description;
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
    .logical_position = xdg_output_handle_logical_position,
    .logical_size = xdg_output_handle_logical_size,
    .done = xdg_output_handle_done,
    .name = xdg_output_handle_name,
    .description = xdg_output_handle_description,
};

/* wl_pointer event listeners */

static void pointer_handle_enter(void *data, struct wl_pointer *wl_pointer,
                                 uint32_t serial, struct wl_surface *wl_surface,
                                 wl_fixed_t sx, wl_fixed_t sy)
{
    (void)data; (void)wl_pointer; (void)serial;
    
    int found_screen = -1;
    for (int i = 0; i < nr_screens; i++) {
        if (wl_surface == screens[i].wl_surface) {
            found_screen = i;
            break;
        }
    }
    
    if (found_screen < 0) return;
    
    pointer_x = wl_fixed_to_int(sx);
    pointer_y = wl_fixed_to_int(sy);
    
    if (pointer_x < -10000 || pointer_x > 10000 || 
        pointer_y < -10000 || pointer_y > 10000) return;
}

static void pointer_handle_leave(void *data, struct wl_pointer *wl_pointer,
                                 uint32_t serial, struct wl_surface *wl_surface)
{
    (void)data; (void)wl_pointer; (void)serial; (void)wl_surface;
}

static void pointer_handle_motion(void *data, struct wl_pointer *wl_pointer,
                                  uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
    (void)data; (void)wl_pointer; (void)time;
    pointer_x = wl_fixed_to_int(sx);
    pointer_y = wl_fixed_to_int(sy);
    
    for (int i = 0; i < nr_screens; i++) {
        if (pointer_x >= screens[i].x && pointer_x < screens[i].x + screens[i].w &&
            pointer_y >= screens[i].y && pointer_y < screens[i].y + screens[i].h) {
            if (current_screen_index != i) {
                current_screen_index = i;
                screen = &screens[i];
            }
            break;
        }
    }
}

static void pointer_handle_button(void *data, struct wl_pointer *pointer,
                                  uint32_t serial, uint32_t time, uint32_t button,
                                  uint32_t state)
{
    (void)data; (void)pointer; (void)serial; (void)time; (void)button; (void)state;
}

static void pointer_handle_axis(void *data, struct wl_pointer *pointer,
                                uint32_t time, uint32_t axis, wl_fixed_t value)
{
    (void)data; (void)pointer; (void)time; (void)axis; (void)value;
}

static void pointer_handle_frame(void *data, struct wl_pointer *pointer)
{
    (void)data; (void)pointer;
}

static void pointer_handle_axis_source(void *data, struct wl_pointer *pointer,
                                       uint32_t axis_source)
{
    (void)data; (void)pointer; (void)axis_source;
}

static void pointer_handle_axis_stop(void *data, struct wl_pointer *pointer,
                                      uint32_t time, uint32_t axis)
{
    (void)data; (void)pointer; (void)time; (void)axis;
}

static void pointer_handle_axis_discrete(void *data, struct wl_pointer *pointer,
                                         uint32_t axis, int32_t discrete)
{
    (void)data; (void)pointer; (void)axis; (void)discrete;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_handle_enter,
    .leave = pointer_handle_leave,
    .motion = pointer_handle_motion,
    .button = pointer_handle_button,
    .axis = pointer_handle_axis,
    .frame = pointer_handle_frame,
    .axis_source = pointer_handle_axis_source,
    .axis_stop = pointer_handle_axis_stop,
    .axis_discrete = pointer_handle_axis_discrete,
};

static void global_handler(void *data, struct wl_registry *registry,
                          uint32_t name, const char *interface, uint32_t version)
{
    (void)version;
    struct wl_info *wl = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        wl->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        wl->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        wl->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        /* Add new screen to array */
        if (nr_screens < MAX_SCREENS) {
            struct screen_info *scr = &screens[nr_screens];
            scr->wl_output = wl_registry_bind(registry, name, &wl_output_interface, 2);
            scr->output_name = name;
            scr->scale = 1;
            scr->configured = 0;
            wl_output_add_listener(scr->wl_output, &output_listener, scr);
            nr_screens++;
        }
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

/* ============================================================================
 * Keyboard Input Handling
 * ============================================================================ */

static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
                                   uint32_t format, int fd, uint32_t size)
{
    (void)data; (void)keyboard;
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

static void keyboard_handle_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
    (void)data; (void)keyboard; (void)serial; (void)keys;
    for (int i = 0; i < nr_screens; i++) {
        if (surface == screens[i].wl_surface) {
            if (current_screen_index != i) {
                current_screen_index = i;
                screen = &screens[i];
                generate_hints();
                render_hints();
            }
            break;
        }
    }
}
static void keyboard_handle_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface) {
    (void)data; (void)keyboard; (void)serial; (void)surface;
}
static void keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
    (void)data; (void)keyboard; (void)serial; (void)mods_depressed; (void)mods_latched; (void)mods_locked; (void)group;
}

static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
                                uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
    (void)data; (void)keyboard; (void)serial; (void)time;
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
            size_t len = strlen(input_buf);
            if (len < sizeof(input_buf) - 1) {
                input_buf[len] = c;
                input_buf[len + 1] = '\0';
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
                size_t len = strlen(input_buf);
                input_buf[len] = utf8[0];
                input_buf[len + 1] = '\0';
                
                /* Multi-screen: first input determines current screen */
                if (nr_screens > 1 && strlen(input_buf) == 1) {
                    for (int s = 0; s < nr_screens && s < (int)strlen(hint_chars); s++) {
                        if (input_buf[0] == hint_chars[s]) {
                            current_screen_index = s;
                            screen = &screens[s];
                            break;
                        }
                    }
                    generate_hints();
                }
                
                filter_hints();

                if (nr_matched == 1) {
                    selected_x = matched[0].x + matched[0].w / 2;
                    selected_y = matched[0].y + matched[0].h / 2;
                    /* Coordinates are relative to current screen, no offset needed */
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

/* ============================================================================
 * Layer Surface Handlers
 * ============================================================================ */

static void layer_surface_handle_configure(void *data, struct zwlr_layer_surface_v1 *layer_surface,
                                           uint32_t serial, uint32_t width, uint32_t height)
{
    (void)width; (void)height;
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    
    struct screen_info *scr = data;
    scr->configured = 1;
    
    int all_configured = 1;
    for (int i = 0; i < nr_screens; i++) {
        if (!screens[i].configured) {
            all_configured = 0;
            break;
        }
    }
    
    if (all_configured) {
        layer_configured = 1;
    }
}

static void layer_surface_handle_closed(void *data, struct zwlr_layer_surface_v1 *layer_surface)
{
    (void)data; (void)layer_surface;
    running = 0;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_handle_configure,
    .closed = layer_surface_handle_closed,
};

static int create_layer_surfaces(void)
{
    if (nr_screens == 0) return -1;
    
    /* Create layer surface for each screen */
    for (int i = 0; i < nr_screens; i++) {
        screens[i].wl_surface = wl_compositor_create_surface(wl.compositor);
        if (!screens[i].wl_surface) return -1;

        screens[i].layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            wl.layer_shell, screens[i].wl_surface,
            screens[i].wl_output,  /* Bind to specific output */
            ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
            "hyprwarp");
        
        if (!screens[i].layer_surface) {
            wl_surface_destroy(screens[i].wl_surface);
            return -1;
        }

        zwlr_layer_surface_v1_set_size(screens[i].layer_surface, screens[i].w, screens[i].h);
        zwlr_layer_surface_v1_set_anchor(screens[i].layer_surface,
                                         ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                         ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
        zwlr_layer_surface_v1_set_exclusive_zone(screens[i].layer_surface, -1);
        zwlr_layer_surface_v1_set_keyboard_interactivity(screens[i].layer_surface, 
                                                         ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
        zwlr_layer_surface_v1_add_listener(screens[i].layer_surface, &layer_surface_listener, &screens[i]);

        wl_surface_commit(screens[i].wl_surface);
    }
    
    return 0;
}

/* ============================================================================
 * Command Expansion and Mouse Movement
 * ============================================================================ */

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
    snprintf(output, size, "%s", cmd);
    
    char buf[64];
    
    snprintf(buf, sizeof(buf), "%d", screen->w);
    replace_str(output, size, "{screen_w}", buf);
    
    snprintf(buf, sizeof(buf), "%d", screen->h);
    replace_str(output, size, "{screen_h}", buf);

    snprintf(buf, sizeof(buf), "%d", x);
    replace_str(output, size, "{x}", buf);
    
    snprintf(buf, sizeof(buf), "%d", y);
    replace_str(output, size, "{y}", buf);
    
    snprintf(buf, sizeof(buf), "%.3f", (double)x / screen->w);
    replace_str(output, size, "{scale_x}", buf);
    
    snprintf(buf, sizeof(buf), "%.3f", (double)y / screen->h);
    replace_str(output, size, "{scale_y}", buf);
    
    int global_x = screen->x + x;
    int global_y = screen->y + y;
    snprintf(buf, sizeof(buf), "%d", global_x);
    replace_str(output, size, "{global_x}", buf);
    
    snprintf(buf, sizeof(buf), "%d", global_y);
    replace_str(output, size, "{global_y}", buf);
    
    snprintf(buf, sizeof(buf), "%.3f", (double)global_x / global_surface_w);
    replace_str(output, size, "{global_scale_x}", buf);
    
    snprintf(buf, sizeof(buf), "%.3f", (double)global_y / global_surface_h);
    replace_str(output, size, "{global_scale_y}", buf);
}

static void move_mouse(int x, int y)
{
    if (wl.ptr) {
        zwlr_virtual_pointer_v1_motion_absolute(wl.ptr, 0, 
            (uint32_t)x, (uint32_t)y, 
            (uint32_t)screen->w, (uint32_t)screen->h);
        zwlr_virtual_pointer_v1_frame(wl.ptr);
    } else if (on_select_cmd[0] != '\0') {
        char cmd[512];
        expand_cmd(on_select_cmd, cmd, sizeof(cmd), x, y);
        int __attribute__((unused)) _res = system(cmd);
    } else {
        fprintf(stderr, "Error: on_select_cmd not set\n");
    }
}

/* ============================================================================
 * Main Function
 * ============================================================================ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    load_config();
    
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);

    wl.dpy = wl_display_connect(NULL);
    if (!wl.dpy) return 1;

    /* Step 1: Collect all global objects */
    wl.registry = wl_display_get_registry(wl.dpy);
    wl_registry_add_listener(wl.registry, &registry_listener, &wl);
    wl_display_roundtrip(wl.dpy);

    if (!wl.compositor || !wl.shm || !wl.layer_shell) return 1;

    /* Error if no screens found */
    if (nr_screens == 0) {
        fprintf(stderr, "Error: No outputs found\n");
        return 1;
    }

    /* Step 2: Create xdg_output for each screen to get logical coords */
    if (wl.xdg_output_manager) {
        for (int i = 0; i < nr_screens; i++) {
            struct zxdg_output_v1 *xdg_output = zxdg_output_manager_v1_get_xdg_output(
                wl.xdg_output_manager, screens[i].wl_output);
            screens[i].xdg_output = xdg_output;
            zxdg_output_v1_add_listener(xdg_output, &xdg_output_listener, &screens[i]);
        }
    }
    wl_display_roundtrip(wl.dpy);
    
    /* Step 3: Calculate global surface size (combined area of all screens) */
    global_origin_x = 0;
    global_origin_y = 0;
    for (int i = 0; i < nr_screens; i++) {
        if (screens[i].x < global_origin_x) global_origin_x = screens[i].x;
        if (screens[i].y < global_origin_y) global_origin_y = screens[i].y;
    }
    
    for (int i = 0; i < nr_screens; i++) {
        int right = screens[i].x + screens[i].w;
        int bottom = screens[i].y + screens[i].h;
        if (right > global_surface_w) global_surface_w = right;
        if (bottom > global_surface_h) global_surface_h = bottom;
    }
    
    /* Adjust surface size if negative coordinates exist */
    if (global_origin_x < 0) global_surface_w += -global_origin_x;
    if (global_origin_y < 0) global_surface_h += -global_origin_y;

    screen = &screens[0];
    current_screen_index = 0;

    if (wl.seat) {
        pointer = wl_seat_get_pointer(wl.seat);
        if (pointer) {
            wl_pointer_add_listener(pointer, &pointer_listener, NULL);
        }
        
        struct wl_keyboard *keyboard = wl_seat_get_keyboard(wl.seat);
        if (keyboard) wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
    }

    if (init_shm() < 0) return 1;

    xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkb_ctx) return 1;

    if (create_layer_surfaces() < 0) return 1;

    while (!layer_configured && wl_display_dispatch(wl.dpy) != -1) {}

    generate_hints();
    render_hints();

    fprintf(stderr, "Hint mode started. Type to filter, ESC to cancel.\n");

    while (running && wl_display_dispatch(wl.dpy) != -1) {}

    if (selection_made) {
        nr_matched = 0;
        render_hints();
        wl_display_roundtrip(wl.dpy);
        move_mouse(selected_x, selected_y);
if (on_exit_cmd[0] != '\0') {
            char cmd[256];
            expand_cmd(on_exit_cmd, cmd, sizeof(cmd), selected_x, selected_y);
            int __attribute__((unused)) _res = system(cmd);
        }
    }

    for (int i = 0; i < nr_screens; i++) {
        if (screens[i].wl_surface) wl_surface_destroy(screens[i].wl_surface);
        if (screens[i].wl_pool) wl_shm_pool_destroy(screens[i].wl_pool);
        if (screens[i].data && screens[i].data != MAP_FAILED) {
            munmap(screens[i].data, screens[i].w * screens[i].h * 4);
        }
    }
    if (xkb_state) xkb_state_unref(xkb_state);
    if (xkb_ctx) xkb_context_unref(xkb_ctx);
    if (pointer) wl_pointer_release(pointer);
    wl_display_disconnect(wl.dpy);

    return 0;
}

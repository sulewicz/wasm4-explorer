#include "explorer.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef W4X_ENABLE_SDL
#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_ttf.h>
#ifdef W4X_DIRECT_FB
#include <linux/fb.h>
#include <linux/input.h>
#endif

#define W4X_SCREEN_W 640
#define W4X_SCREEN_H 480
#define W4X_HEADER_H 54
#define W4X_FOOTER_H 34
#define W4X_GRID_COLUMNS 3
#define W4X_GRID_ROWS 2
#define W4X_VISIBLE_TILES (W4X_GRID_COLUMNS * W4X_GRID_ROWS)
#define W4X_TILE_X 10
#define W4X_TILE_Y (W4X_HEADER_H + 8)
#define W4X_TILE_W 200
#define W4X_TILE_H 180
#define W4X_TILE_GAP_X 10
#define W4X_TILE_GAP_Y 8
#define W4X_TITLE_BAR_H 30
#define W4X_NEAR_TILES 6

#define W4X_BTN_UP SDLK_UP
#define W4X_BTN_DOWN SDLK_DOWN
#define W4X_BTN_LEFT SDLK_LEFT
#define W4X_BTN_RIGHT SDLK_RIGHT
#define W4X_BTN_A SDLK_SPACE
#define W4X_BTN_B SDLK_LCTRL
#define W4X_BTN_X SDLK_LSHIFT
#define W4X_BTN_Y SDLK_LALT
#define W4X_BTN_L1 SDLK_e
#define W4X_BTN_R1 SDLK_t
#define W4X_BTN_SELECT SDLK_RCTRL
#define W4X_BTN_START SDLK_RETURN
#define W4X_BTN_MENU SDLK_ESCAPE
#ifdef W4X_DIRECT_FB
#define W4X_INPUT_DEVICE "/dev/input/event0"
#endif

typedef struct W4XThumbJob {
    pid_t pid;
    size_t index;
    char tmp_path[W4X_PATH_MAX];
    char final_path[W4X_PATH_MAX];
} W4XThumbJob;

typedef struct W4XUiState {
    SDL_Surface *video;
    SDL_Surface *screen;
    TTF_Font *font_title;
    TTF_Font *font_body;
    TTF_Font *font_small;
    W4XCatalog *catalog;
    const W4XPathSet *paths;
    const W4XCatalogOptions *catalog_options;
    size_t *view;
    size_t view_count;
    size_t selected;
    size_t top;
    char status[160];
    bool quit;
    bool dirty;
    bool refreshing;
    W4XThumbJob thumb_job;
#ifdef W4X_DIRECT_FB
    int fb_fd;
    int input_fd;
    uint32_t *fb_addr;
    long fb_size;
    struct fb_var_screeninfo fb_vinfo;
    struct fb_fix_screeninfo fb_finfo;
#endif
} W4XUiState;

static volatile sig_atomic_t ui_signal_quit = 0;
static SDL_Color color_bg = {16, 18, 22, 0};
static SDL_Color color_panel = {29, 32, 39, 0};
static SDL_Color color_selected = {84, 72, 150, 0};
static SDL_Color color_text = {242, 244, 248, 0};
static SDL_Color color_muted = {164, 173, 190, 0};
static SDL_Color color_accent = {118, 213, 171, 0};
static SDL_Color color_warn = {232, 181, 93, 0};

static bool mkdir_if_missing(const char *path)
{
    return mkdir(path, 0777) == 0 || errno == EEXIST;
}

static bool ensure_image_cache_dir(const W4XPathSet *paths)
{
    char path[W4X_PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/images", paths->cache_dir);
    return written > 0 && (size_t)written < sizeof(path) && mkdir_if_missing(path);
}

static const char *font_path_primary(void)
{
    if (access("/mnt/SDCARD/miyoo/app/Exo-2-Bold-Italic_Universal.ttf", R_OK) == 0)
        return "/mnt/SDCARD/miyoo/app/Exo-2-Bold-Italic_Universal.ttf";
    if (access("/customer/app/Exo-2-Bold-Italic.ttf", R_OK) == 0)
        return "/customer/app/Exo-2-Bold-Italic.ttf";
    return "/mnt/SDCARD/miyoo/app/wqy-microhei.ttc";
}

static const char *font_path_body(void)
{
    if (access("/mnt/SDCARD/miyoo/app/wqy-microhei.ttc", R_OK) == 0)
        return "/mnt/SDCARD/miyoo/app/wqy-microhei.ttc";
    return font_path_primary();
}

static Uint32 map_color(SDL_Surface *surface, SDL_Color color)
{
    return SDL_MapRGB(surface->format, color.r, color.g, color.b);
}

static SDL_Surface *create_opaque_surface_like(SDL_Surface *source, int w, int h)
{
    if (source == NULL || source->format == NULL)
        return SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, 32, 0, 0, 0, 0);

    return SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, source->format->BitsPerPixel,
                                source->format->Rmask, source->format->Gmask,
                                source->format->Bmask, 0);
}

#ifdef W4X_DIRECT_FB
static Uint32 fb_bitfield_mask(struct fb_bitfield field)
{
    if (field.length == 0 || field.length >= 32)
        return 0;
    return ((1u << field.length) - 1u) << field.offset;
}

static SDL_Surface *create_framebuffer_surface(W4XUiState *ui)
{
    Uint32 rmask = fb_bitfield_mask(ui->fb_vinfo.red);
    Uint32 gmask = fb_bitfield_mask(ui->fb_vinfo.green);
    Uint32 bmask = fb_bitfield_mask(ui->fb_vinfo.blue);
    Uint32 amask = fb_bitfield_mask(ui->fb_vinfo.transp);

    if (rmask == 0 || gmask == 0 || bmask == 0)
        return NULL;

    return SDL_CreateRGBSurface(SDL_SWSURFACE, (int)ui->fb_vinfo.xres,
                                (int)ui->fb_vinfo.yres,
                                (int)ui->fb_vinfo.bits_per_pixel,
                                rmask, gmask, bmask, amask);
}
#endif

static void fill_rect(SDL_Surface *surface, SDL_Rect rect, SDL_Color color)
{
    SDL_FillRect(surface, &rect, map_color(surface, color));
}

static void stroke_rect(SDL_Surface *surface, SDL_Rect rect, SDL_Color color,
                        int thickness)
{
    if (thickness <= 0)
        return;

    fill_rect(surface, (SDL_Rect){rect.x, rect.y, rect.w, thickness}, color);
    fill_rect(surface, (SDL_Rect){rect.x, rect.y + rect.h - thickness,
                                  rect.w, thickness}, color);
    fill_rect(surface, (SDL_Rect){rect.x, rect.y, thickness, rect.h}, color);
    fill_rect(surface, (SDL_Rect){rect.x + rect.w - thickness, rect.y,
                                  thickness, rect.h}, color);
}

static void draw_text(SDL_Surface *surface, TTF_Font *font, const char *text,
                      SDL_Color color, int x, int y, int max_w)
{
    if (font == NULL || text == NULL || text[0] == '\0' || max_w <= 0)
        return;

    char buf[W4X_TITLE_MAX + W4X_AUTHOR_MAX + 8];
    snprintf(buf, sizeof(buf), "%s", text);

    size_t len = strlen(buf);
    while (len > 0) {
        int w = 0;
        int h = 0;
        if (TTF_SizeUTF8(font, buf, &w, &h) == 0 && w <= max_w)
            break;
        if (len <= 1)
            break;
        len--;
        buf[len] = '\0';
    }

    if (len != strlen(text) && len > 1) {
        buf[len - 1] = '.';
        buf[len - 2] = '.';
    }

    SDL_Surface *text_surface = TTF_RenderUTF8_Blended(font, buf, color);
    if (text_surface == NULL)
        return;
    SDL_Rect dst = {x, y, 0, 0};
    SDL_BlitSurface(text_surface, NULL, surface, &dst);
    SDL_FreeSurface(text_surface);
}

static void set_status_with_title(W4XUiState *ui, const char *prefix,
                                  const char *title)
{
    snprintf(ui->status, sizeof(ui->status), "%s", prefix);
    strncat(ui->status, title, sizeof(ui->status) - strlen(ui->status) - 1);
}

static void rebuild_view(W4XUiState *ui)
{
    ui->view_count = 0;
    for (size_t i = 0; i < ui->catalog->count; i++)
        ui->view[ui->view_count++] = i;
    if (ui->selected >= ui->view_count)
        ui->selected = ui->view_count == 0 ? 0 : ui->view_count - 1;
    if (ui->top > ui->selected)
        ui->top = ui->selected;
    ui->top -= ui->top % W4X_GRID_COLUMNS;
    while (ui->selected >= ui->top + W4X_VISIBLE_TILES)
        ui->top += W4X_GRID_COLUMNS;
}

static void draw_thumbnail_placeholder(W4XUiState *ui, SDL_Rect rect,
                                       W4XThumbnailStatus status)
{
    fill_rect(ui->screen, rect, color_panel);
    SDL_Color label_color = status == W4X_THUMBNAIL_STALE ? color_warn : color_muted;
    draw_text(ui->screen, ui->font_small, "W4", label_color,
              rect.x + rect.w / 2 - 12, rect.y + rect.h / 2 - 8, rect.w - 8);
}

static bool image_file_decodes(const char *image_path)
{
    SDL_Surface *image = IMG_Load(image_path);
    if (image == NULL)
        return false;
    SDL_FreeSurface(image);
    return true;
}

static void draw_thumbnail(W4XUiState *ui, const W4XCatalogEntry *entry,
                           SDL_Rect rect)
{
    W4XThumbnailStatus status = w4x_thumbnail_status(ui->paths, entry, true);
    if (status != W4X_THUMBNAIL_VALID) {
        draw_thumbnail_placeholder(ui, rect, status);
        return;
    }

    char image_path[W4X_PATH_MAX];
    if (w4x_thumbnail_path(ui->paths, entry, image_path, sizeof(image_path)) != 0) {
        draw_thumbnail_placeholder(ui, rect, W4X_THUMBNAIL_MISSING);
        return;
    }

    SDL_Surface *image = IMG_Load(image_path);
    if (image == NULL) {
        draw_thumbnail_placeholder(ui, rect, W4X_THUMBNAIL_MISSING);
        return;
    }

    SDL_Surface *converted = create_opaque_surface_like(ui->screen, image->w, image->h);
    SDL_Surface *scaled = create_opaque_surface_like(ui->screen, rect.w, rect.h);
    bool rendered = false;
    if (converted != NULL && scaled != NULL) {
        SDL_FillRect(converted, NULL, map_color(converted, color_panel));
        SDL_FillRect(scaled, NULL, map_color(scaled, color_panel));
        SDL_Rect src = {0, 0, converted->w, converted->h};
        SDL_Rect dst = {0, 0, rect.w, rect.h};
        rendered = SDL_BlitSurface(image, NULL, converted, NULL) == 0 &&
                   SDL_SoftStretch(converted, &src, scaled, &dst) == 0 &&
                   SDL_BlitSurface(scaled, NULL, ui->screen, &rect) == 0;
    }
    if (!rendered)
        draw_thumbnail_placeholder(ui, rect, W4X_THUMBNAIL_MISSING);
    if (scaled != NULL)
        SDL_FreeSurface(scaled);
    if (converted != NULL)
        SDL_FreeSurface(converted);
    SDL_FreeSurface(image);
}

static bool download_thumb_start(W4XUiState *ui, size_t catalog_index)
{
    if (ui->thumb_job.pid > 0)
        return false;

    W4XCatalogEntry *entry = &ui->catalog->entries[catalog_index];
    if (entry->image_url[0] == '\0')
        return false;

    if (w4x_thumbnail_status(ui->paths, entry, true) == W4X_THUMBNAIL_VALID)
        return false;

    if (w4x_thumbnail_path(ui->paths, entry, ui->thumb_job.final_path,
                           sizeof(ui->thumb_job.final_path)) != 0)
        return false;

    int written = snprintf(ui->thumb_job.tmp_path, sizeof(ui->thumb_job.tmp_path),
                           "%s.%ld.tmp", ui->thumb_job.final_path, (long)getpid());
    if (written < 0 || (size_t)written >= sizeof(ui->thumb_job.tmp_path))
        return false;

    pid_t pid = fork();
    if (pid < 0)
        return false;
    if (pid == 0) {
        execlp("wget", "wget", "-q", "-O", ui->thumb_job.tmp_path,
               entry->image_url, (char *)NULL);
        _exit(127);
    }

    ui->thumb_job.pid = pid;
    ui->thumb_job.index = catalog_index;
    set_status_with_title(ui, "Fetching thumbnail: ", entry->title);
    return true;
}

static void download_thumb_poll(W4XUiState *ui)
{
    if (ui->thumb_job.pid <= 0)
        return;

    int status = 0;
    pid_t rc = waitpid(ui->thumb_job.pid, &status, WNOHANG);
    if (rc == 0)
        return;
    if (rc < 0) {
        ui->thumb_job.pid = 0;
        return;
    }

    W4XCatalogEntry *entry = &ui->catalog->entries[ui->thumb_job.index];
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
        image_file_decodes(ui->thumb_job.tmp_path) &&
        rename(ui->thumb_job.tmp_path, ui->thumb_job.final_path) == 0 &&
        w4x_thumbnail_mark_valid(ui->paths, entry) == 0) {
        set_status_with_title(ui, "Thumbnail cached: ", entry->title);
    }
    else {
        unlink(ui->thumb_job.tmp_path);
        set_status_with_title(ui, "Thumbnail unavailable: ", entry->title);
    }

    ui->thumb_job.pid = 0;
    ui->dirty = true;
}

static void schedule_visible_thumbnails(W4XUiState *ui)
{
    if (ui->thumb_job.pid > 0 || ui->view_count == 0)
        return;

    size_t start = ui->top > W4X_NEAR_TILES ? ui->top - W4X_NEAR_TILES : 0;
    size_t end = ui->top + W4X_VISIBLE_TILES + W4X_NEAR_TILES;
    if (end > ui->view_count)
        end = ui->view_count;

    for (size_t i = start; i < end; i++) {
        size_t catalog_index = ui->view[i];
        W4XCatalogEntry *entry = &ui->catalog->entries[catalog_index];
        W4XThumbnailStatus status = w4x_thumbnail_status(ui->paths, entry, true);
        if (status != W4X_THUMBNAIL_VALID && download_thumb_start(ui, catalog_index)) {
            ui->dirty = true;
            return;
        }
    }
}

static void present_ui(W4XUiState *ui)
{
#ifdef W4X_DIRECT_FB
    if (ui->fb_addr == NULL)
        return;

    int buffers = ui->fb_vinfo.yres > 0 ?
        (int)(ui->fb_vinfo.yres_virtual / ui->fb_vinfo.yres) : 1;
    if (buffers < 1)
        buffers = 1;

    if (ui->screen->format->BytesPerPixel != 4)
        return;

    uint32_t *pixels = (uint32_t *)ui->screen->pixels;
    int src_stride = ui->screen->pitch / (int)sizeof(uint32_t);
    int dst_stride = ui->fb_finfo.line_length / (int)sizeof(uint32_t);
    int width = ui->screen->w < (int)ui->fb_vinfo.xres ?
        ui->screen->w : (int)ui->fb_vinfo.xres;
    int height = ui->screen->h < (int)ui->fb_vinfo.yres ?
        ui->screen->h : (int)ui->fb_vinfo.yres;

    for (int b = 0; b < buffers; b++) {
        int buffer_y = b * (int)ui->fb_vinfo.yres;
        for (int y = 0; y < height; y++) {
            int dst_y = buffer_y + ((int)ui->fb_vinfo.yres - 1 - y);
            long row_offset = (long)dst_y * (long)dst_stride;
            long src_offset = (long)y * (long)src_stride;
            for (int x = 0; x < width; x++) {
                int dst_x = (int)ui->fb_vinfo.xres - 1 - x;
                ui->fb_addr[row_offset + dst_x] = pixels[src_offset + x];
            }
        }
    }
#else
    SDL_BlitSurface(ui->screen, NULL, ui->video, NULL);
    SDL_Flip(ui->video);
#endif
}

static void render_ui(W4XUiState *ui)
{
    SDL_FillRect(ui->screen, NULL, map_color(ui->screen, color_bg));

    fill_rect(ui->screen, (SDL_Rect){0, 0, W4X_SCREEN_W, W4X_HEADER_H}, color_panel);
    draw_text(ui->screen, ui->font_title, "WASM-4 Explorer", color_text, 18, 12, 330);

    char header[128];
    snprintf(header, sizeof(header), "%zu games (%s)", ui->view_count,
             ui->refreshing ? "refreshing..." : "X to refresh");
    draw_text(ui->screen, ui->font_small, header, color_muted, 390, 18, 220);

    if (ui->view_count == 0) {
        draw_text(ui->screen, ui->font_body, "No catalog entries match this view.",
                  color_text, 42, 190, 560);
    }

    for (size_t slot = 0; slot < W4X_VISIBLE_TILES; slot++) {
        size_t view_index = ui->top + slot;
        if (view_index >= ui->view_count)
            break;

        bool selected = view_index == ui->selected;
        int col = (int)(slot % W4X_GRID_COLUMNS);
        int row = (int)(slot / W4X_GRID_COLUMNS);
        int x = W4X_TILE_X + col * (W4X_TILE_W + W4X_TILE_GAP_X);
        int y = W4X_TILE_Y + row * (W4X_TILE_H + W4X_TILE_GAP_Y);
        SDL_Rect tile_rect = {x, y, W4X_TILE_W, W4X_TILE_H};
        fill_rect(ui->screen, tile_rect, selected ? color_selected : color_panel);

        W4XCatalogEntry *entry = &ui->catalog->entries[ui->view[view_index]];
        draw_thumbnail(ui, entry, tile_rect);

        SDL_Rect title_bar = {x, y + W4X_TILE_H - W4X_TITLE_BAR_H,
                              W4X_TILE_W, W4X_TITLE_BAR_H};
        fill_rect(ui->screen, title_bar, selected ? color_selected : color_panel);
        draw_text(ui->screen, ui->font_small, entry->title, color_text,
                  x + 8, title_bar.y + 7, W4X_TILE_W - 16);
        if (selected)
            stroke_rect(ui->screen, tile_rect, color_accent, 3);
    }

    fill_rect(ui->screen, (SDL_Rect){0, W4X_SCREEN_H - W4X_FOOTER_H,
                                     W4X_SCREEN_W, W4X_FOOTER_H}, color_panel);
    if (ui->view_count > 0) {
        W4XCatalogEntry *entry = &ui->catalog->entries[ui->view[ui->selected]];
        draw_text(ui->screen, ui->font_small, entry->author, color_muted,
                  16, W4X_SCREEN_H - 25, 360);
    }
    draw_text(ui->screen, ui->font_small, ui->status, color_accent,
              430, W4X_SCREEN_H - 25, 190);

    present_ui(ui);
    ui->dirty = false;
}

static void ensure_selection_visible(W4XUiState *ui)
{
    if (ui->selected < ui->top)
        ui->top = (ui->selected / W4X_GRID_COLUMNS) * W4X_GRID_COLUMNS;
    while (ui->selected >= ui->top + W4X_VISIBLE_TILES)
        ui->top += W4X_GRID_COLUMNS;
}

static void select_by(W4XUiState *ui, int delta)
{
    if (ui->view_count == 0 || delta == 0)
        return;

    if (delta < 0) {
        size_t step = (size_t)(-delta);
        ui->selected = ui->selected < step ? 0 : ui->selected - step;
    }
    else {
        ui->selected += (size_t)delta;
        if (ui->selected >= ui->view_count)
            ui->selected = ui->view_count - 1;
    }

    ensure_selection_visible(ui);
    ui->dirty = true;
}

static void page_by(W4XUiState *ui, int delta)
{
    if (ui->view_count == 0)
        return;

    if (delta < 0) {
        size_t step = ui->selected < W4X_VISIBLE_TILES ? ui->selected : W4X_VISIBLE_TILES;
        ui->selected -= step;
    }
    else {
        ui->selected += W4X_VISIBLE_TILES;
        if (ui->selected >= ui->view_count)
            ui->selected = ui->view_count - 1;
    }

    ui->top = (ui->selected / W4X_VISIBLE_TILES) * W4X_VISIBLE_TILES;
    ensure_selection_visible(ui);
    ui->dirty = true;
}

static void refresh_catalog(W4XUiState *ui)
{
    ui->refreshing = true;
    snprintf(ui->status, sizeof(ui->status), "Refreshing catalog");
    render_ui(ui);

    W4XCatalogOptions forced = *ui->catalog_options;
    forced.force = true;
    W4XCatalogResult result;
    if (w4x_refresh_catalog(ui->paths, &forced, &result) == 0) {
        W4XCatalog next;
        if (w4x_catalog_load_cache(ui->paths, &next) == 0) {
            w4x_catalog_free(ui->catalog);
            *ui->catalog = next;
            rebuild_view(ui);
            snprintf(ui->status, sizeof(ui->status), "Catalog %s",
                     w4x_catalog_status_string(result.status));
        }
        else {
            snprintf(ui->status, sizeof(ui->status), "Refresh failed");
        }
    }
    else {
        snprintf(ui->status, sizeof(ui->status), "Refresh failed");
    }
    ui->refreshing = false;
    ui->dirty = true;
}

static void handle_key(W4XUiState *ui, SDLKey key)
{
    switch (key) {
    case W4X_BTN_UP:
        select_by(ui, -W4X_GRID_COLUMNS);
        break;
    case W4X_BTN_DOWN:
        select_by(ui, W4X_GRID_COLUMNS);
        break;
    case W4X_BTN_L1:
        page_by(ui, -1);
        break;
    case W4X_BTN_R1:
        page_by(ui, 1);
        break;
    case W4X_BTN_LEFT:
        select_by(ui, -1);
        break;
    case W4X_BTN_RIGHT:
        select_by(ui, 1);
        break;
    case W4X_BTN_B:
    case W4X_BTN_MENU:
        ui->quit = true;
        break;
    case W4X_BTN_X:
        refresh_catalog(ui);
        break;
    case W4X_BTN_A:
    case W4X_BTN_START:
        if (ui->view_count > 0) {
            W4XCatalogEntry *entry = &ui->catalog->entries[ui->view[ui->selected]];
            set_status_with_title(ui, "Installing: ", entry->title);
            render_ui(ui);

            W4XInstallResult result;
            if (w4x_install_catalog_entry(ui->paths, entry, &result) == 0) {
                entry->installed = true;
                W4XLaunchResult launch;
                if (w4x_prepare_onion_launch(ui->paths, entry, &result, &launch) == 0) {
                    set_status_with_title(ui, "Launching: ", entry->title);
                    render_ui(ui);
                    w4x_log(ui->paths, "native UI prepared Onion launch handoff");
                    ui->quit = true;
                }
                else {
                    set_status_with_title(ui, "Launch failed: ", entry->title);
                    w4x_log(ui->paths, "native UI failed to prepare Onion launch handoff");
                }
            }
            else {
                set_status_with_title(ui, "Install failed: ", entry->title);
                w4x_log(ui->paths, "native UI failed to install catalog entry");
            }
            ui->dirty = true;
        }
        break;
    default:
        break;
    }
}

static void handle_signal(int sig)
{
    (void)sig;
    ui_signal_quit = 1;
}

static long monotonic_ms(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0)
        return 0;
    return (long)tv.tv_sec * 1000L + (long)tv.tv_usec / 1000L;
}

#ifdef W4X_DIRECT_FB
static SDLKey translate_hw_key(int key)
{
    switch (key) {
    case KEY_UP:
        return W4X_BTN_UP;
    case KEY_DOWN:
        return W4X_BTN_DOWN;
    case KEY_LEFT:
        return W4X_BTN_LEFT;
    case KEY_RIGHT:
        return W4X_BTN_RIGHT;
    case KEY_SPACE:
        return W4X_BTN_A;
    case KEY_LEFTCTRL:
        return W4X_BTN_B;
    case KEY_LEFTSHIFT:
        return W4X_BTN_X;
    case KEY_LEFTALT:
        return W4X_BTN_Y;
    case KEY_E:
        return W4X_BTN_L1;
    case KEY_T:
        return W4X_BTN_R1;
    case KEY_RIGHTCTRL:
        return W4X_BTN_SELECT;
    case KEY_ENTER:
        return W4X_BTN_START;
    case KEY_ESC:
        return W4X_BTN_MENU;
    default:
        return SDLK_UNKNOWN;
    }
}
#endif

static bool init_sdl(W4XUiState *ui)
{
    ui_signal_quit = 0;
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
#ifdef W4X_DIRECT_FB
    ui->fb_fd = -1;
    ui->input_fd = -1;

    if (SDL_Init(0) != 0)
        return false;
    SDL_ShowCursor(SDL_DISABLE);
    if (TTF_Init() != 0)
        return false;

    ui->fb_fd = open("/dev/fb0", O_RDWR);
    if (ui->fb_fd < 0)
        return false;
    if (ioctl(ui->fb_fd, FBIOGET_FSCREENINFO, &ui->fb_finfo) != 0 ||
        ioctl(ui->fb_fd, FBIOGET_VSCREENINFO, &ui->fb_vinfo) != 0)
        return false;

    ui->fb_vinfo.yoffset = 0;
    (void)ioctl(ui->fb_fd, FBIOPUT_VSCREENINFO, &ui->fb_vinfo);

    ui->fb_size = (long)ui->fb_finfo.smem_len;
    ui->fb_addr = mmap(NULL, (size_t)ui->fb_size, PROT_READ | PROT_WRITE,
                       MAP_SHARED, ui->fb_fd, 0);
    if (ui->fb_addr == MAP_FAILED) {
        ui->fb_addr = NULL;
        return false;
    }

    ui->input_fd = open(W4X_INPUT_DEVICE, O_RDONLY | O_NONBLOCK);
    if (ui->input_fd < 0)
        return false;

    ui->video = NULL;
    ui->screen = create_framebuffer_surface(ui);
#else
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return false;
    SDL_ShowCursor(SDL_DISABLE);
    SDL_EnableKeyRepeat(260, 70);
    if (TTF_Init() != 0)
        return false;

    ui->video = SDL_SetVideoMode(W4X_SCREEN_W, W4X_SCREEN_H, 32, SDL_HWSURFACE);
    ui->screen = SDL_CreateRGBSurface(SDL_SWSURFACE, W4X_SCREEN_W, W4X_SCREEN_H,
                                      32, 0, 0, 0, 0);
#endif
    ui->font_title = TTF_OpenFont(font_path_primary(), 26);
    ui->font_body = TTF_OpenFont(font_path_body(), 22);
    ui->font_small = TTF_OpenFont(font_path_body(), 15);

    return ui->screen != NULL && ui->font_title != NULL &&
           ui->font_body != NULL && ui->font_small != NULL
#ifndef W4X_DIRECT_FB
           && ui->video != NULL
#endif
        ;
}

static void free_sdl(W4XUiState *ui)
{
    if (ui->thumb_job.pid > 0) {
        kill(ui->thumb_job.pid, SIGTERM);
        waitpid(ui->thumb_job.pid, NULL, 0);
        unlink(ui->thumb_job.tmp_path);
    }
    if (ui->font_title != NULL)
        TTF_CloseFont(ui->font_title);
    if (ui->font_body != NULL)
        TTF_CloseFont(ui->font_body);
    if (ui->font_small != NULL)
        TTF_CloseFont(ui->font_small);
    if (ui->screen != NULL)
        SDL_FreeSurface(ui->screen);
    if (ui->video != NULL)
        SDL_FreeSurface(ui->video);
#ifdef W4X_DIRECT_FB
    if (ui->fb_addr != NULL)
        munmap(ui->fb_addr, (size_t)ui->fb_size);
    if (ui->fb_fd >= 0)
        close(ui->fb_fd);
    if (ui->input_fd >= 0)
        close(ui->input_fd);
#endif
    TTF_Quit();
    SDL_Quit();
}

#ifdef W4X_DIRECT_FB
static void poll_direct_input(W4XUiState *ui)
{
    if (ui->input_fd < 0)
        return;

    struct pollfd fd = {.fd = ui->input_fd, .events = POLLIN};
    while (poll(&fd, 1, 0) > 0) {
        struct input_event ev;
        ssize_t bytes = read(ui->input_fd, &ev, sizeof(ev));
        if (bytes != (ssize_t)sizeof(ev))
            return;
        if (ev.type != EV_KEY || ev.value != 1)
            continue;
        SDLKey key = translate_hw_key(ev.code);
        if (key != SDLK_UNKNOWN)
            handle_key(ui, key);
    }
}
#endif

int w4x_run_browser_ui(const W4XPathSet *paths, W4XCatalog *catalog,
                       const W4XCatalogOptions *catalog_options, bool headless)
{
    if (headless) {
        printf("native_ui=headless rows=%zu\n", catalog->count);
        return 0;
    }

    ensure_image_cache_dir(paths);

    W4XUiState ui;
    memset(&ui, 0, sizeof(ui));
    ui.paths = paths;
    ui.catalog = catalog;
    ui.catalog_options = catalog_options;
    ui.view = calloc(catalog->count == 0 ? 1 : catalog->count, sizeof(*ui.view));
    if (ui.view == NULL)
        return -1;

    snprintf(ui.status, sizeof(ui.status), "Ready");
    rebuild_view(&ui);
    ui.dirty = true;
    bool exit_after_first_render = getenv("WASM4_UI_EXIT_AFTER_FIRST_RENDER") != NULL;
    long start_ms = monotonic_ms();

    if (!init_sdl(&ui)) {
        free(ui.view);
        free_sdl(&ui);
        return -1;
    }

    Uint32 last_render = 0;
    while (!ui.quit && !ui_signal_quit) {
#ifdef W4X_DIRECT_FB
        poll_direct_input(&ui);
#else
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT)
                ui.quit = true;
            else if (event.type == SDL_KEYDOWN)
                handle_key(&ui, event.key.keysym.sym);
        }
#endif

        download_thumb_poll(&ui);
        schedule_visible_thumbnails(&ui);

        Uint32 now = SDL_GetTicks();
        if (ui.dirty || now - last_render > 250) {
            render_ui(&ui);
            last_render = now;
            if (exit_after_first_render) {
                printf("native_ui_first_render_ms=%ld rows=%zu\n",
                       monotonic_ms() - start_ms, catalog->count);
                ui.quit = true;
            }
        }
        SDL_Delay(16);
    }

    free(ui.view);
    free_sdl(&ui);
    return 0;
}

#else

int w4x_run_browser_ui(const W4XPathSet *paths, W4XCatalog *catalog,
                       const W4XCatalogOptions *catalog_options, bool headless)
{
    (void)paths;
    (void)catalog_options;
    (void)headless;
    printf("native_ui=disabled rows=%zu\n", catalog->count);
    return 0;
}

#endif

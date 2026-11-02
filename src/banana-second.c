/* banana-second.so - LD_PRELOAD shim: a second screen for ScummVM on bananaOS (bottom panel DSI-1).
 *
 * ScummVM's SurfaceSDL backend composes the whole picture (game, GUI, cursor) in a software surface and uploads it
 * with SDL_UpdateTexture(screenTexture, NULL, pixels, pitch), then SDL_RenderCopy()s it into the window.
 * We copy those pixels (no GPU readback) and draw them with nearest-neighbour scaling into a DRM dumb buffer on a
 * free CRTC of the bottom panel (the technique of the Lighthouse companion screen for bananaOS).
 *
 * Modes (F10 goes round; the launcher's gptokeyb file maps R2 to F10):
 *   0 mirror : the bottom panel shows the same picture as the top, touches land where they are drawn.
 *   1 split  : "verb screen", only in games listed in the split file. The top panel shows only the room (the first N of
 *              the 200 game lines), the bottom panel shows the rest (sentence line, verbs, inventory) enlarged. Touches
 *              on the bottom panel are rewritten to the matching point of the game picture. After a tap the pointer is
 *              put back where it was and one click is made there.
 *   2 zoom   : works in every game. The top panel shows an enlarged part (1.5x, 2x, 3x; F8 changes it, gptokeyb maps
 *              L3 to F8) that follows the pointer, the bottom panel shows the whole picture with a frame around it.
 * F7 opens the on-screen keyboard (turned into Ctrl+F7). F9 is turned into '.' (skips a line of dialogue in SCUMM games).
 *
 * Env: BANANA_SECOND=0 disables, BANANA_SECOND_MS (min ms between mirrored frames, default 40), BANANA_PANEL_ID (1),
 *      BANANA_SPLIT_FILE (lines "title substring=room lines of 200"), BANANA_KEEP_POINTER=0, BANANA_AUTOCLICK=0,
 *      BANANA_RETURN_MS (250).
 * Debugging from a shell on the device: touch /tmp/banana-dump (next frame to /tmp/banana-frame.ppm), write "MODE [ZOOM]"
 * to /tmp/banana-mode, touch /tmp/banana-shot (both panels to /tmp/banana-top.ppm and /tmp/banana-bottom.ppm).
 * Licence: GPL-3.0-or-later.
 */
#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define LOG(...) do { fprintf(stderr, "BANANA-SECOND: " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } while (0)

static struct {
    int tried, ok, failed;
    int fd; uint32_t crtc, fb, pitch; size_t size; uint32_t *px; int W, H;
    /* last uploaded frame */
    SDL_Texture *tex; int sw, sh; uint8_t *buf; size_t bufsize; int dirty;
    /* where the window shows it (as ScummVM asked) */
    SDL_Rect dst; int have_dst; int dst_changed;
    int outw, outh;
    uint32_t last_ms, interval;
    uint32_t lut565[65536];
    int *xmap; int xmap_n;
    uint64_t frames, tsum; uint32_t tlog;
    /* split mode */
    char title[256]; int roomline; /* room lines of 200, 0 = no split for this title */
    int mode;                      /* 0 mirror, 1 split, 2 zoom */
    int zi, have_ptr, ptr_x, ptr_y, view_x, view_y, view_w, view_h, view_init; /* zoom: top shows a window of the picture that follows the pointer */
    int presents;
    int shot_req, shot_bottom, nudge; uint32_t shot_after; /* remote control from /tmp for screenshots */
    /* pointer parking: the stick-driven pointer position before a bottom-panel tap, put back afterwards */
    SDL_Window *win; int have_cur, cur_x, cur_y; uint32_t ret_due, quiet_until; int keep_pointer, nfingers, return_ms, stage, autoclick;
    struct { uint32_t ms; int x, y; } hist[64]; int hist_n;
    /* bottom-panel rect of the split picture, and the texture rows it shows */
    SDL_Rect db; int roomy;
} G;

/* ---------------- DRM bottom panel ---------------- */
static int find_fd(void) {
    char path[64], buf[256];
    for (int fd = 3; fd < 256; fd++) {
        snprintf(path, sizeof path, "/proc/self/fd/%d", fd);
        ssize_t n = readlink(path, buf, sizeof buf - 1);
        if (n > 0) { buf[n] = 0; if (!strncmp(buf, "/dev/dri/card", 13)) return fd; }
    }
    return -1;
}

static int panel_setup(void) {
    int fd = find_fd();
    if (fd < 0) { LOG("no /dev/dri/card fd in this process"); return 0; }
    drmModeRes *res = drmModeGetResources(fd);
    if (!res) { LOG("drmModeGetResources failed"); return 0; }
    const char *e = getenv("BANANA_PANEL_ID");
    int want = e ? atoi(e) : 1;
    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
        if (!c) continue;
        if (c->connector_type == 16 && (int)c->connector_type_id == want && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0)
            conn = c;
        else drmModeFreeConnector(c);
    }
    if (!conn) { LOG("DSI-%d not found", want); drmModeFreeResources(res); return 0; }
    drmModeModeInfo mode = conn->modes[0];
    int W = mode.hdisplay, H = mode.vdisplay;
    uint32_t conn_id = conn->connector_id;
    uint32_t busy = 0;
    for (int i = 0; i < res->count_connectors; i++) {
        if (res->connectors[i] == conn_id) continue;
        drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
        if (!c) continue;
        if (c->encoder_id) {
            drmModeEncoder *en = drmModeGetEncoder(fd, c->encoder_id);
            if (en) {
                for (int k = 0; k < res->count_crtcs; k++) if (res->crtcs[k] == en->crtc_id) busy |= 1u << k;
                drmModeFreeEncoder(en);
            }
        }
        drmModeFreeConnector(c);
    }
    uint32_t possible = 0, cur = 0;
    for (int i = 0; i < conn->count_encoders; i++) {
        drmModeEncoder *en = drmModeGetEncoder(fd, conn->encoders[i]);
        if (!en) continue;
        possible |= en->possible_crtcs;
        if (conn->encoder_id == en->encoder_id) cur = en->crtc_id;
        drmModeFreeEncoder(en);
    }
    uint32_t crtc = 0;
    for (int k = 0; k < res->count_crtcs && !crtc; k++)
        if ((possible & (1u << k)) && !(busy & (1u << k)) && !(cur && res->crtcs[k] != cur)) crtc = res->crtcs[k];
    for (int k = 0; k < res->count_crtcs && !crtc; k++)
        if ((possible & (1u << k)) && !(busy & (1u << k))) crtc = res->crtcs[k];
    LOG("fd %d connector %u crtc %u possible 0x%x busy 0x%x current %u mode %dx%d", fd, conn_id, crtc, possible, busy, cur, W, H);
    if (!crtc) { LOG("no free CRTC"); drmModeFreeConnector(conn); drmModeFreeResources(res); return 0; }

    struct drm_mode_create_dumb cd = { .width = W, .height = H, .bpp = 32 };
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd)) { LOG("create dumb failed"); return 0; }
    uint32_t handles[4] = { cd.handle }, pitches[4] = { cd.pitch }, offsets[4] = { 0 }, fb = 0;
    uint32_t fmt = 0x34324258; /* XBGR8888: bytes R,G,B,X */
    if (drmModeAddFB2(fd, W, H, fmt, handles, pitches, offsets, &fb, 0)) {
        fmt = 0x34324241; /* ABGR8888 */
        if (drmModeAddFB2(fd, W, H, fmt, handles, pitches, offsets, &fb, 0)) { LOG("addfb2 failed"); return 0; }
    }
    struct drm_mode_map_dumb md = { .handle = cd.handle };
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md)) { LOG("map dumb failed"); return 0; }
    void *m = mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, md.offset);
    if (m == MAP_FAILED) { LOG("mmap failed"); return 0; }
    memset(m, 0, cd.size);
    uint32_t cids[1] = { conn_id };
    if (drmModeSetCrtc(fd, crtc, fb, 0, 0, cids, 1, &mode)) { LOG("setcrtc failed"); return 0; }
    G.fd = fd; G.crtc = crtc; G.fb = fb; G.pitch = cd.pitch; G.size = cd.size; G.px = m; G.W = W; G.H = H;
    drmModeFreeConnector(conn); drmModeFreeResources(res);
    LOG("running: %dx%d pitch %u fourcc 0x%x, every %u ms", W, H, G.pitch, fmt, G.interval);
    return 1;
}

static void init_once(void) {
    if (G.tried) return;
    G.tried = 1;
    const char *e = getenv("BANANA_SECOND");
    if (e && !strcmp(e, "0")) { LOG("disabled by env"); G.failed = 1; return; }
    e = getenv("BANANA_SECOND_MS");
    G.interval = e ? (uint32_t)atoi(e) : 40;
    e = getenv("BANANA_KEEP_POINTER");
    G.keep_pointer = !(e && !strcmp(e, "0"));
    e = getenv("BANANA_AUTOCLICK");
    G.autoclick = !(e && !strcmp(e, "0"));
    e = getenv("BANANA_RETURN_MS");
    G.return_ms = e ? atoi(e) : 250; /* the game reads button+position on its next tick: the pointer must stay on the verb until then */
    for (int v = 0; v < 65536; v++) {
        uint32_t r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
        r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
        G.lut565[v] = 0xff000000u | (b << 16) | (g << 8) | r; /* bytes R,G,B,A */
    }
    if (!panel_setup()) { G.failed = 1; return; }
    G.ok = 1;
}

/* ---------------- split configuration ---------------- */
static int lookup_roomline(const char *title) {
    const char *file = getenv("BANANA_SPLIT_FILE");
    int found = 0, any = 0;
    if (file) {
        FILE *f = fopen(file, "r");
        if (f) {
            char line[300];
            while (fgets(line, sizeof line, f)) {
                char *eq = strrchr(line, '=');
                if (!eq || line[0] == '#') continue;
                *eq = 0; any = 1;
                if (*line && strcasestr(title, line)) found = atoi(eq + 1);
            }
            fclose(f);
        }
    }
    if (!any && strcasestr(title, "Monkey Island")) found = 144;
    return found > 0 && found < 200 ? found : 0;
}

static void set_mode(int m) {
    if (m != G.mode) { G.mode = m; G.dst_changed = 1; G.dirty = 1; G.view_init = 0; LOG("mode %s", m == 1 ? "split" : m == 2 ? "zoom" : "mirror"); }
}

void SDL_SetWindowTitle(SDL_Window *w, const char *title) {
    static void (*real)(SDL_Window *, const char *);
    if (!real) real = dlsym(RTLD_NEXT, "SDL_SetWindowTitle");
    real(w, title);
    snprintf(G.title, sizeof G.title, "%s", title ? title : "");
    G.roomline = lookup_roomline(G.title);
    LOG("window title \"%s\" -> %s", G.title, G.roomline ? "split available" : "mirror only");
    set_mode(G.roomline ? 1 : 0);
}

/* ---------------- frame capture ---------------- */
int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch) {
    static int (*real)(SDL_Texture *, const SDL_Rect *, const void *, int);
    if (!real) real = dlsym(RTLD_NEXT, "SDL_UpdateTexture");
    int r = real(texture, rect, pixels, pitch);
    if (r == 0 && !rect && pixels && !G.failed) {
        uint32_t fmt; int access, w, h;
        if (SDL_QueryTexture(texture, &fmt, &access, &w, &h) == 0 && access == SDL_TEXTUREACCESS_STREAMING &&
            fmt == SDL_PIXELFORMAT_RGB565) {
            size_t need = (size_t)w * h * 2;
            if (need > G.bufsize) { free(G.buf); G.buf = malloc(need); G.bufsize = G.buf ? need : 0; }
            if (G.buf) {
                for (int y = 0; y < h; y++) memcpy(G.buf + (size_t)y * w * 2, (const uint8_t *)pixels + (size_t)y * pitch, (size_t)w * 2);
                if (G.sw != w || G.sh != h) G.dst_changed = 1;
                G.tex = texture; G.sw = w; G.sh = h; G.dirty = 1;
            }
        }
    }
    return r;
}

static int split_active(void) { return G.mode == 1 && G.roomline > 0 && G.sh > 0 && G.sw > 0; }
static int zoom_active(void) { return G.mode == 2 && G.sh > 0 && G.sw > 0; }
static const float zoom_levels[3] = { 1.5f, 2.0f, 3.0f };

/* the part of the picture shown on the top panel in zoom mode: it stays still until the pointer nears its edge */
static void zoom_update_view(void) {
    float z = zoom_levels[G.zi % 3];
    int vw = (int)(G.sw / z), vh = (int)(G.sh / z);
    int tx = G.sw / 2, ty = G.sh / 2;
    if (G.have_ptr && G.dst.w > 0 && G.dst.h > 0) {
        tx = (G.ptr_x - G.dst.x) * G.sw / G.dst.w;
        ty = (G.ptr_y - G.dst.y) * G.sh / G.dst.h;
    }
    int ox = G.view_x, oy = G.view_y;
    if (!G.view_init || vw != G.view_w || vh != G.view_h) { G.view_x = tx - vw / 2; G.view_y = ty - vh / 2; G.view_init = 1; }
    G.view_w = vw; G.view_h = vh;
    int mx = vw / 5, my = vh / 5;
    if (tx < G.view_x + mx) G.view_x = tx - mx;
    if (tx > G.view_x + vw - mx) G.view_x = tx - vw + mx;
    if (ty < G.view_y + my) G.view_y = ty - my;
    if (ty > G.view_y + vh - my) G.view_y = ty - vh + my;
    if (G.view_x < 0) G.view_x = 0;
    if (G.view_y < 0) G.view_y = 0;
    if (G.view_x > G.sw - vw) G.view_x = G.sw - vw;
    if (G.view_y > G.sh - vh) G.view_y = G.sh - vh;
    if (ox != G.view_x || oy != G.view_y) G.dirty = 1;
}

static int roomy_tex(void) { return G.roomline * G.sh / 200; }

int SDL_RenderCopy(SDL_Renderer *rd, SDL_Texture *texture, const SDL_Rect *src, const SDL_Rect *dst) {
    static int (*real)(SDL_Renderer *, SDL_Texture *, const SDL_Rect *, const SDL_Rect *);
    if (!real) real = dlsym(RTLD_NEXT, "SDL_RenderCopy");
    if (texture == G.tex && !src && dst) {
        if (!G.have_dst || memcmp(&G.dst, dst, sizeof *dst)) { G.dst = *dst; G.have_dst = 1; G.dst_changed = 1; }
        SDL_GetRendererOutputSize(rd, &G.outw, &G.outh);
        G.win = SDL_RenderGetWindow(rd);
        if (split_active() && G.outw > 0 && G.outh > 0) {
            int ry = roomy_tex();
            SDL_Rect s = { 0, 0, G.sw, ry };
            float sc = (float)G.outw / G.sw;
            if ((float)G.outh / ry < sc) sc = (float)G.outh / ry;
            int w = (int)(G.sw * sc), h = (int)(ry * sc);
            SDL_Rect d = { (G.outw - w) / 2, (G.outh - h) / 2, w, h };
            return real(rd, texture, &s, &d);
        }
        if (zoom_active()) {
            zoom_update_view();
            SDL_Rect s = { G.view_x, G.view_y, G.view_w, G.view_h };
            return real(rd, texture, &s, dst);
        }
    }
    return real(rd, texture, src, dst);
}

/* scale a rectangle of the captured frame into a rectangle of the bottom panel */
static void blit(int sx0, int sy0, int sw, int sh, int dx, int dy, int dw, int dh) {
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    if (G.xmap_n < dw) { free(G.xmap); G.xmap = malloc(sizeof(int) * (size_t)dw); G.xmap_n = G.xmap ? dw : 0; if (!G.xmap) return; }
    for (int x = 0; x < dw; x++) G.xmap[x] = sx0 + x * sw / dw;
    int x0 = dx < 0 ? -dx : 0, x1 = dx + dw > G.W ? G.W - dx : dw;
    for (int y = 0; y < dh; y++) {
        int py = dy + y;
        if (py < 0 || py >= G.H) continue;
        const uint16_t *s = (const uint16_t *)(G.buf + (size_t)(sy0 + y * sh / dh) * G.sw * 2);
        uint32_t *d = (uint32_t *)((uint8_t *)G.px + (size_t)py * G.pitch) + dx;
        for (int x = x0; x < x1; x++) d[x] = G.lut565[s[G.xmap[x]]];
    }
}

static void draw_frame(void) {
    if (G.dst_changed) { memset(G.px, 0, G.size); G.dst_changed = 0; }
    if (split_active()) {
        int ry = roomy_tex(), rh = G.sh - ry;
        float sc = (float)G.W / G.sw;
        if ((float)G.H / rh < sc) sc = (float)G.H / rh;
        int w = (int)(G.sw * sc), h = (int)(rh * sc);
        G.db = (SDL_Rect){ (G.W - w) / 2, (G.H - h) / 2, w, h };
        G.roomy = ry;
        blit(0, ry, G.sw, rh, G.db.x, G.db.y, G.db.w, G.db.h);
    } else {
        int ow = G.outw > 0 ? G.outw : G.W, oh = G.outh > 0 ? G.outh : G.H;
        int dx = G.dst.x * G.W / ow, dy = G.dst.y * G.H / oh, dw = G.dst.w * G.W / ow, dh = G.dst.h * G.H / oh;
        blit(0, 0, G.sw, G.sh, dx, dy, dw, dh);
        if (zoom_active() && G.view_w > 0 && dw > 0 && dh > 0) {
            /* yellow frame around the part the top panel shows */
            int x0 = dx + G.view_x * dw / G.sw, y0 = dy + G.view_y * dh / G.sh;
            int x1 = x0 + G.view_w * dw / G.sw - 1, y1 = y0 + G.view_h * dh / G.sh - 1;
            for (int t = 0; t < 3; t++)
                for (int x = x0; x <= x1; x++) {
                    int ys[2] = { y0 + t, y1 - t };
                    for (int k = 0; k < 2; k++) if (x >= 0 && x < G.W && ys[k] >= 0 && ys[k] < G.H)
                        ((uint32_t *)((uint8_t *)G.px + (size_t)ys[k] * G.pitch))[x] = 0xff00ffffu;
                }
            for (int t = 0; t < 3; t++)
                for (int y = y0; y <= y1; y++) {
                    int xs[2] = { x0 + t, x1 - t };
                    for (int k = 0; k < 2; k++) if (y >= 0 && y < G.H && xs[k] >= 0 && xs[k] < G.W)
                        ((uint32_t *)((uint8_t *)G.px + (size_t)y * G.pitch))[xs[k]] = 0xff00ffffu;
                }
        }
    }
}

static void dump_frame(void) {
    if (!G.buf) return;
    FILE *f = fopen("/tmp/banana-frame.ppm", "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", G.sw, G.sh);
    for (int i = 0; i < G.sw * G.sh; i++) {
        uint16_t v = ((uint16_t *)G.buf)[i];
        uint8_t rgb[3] = { (uint8_t)(((v >> 11) & 31) * 255 / 31), (uint8_t)(((v >> 5) & 63) * 255 / 63), (uint8_t)((v & 31) * 255 / 31) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    LOG("frame dumped to /tmp/banana-frame.ppm (%dx%d), title \"%s\"", G.sw, G.sh, G.title);
}

void SDL_RenderPresent(SDL_Renderer *rd) {
    static void (*real)(SDL_Renderer *);
    if (!real) real = dlsym(RTLD_NEXT, "SDL_RenderPresent");
    int shooting = G.shot_req && (int32_t)(SDL_GetTicks() - G.shot_after) >= 0 && G.have_dst;
    if (shooting) {
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(rd, &w, &h);
        uint8_t *tmp = malloc((size_t)w * h * 3);
        if (tmp && SDL_RenderReadPixels(rd, NULL, SDL_PIXELFORMAT_RGB24, tmp, w * 3) == 0) {
            FILE *f = fopen("/tmp/banana-top.ppm", "wb");
            if (f) { fprintf(f, "P6\n%d %d\n255\n", w, h); fwrite(tmp, 1, (size_t)w * h * 3, f); fclose(f); }
            G.shot_req = 0; G.shot_bottom = 1; G.dirty = 1; G.last_ms = 0; /* bottom panel is drawn below, then saved */
        } else { LOG("shot: top readback failed (%s)", SDL_GetError()); G.shot_req = 0; }
        free(tmp);
    }
    real(rd);
    if (G.failed) return;
    init_once();
    if (!G.ok) return;
    if ((++G.presents % 30) == 0 && access("/tmp/banana-dump", F_OK) == 0) { dump_frame(); unlink("/tmp/banana-dump"); }
    if (!G.dirty || !G.have_dst || !G.buf) return;
    uint32_t now = SDL_GetTicks();
    if (now - G.last_ms < G.interval) return;
    G.last_ms = now;
    G.dirty = 0;
    draw_frame();
    if (G.shot_bottom) {
        FILE *f = fopen("/tmp/banana-bottom.ppm", "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", G.W, G.H);
            for (int y = 0; y < G.H; y++) {
                const uint8_t *row = (const uint8_t *)G.px + (size_t)y * G.pitch;
                for (int x = 0; x < G.W; x++) fwrite(row + x * 4, 1, 3, f);
            }
            fclose(f);
            LOG("shot saved (mode %d, zoom %.1fx)", G.mode, zoom_levels[G.zi % 3]);
        }
        G.shot_bottom = 0;
    }
    uint32_t t = SDL_GetTicks() - now;
    G.frames++; G.tsum += t;
    if (now - G.tlog > 5000) {
        G.tlog = now;
        LOG("%s: %llu frames, avg %.2f ms/frame, src %dx%d dst %d,%d %dx%d out %dx%d",
            split_active() ? "split" : "mirror", (unsigned long long)G.frames, G.frames ? (double)G.tsum / G.frames : 0.0,
            G.sw, G.sh, G.dst.x, G.dst.y, G.dst.w, G.dst.h, G.outw, G.outh);
    }
}

/* ---------------- touch / keys ---------------- */
/* bottom panel pixel -> normalized window coordinates that ScummVM turns into the matching game point */
static void touch_remap(float *nx, float *ny) {
    if (!split_active() || !G.ok || !G.have_dst || G.db.w <= 0 || G.outw <= 0) return;
    float px = *nx * G.W, py = *ny * G.H;
    float u = (px - G.db.x) / G.db.w, v = (py - G.db.y) / G.db.h;
    u = u < 0 ? 0 : u > 0.999f ? 0.999f : u;
    v = v < 0 ? 0 : v > 0.999f ? 0.999f : v;
    float tx = u * G.sw, ty = G.roomy + v * (G.sh - G.roomy);
    float wx = G.dst.x + tx * G.dst.w / G.sw, wy = G.dst.y + ty * G.dst.h / G.sh;
    *nx = wx / G.outw; *ny = wy / G.outh;
}

static void hist_add(int x, int y) {
    uint32_t now = SDL_GetTicks();
    if (G.nfingers > 0 || (int32_t)(now - G.quiet_until) < 0) return; /* touch's own mouse events are not the stick pointer */
    G.hist[G.hist_n++ & 63].ms = now;
    G.hist[(G.hist_n - 1) & 63].x = x; G.hist[(G.hist_n - 1) & 63].y = y;
}

/* newest pointer position that is at least 40 ms older than 'now' (the touch's own mouse events can precede its finger event) */
static int hist_before(uint32_t now, int *x, int *y) {
    for (int i = 0; i < 64 && i < G.hist_n; i++) {
        int k = (G.hist_n - 1 - i) & 63;
        if ((int32_t)(now - G.hist[k].ms) >= 40) { *x = G.hist[k].x; *y = G.hist[k].y; return 1; }
    }
    return 0;
}

int SDL_PollEvent(SDL_Event *ev) {
    static int (*real)(SDL_Event *);
    if (!real) real = dlsym(RTLD_NEXT, "SDL_PollEvent");
    int r;
    {
        static uint32_t last_chk;
        uint32_t t = SDL_GetTicks();
        if (t - last_chk > 150) {
            last_chk = t;
            FILE *f = fopen("/tmp/banana-mode", "r");
            if (f) { int m = -1, z = -1; if (fscanf(f, "%d %d", &m, &z) >= 1 && m >= 0 && m <= 2) {
                        if (z >= 0 && z < 3) { G.zi = z; G.view_init = 0; }
                        if (m == 1 && !G.roomline) LOG("remote: no split for this title"); else set_mode(m);
                        G.nudge = 6; }
                     fclose(f); unlink("/tmp/banana-mode"); }
            if (access("/tmp/banana-shot", F_OK) == 0) { unlink("/tmp/banana-shot"); G.shot_req = 1; G.shot_after = t + 600; G.nudge = 6; }
        }
        if (G.nudge > 0 && ev) { /* a 1 px wiggle of the pointer makes ScummVM redraw even in a still scene */
            int px = G.have_ptr ? G.ptr_x : 512, py = G.have_ptr ? G.ptr_y : 384;
            memset(ev, 0, sizeof *ev);
            ev->type = SDL_MOUSEMOTION; ev->motion.timestamp = t; ev->motion.windowID = G.win ? SDL_GetWindowID(G.win) : 0;
            ev->motion.x = px + ((G.nudge & 1) ? 1 : 0); ev->motion.y = py;
            G.nudge--;
            return 1;
        }
    }
    if (G.stage && !split_active()) G.stage = 0;
    if (G.stage && ev && (int32_t)(SDL_GetTicks() - G.ret_due) >= 0) {
        uint32_t now = SDL_GetTicks();
        memset(ev, 0, sizeof *ev);
        ev->motion.timestamp = now;
        ev->motion.windowID = G.win ? SDL_GetWindowID(G.win) : 0;
        if (G.stage == 1) {
            /* 1: put the pointer back where it was */
            int ty = G.dst.h > 0 ? (G.cur_y - G.dst.y) * G.sh / G.dst.h : 0;
            LOG("pointer back to %d,%d%s", G.cur_x, G.cur_y, G.autoclick && ty < G.roomy ? ", then one click" : "");
            G.hist[G.hist_n++ & 63].ms = now - 100; G.hist[(G.hist_n - 1) & 63].x = G.cur_x; G.hist[(G.hist_n - 1) & 63].y = G.cur_y;
            if (G.win) SDL_WarpMouseInWindow(G.win, G.cur_x, G.cur_y); /* keep SDL's own position in step */
            ev->type = SDL_MOUSEMOTION;
            ev->motion.x = G.cur_x; ev->motion.y = G.cur_y;
            /* 2: only click if the pointer was over the room (not on the verbs) */
            G.stage = (G.autoclick && ty < G.roomy) ? 2 : 0;
            G.ret_due = now + 60;
        } else {
            ev->type = G.stage == 2 ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            ev->button.button = SDL_BUTTON_LEFT;
            ev->button.state = G.stage == 2 ? SDL_PRESSED : SDL_RELEASED;
            ev->button.clicks = 1;
            ev->button.x = G.cur_x; ev->button.y = G.cur_y;
            G.stage = G.stage == 2 ? 3 : 0;
            G.ret_due = now + 70;
        }
        return 1;
    }
    while ((r = real(ev))) {
        if (!ev) return r;
        {
            static int nlog;
            if (nlog < 600) {
                if (ev->type == SDL_FINGERDOWN || ev->type == SDL_FINGERUP) { nlog++; LOG("ev %s raw %.3f,%.3f mode=%d", ev->type == SDL_FINGERDOWN ? "FINGERDOWN" : "FINGERUP", ev->tfinger.x, ev->tfinger.y, G.mode); }
                else if (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP) { nlog++; LOG("ev MOUSE%s button %d which %u at %d,%d", ev->type == SDL_MOUSEBUTTONDOWN ? "DOWN" : "UP", ev->button.button, (unsigned)ev->button.which, ev->button.x, ev->button.y); }
            }
        }
        if (ev->type == SDL_MOUSEMOTION) { G.ptr_x = ev->motion.x; G.ptr_y = ev->motion.y; G.have_ptr = 1; }
        else if (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP) { G.ptr_x = ev->button.x; G.ptr_y = ev->button.y; G.have_ptr = 1; }
        /* touches arrive as finger events (ScummVM turns them into pointer moves itself): the zoom must follow those too */
        if ((ev->type == SDL_FINGERDOWN || ev->type == SDL_FINGERMOTION || ev->type == SDL_FINGERUP) && G.outw > 0 && G.outh > 0 && !split_active()) {
            G.ptr_x = (int)(ev->tfinger.x * G.outw); G.ptr_y = (int)(ev->tfinger.y * G.outh); G.have_ptr = 1;
        }
        if (ev->type == SDL_MOUSEMOTION) hist_add(ev->motion.x, ev->motion.y);
        else if (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP) hist_add(ev->button.x, ev->button.y);
        /* F9 (a gamepad button via gptokeyb) becomes '.', which skips a line of dialogue in SCUMM games */
        if ((ev->type == SDL_KEYDOWN || ev->type == SDL_KEYUP) && ev->key.keysym.scancode == SDL_SCANCODE_F9) {
            ev->key.keysym.scancode = SDL_SCANCODE_PERIOD; ev->key.keysym.sym = SDLK_PERIOD;
        }
        /* F7 (a gamepad button via gptokeyb) opens ScummVM's on-screen keyboard, which listens for Ctrl+F7:
         * ScummVM reads the modifiers from SDL_GetModState(), so Ctrl is set while the key is down */
        if ((ev->type == SDL_KEYDOWN || ev->type == SDL_KEYUP) && ev->key.keysym.scancode == SDL_SCANCODE_F7) {
            LOG("F7 %s -> Ctrl+F7", ev->type == SDL_KEYDOWN ? "down" : "up");
            SDL_SetModState(ev->type == SDL_KEYDOWN ? KMOD_LCTRL : KMOD_NONE);
            ev->key.keysym.mod = ev->type == SDL_KEYDOWN ? KMOD_LCTRL : KMOD_NONE;
        }
        if ((ev->type == SDL_KEYDOWN || ev->type == SDL_KEYUP) && ev->key.keysym.scancode == SDL_SCANCODE_F10) {
            if (ev->type == SDL_KEYDOWN && !ev->key.repeat) {
                int next = (G.mode + 1) % 3;
                if (next == 1 && !G.roomline) next = 2; /* no verb layout known for this game */
                set_mode(next);
            }
            continue; /* consumed */
        }
        if ((ev->type == SDL_KEYDOWN || ev->type == SDL_KEYUP) && ev->key.keysym.scancode == SDL_SCANCODE_F8) {
            if (ev->type == SDL_KEYDOWN && !ev->key.repeat) { G.zi = (G.zi + 1) % 3; G.view_init = 0; G.dirty = 1; LOG("zoom %.1fx", zoom_levels[G.zi]); }
            continue; /* consumed */
        }
        if (split_active()) {
            if (ev->type == SDL_FINGERDOWN || ev->type == SDL_FINGERUP || ev->type == SDL_FINGERMOTION) {
                touch_remap(&ev->tfinger.x, &ev->tfinger.y);
                if (ev->type != SDL_FINGERMOTION) LOG("  remapped to %.3f,%.3f (window %d,%d)", ev->tfinger.x, ev->tfinger.y, (int)(ev->tfinger.x * G.outw), (int)(ev->tfinger.y * G.outh));
                if (G.keep_pointer) {
                    uint32_t now = SDL_GetTicks();
                    if (ev->type == SDL_FINGERDOWN) {
                        if (G.nfingers == 0) G.have_cur = hist_before(now, &G.cur_x, &G.cur_y);
                        G.nfingers++; if (G.stage == 1) G.stage = 0;
                    } else if (ev->type == SDL_FINGERUP) {
                        if (G.nfingers > 0) G.nfingers--;
                        G.quiet_until = now + (G.return_ms > 0 ? G.return_ms : 300) + 150;
                        if (G.nfingers == 0) {
                            LOG("tap released, pointer parked at %d,%d (known %d)", G.cur_x, G.cur_y, G.have_cur);
                            if (G.have_cur) { G.stage = 1; G.ret_due = now + (G.return_ms > 0 ? G.return_ms : 300); }
                        }
                    }
                }
            }
            else if ((ev->type == SDL_MOUSEMOTION && ev->motion.which == SDL_TOUCH_MOUSEID) ||
                     ((ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP) && ev->button.which == SDL_TOUCH_MOUSEID)) {
                int *x = ev->type == SDL_MOUSEMOTION ? &ev->motion.x : &ev->button.x;
                int *y = ev->type == SDL_MOUSEMOTION ? &ev->motion.y : &ev->button.y;
                if (G.outw > 0 && G.outh > 0) {
                    float nx = (float)*x / G.outw, ny = (float)*y / G.outh;
                    touch_remap(&nx, &ny);
                    *x = (int)(nx * G.outw); *y = (int)(ny * G.outh);
                }
            }
        }
        return r;
    }
    return 0;
}

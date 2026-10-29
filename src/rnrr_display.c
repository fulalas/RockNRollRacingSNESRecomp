#include "rnrr_display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include "ppu_dma_trace.h"
#include "snes/dma.h"
#include "snes/cart.h"
#include "rnrr_ws_cull.h"
#include "desktop/config.h"
#include "desktop/display_aspect.h"
#include "desktop/host_clock.h"
#include "desktop/sdl_compat.h"
#include "snes_osd.h"
#include "snes_overlay_draw.h"

enum {
    HUD_LEFT_END = 84,
    HUD_RIGHT_START = 186,
    INTERP_MAX_SCROLL = 48,
    INTERP_MAX_OBJ = 24,
    KEY_PERIOD_MAX = 3,
};

volatile uint16_t g_rnrr_cull_left = 0xffc0;
volatile uint16_t g_rnrr_cull_right = 0x100;
volatile uint8_t g_rnrr_oc_fast = 6;
volatile uint8_t g_rnrr_oc_slow = 8;
static unsigned s_overclock = 100;

static unsigned s_overclock_live = 100;

static void overclock_apply(unsigned percent)
{
    if (percent == s_overclock_live) return;
    s_overclock_live = percent;
    unsigned fast = (600 + percent / 2) / percent, slow = (800 + percent / 2) / percent;
    g_rnrr_oc_fast = (uint8_t)(fast ? fast : 1);
    g_rnrr_oc_slow = (uint8_t)(slow ? slow : 1);
    g_interp_bridge_master_scale_pct = percent;
}

void RnrrSetOverclock(unsigned percent)
{
    if (percent < 100) percent = 100;
    if (percent > 400) percent = 400;
    s_overclock = percent;
}

unsigned RnrrOverclock(void)
{
    return s_overclock;
}
static int s_cull_extra = -1;

static void apply_cull_window(int extra)
{
    static const uint16_t sites[][2] = {
        {0xFF11, 0}, {0xFF16, 1}, {0xFF31, 0}, {0xFF36, 1}, {0xFF61, 0}, {0xFF66, 1},
    };
    if (extra == s_cull_extra) return;
    s_cull_extra = extra;
    uint16_t left = (uint16_t)(0xffc0 - extra), right = (uint16_t)(0x100 + extra);
    g_rnrr_cull_left = left;
    g_rnrr_cull_right = right;
    if (!g_snes || !g_snes->cart) return;
    for (size_t i = 0; i < sizeof(sites) / sizeof(sites[0]); i++) {
        uint8_t *p = cart_getRomPtr(g_snes->cart, 0x01, sites[i][0]);
        if (!p) continue;
        uint16_t cur = (uint16_t)(p[0] | (p[1] << 8));
        uint16_t base = sites[i][1] ? 0x100 : 0xffc0;
        int plausible = sites[i][1] ? (cur >= 0x100 && cur <= 0x100 + 272) : (cur <= 0xffc0 && cur >= 0xffc0 - 272);
        if (!plausible) continue;
        uint16_t v = sites[i][1] ? right : left;
        (void)base;
        p[0] = (uint8_t)v;
        p[1] = (uint8_t)(v >> 8);
    }
}

static int s_ws_mode = RNRR_WS_OFF;
static int s_fps_enabled;
static unsigned s_fps;
static int s_frame_w = 256;
static int s_extra;
static int s_race_frame;

typedef union {
    uint8_t b[PPU_SAVESTATE_REGS_SIZE];
    uint64_t align[PPU_SAVESTATE_REGS_SIZE / 8];
} RegBlock;

typedef struct {
    RegBlock regs[225];
    uint16_t oam[0x100];
    uint8_t high_oam[0x20];
    unsigned number;
    int valid;
    int extra;
} FrameRecord;

static FrameRecord s_rec[2];
static FrameRecord s_key[2];
static int s_key_cur;
static unsigned s_key_field;
static unsigned s_key_period = 1;
static uint8_t s_key_hist[8];
static unsigned s_key_hist_pos;

static unsigned key_period_mode(void)
{
    unsigned best = 1, best_n = 0;
    for (unsigned p = 1; p <= KEY_PERIOD_MAX; p++) {
        unsigned n = 0;
        for (unsigned i = 0; i < 8; i++) n += s_key_hist[i] == p;
        if (n > best_n) { best_n = n; best = p; }
    }
    return best;
}

static void key_reset(void)
{
    s_key[0].valid = s_key[1].valid = 0;
    s_key_period = 1;
    memset(s_key_hist, 0, sizeof(s_key_hist));
}

static struct {
    unsigned sim, upd, out, lerp;
    unsigned r_sim, r_upd, r_out, r_lerp;
    Uint64 start;
} s_perf;

static void perf_overlay(uint8_t *dst, size_t pitch, int frame_w, int frame_h, int lerped)
{
    s_perf.out++;
    if (lerped) s_perf.lerp++;
    Uint64 now = SDL_GetPerformanceCounter(), freq = SDL_GetPerformanceFrequency();
    if (!s_perf.start) s_perf.start = now;
    if (freq && now - s_perf.start >= freq) {
        double sec = (double)(now - s_perf.start) / (double)freq;
        s_perf.r_sim = (unsigned)(s_perf.sim / sec + 0.5);
        s_perf.r_upd = (unsigned)(s_perf.upd / sec + 0.5);
        s_perf.r_out = (unsigned)(s_perf.out / sec + 0.5);
        s_perf.r_lerp = (unsigned)(s_perf.lerp / sec + 0.5);
        s_perf.sim = s_perf.upd = s_perf.out = s_perf.lerp = 0;
        s_perf.start = now;
    }
    if (!snes_osd_fps_visible()) return;
    char text[64];
    snprintf(text, sizeof(text), "GAME %u  OUT %u  LERP %u  SIM %u  OC %u",
             s_perf.r_upd, s_perf.r_out, s_perf.r_lerp, s_perf.r_sim, s_overclock_live);
    int w = (int)strlen(text) * 8 + 4;
    if (w > frame_w) w = frame_w;
    snes_ovl_fill_rect((uint32_t *)dst, (int)(pitch / 4), frame_h, 2, 16, w, 12, 0xC0202020u);
    snes_ovl_draw_text((uint32_t *)dst, (int)(pitch / 4), frame_h, 4, 18, text, 0xFFFFFFFFu, 1);
}

static void copy_field(uint8_t *dst, size_t pitch, const uint8_t *field, int w, int h)
{
    for (int y = 0; y < h; y++)
        memcpy(dst + (size_t)y * pitch, field + (size_t)y * w * 4, (size_t)w * 4);
}
static int s_cur;
static Ppu *s_snap;
static Ppu *s_scratch;
static int s_snap_valid;

void RnrrDisplaySetWidescreen(int mode)
{
    s_ws_mode = mode >= RNRR_WS_OFF && mode <= RNRR_WS_FIT ? mode : RNRR_WS_OFF;
}

int RnrrDisplayWidescreenMode(void)
{
    return s_ws_mode;
}

void RnrrDisplaySetPresentationFps(int enabled, unsigned fps)
{
    s_fps_enabled = enabled != 0;
    s_fps = snes_host_valid_fps(fps) ? fps : 0;
}

int RnrrDisplayPresentationEnabled(void)
{
    return s_fps_enabled;
}

static double target_aspect(int drawable_w, int drawable_h)
{
    switch (s_ws_mode) {
    case RNRR_WS_16_9: return 16.0 / 9.0;
    case RNRR_WS_21_9: return 21.0 / 9.0;
    case RNRR_WS_32_9: return 32.0 / 9.0;
    case RNRR_WS_FIT:
        if (drawable_w > 0 && drawable_h > 0)
            return (double)drawable_w / drawable_h;
        return 16.0 / 9.0;
    default: return 4.0 / 3.0;
    }
}


enum {
    CACHE_VIEWS = 2,
    CACHE_LAYERS = 2,
    CACHE_W = 1024,
    CACHE_H = 1024,
    MAX_VIEW_GROUPS = 4,
};

typedef struct {
    uint16_t *entry;
    uint8_t *valid;
    uint8_t base;
} TileStore;

typedef struct {
    int y0, y1;
    int view;
    int off_y[CACHE_LAYERS];
    uint16_t h[CACHE_LAYERS], v[CACHE_LAYERS];
    uint8_t sc[CACHE_LAYERS];
} ViewGroup;

static TileStore s_store[CACHE_VIEWS][CACHE_LAYERS];
typedef struct { uint16_t tx, ty; uint8_t view, valid; } CellTag;
static CellTag s_tag[CACHE_LAYERS][64 * 64];
static uint8_t s_tag_base[CACHE_LAYERS];
static uint16_t *s_prev_vram;
static int s_prev_vram_valid;
static int s_blank_tile[CACHE_LAYERS];
static uint16_t s_blank_tiles_adr[CACHE_LAYERS];
static ViewGroup s_groups[MAX_VIEW_GROUPS];
static int s_ngroups;
static uint16_t *s_vram[2];
static int s_vram_cur;
static uint32_t s_cam[CACHE_VIEWS][2];
typedef struct { int valid; uint16_t h, v; uint32_t fx, fy; } CamTrack;
static CamTrack s_track[CACHE_VIEWS];
static uint64_t s_stat_cam_fallback;
static int s_cache_enabled = 1;
static uint64_t s_stat_learned, s_stat_served, s_stat_blanked;
static int s_map_left[CACHE_LAYERS], s_map_right[CACHE_LAYERS];
static unsigned s_map_extent_frame;

static int store_alloc(TileStore *st)
{
    if (st->entry) return 1;
    st->entry = calloc((size_t)CACHE_W * CACHE_H, sizeof(uint16_t));
    st->valid = calloc((size_t)CACHE_W * CACHE_H / 8, 1);
    if (!st->entry || !st->valid) { free(st->entry); free(st->valid); st->entry = NULL; st->valid = NULL; return 0; }
    return 1;
}

static void store_clear(TileStore *st)
{
    if (st->valid) memset(st->valid, 0, (size_t)CACHE_W * CACHE_H / 8);
}

static void cache_reset(void)
{
    for (int v = 0; v < CACHE_VIEWS; v++)
        for (int l = 0; l < CACHE_LAYERS; l++)
            store_clear(&s_store[v][l]);
    memset(s_tag, 0, sizeof(s_tag));
    memset(s_track, 0, sizeof(s_track));
    s_ngroups = 0;
    s_prev_vram_valid = 0;
    s_map_extent_frame = 0;
    for (int l = 0; l < CACHE_LAYERS; l++) { s_blank_tile[l] = -1; s_blank_tiles_adr[l] = 0xffff; }
}

static void read_cameras(void)
{
    s_cam[0][0] = g_ram[0x18] | (g_ram[0x19] << 8);
    s_cam[0][1] = g_ram[0x1a] | (g_ram[0x1b] << 8);
    s_cam[1][0] = g_ram[0x1c] | (g_ram[0x1d] << 8);
    s_cam[1][1] = g_ram[0x1e] | (g_ram[0x1f] << 8);
}

static int wrap_delta(int cur, int prev)
{
    return ((cur - prev + 512) & 1023) - 512;
}

static int view_for_scroll(uint16_t h, uint16_t v)
{
    for (int i = 0; i < CACHE_VIEWS; i++)
        if ((s_cam[i][0] & 0x3ff) == h && (s_cam[i][1] & 0x3ff) == v) {
            s_track[i].valid = 1; s_track[i].h = h; s_track[i].v = v;
            s_track[i].fx = s_cam[i][0]; s_track[i].fy = s_cam[i][1];
            return i;
        }
    for (int i = 0; i < CACHE_VIEWS; i++)
        if ((s_cam[i][0] & 0x3ff) == h) {
            s_track[i].valid = 1; s_track[i].h = h; s_track[i].v = v;
            s_track[i].fx = s_cam[i][0]; s_track[i].fy = s_cam[i][1];
            return i;
        }
    int best = -1, best_d = 1 << 30;
    for (int i = 0; i < CACHE_VIEWS; i++) {
        if (!s_track[i].valid) continue;
        int dh = wrap_delta(h, s_track[i].h), dv = wrap_delta(v, s_track[i].v);
        int d = dh * dh + dv * dv;
        if (d < best_d) { best_d = d; best = i; }
    }
    if (best < 0 || best_d > 64 * 64) return -1;
    s_track[best].fx = (uint32_t)((int)s_track[best].fx + wrap_delta(h, s_track[best].h));
    s_track[best].fy = (uint32_t)((int)s_track[best].fy + wrap_delta(v, s_track[best].v));
    s_track[best].h = h; s_track[best].v = v;
    s_cam[best][0] = s_track[best].fx;
    s_cam[best][1] = s_track[best].fy;
    s_stat_cam_fallback++;
    return best;
}

static int floor8_signed(int d)
{
    d = ((d + 512) & 1023) - 512;
    return d & ~7;
}

static inline size_t cell_index(uint32_t tx, uint32_t ty)
{
    return (size_t)(ty & (CACHE_H - 1)) * CACHE_W + (tx & (CACHE_W - 1));
}

static inline void store_put(TileStore *st, uint32_t tx, uint32_t ty, uint16_t e)
{
    size_t i = cell_index(tx, ty);
    st->entry[i] = e;
    st->valid[i >> 3] |= (uint8_t)(1u << (i & 7));
}

static inline int store_get(const TileStore *st, uint32_t tx, uint32_t ty, uint16_t *e)
{
    size_t i = cell_index(tx, ty);
    if (!(st->valid[i >> 3] & (1u << (i & 7)))) return 0;
    *e = st->entry[i];
    return 1;
}

static inline uint16_t tilemap_word(uint8_t sc, uint32_t col, uint32_t row)
{
    uint16_t base = (uint16_t)((sc & 0xfc) << 8);
    uint16_t adr = (uint16_t)(base + (row & 31) * 32 + (col & 31));
    if (sc & 1) { if (col & 32) adr += 0x400; }
    if (sc & 2) { if (row & 32) adr += (sc & 1) ? 0x800 : 0x400; }
    return (uint16_t)(adr & 0x7fff);
}

static inline uint32_t map_cell(uint8_t sc, uint32_t col, uint32_t row)
{
    (void)sc;
    return (row & 63) * 64 + (col & 63);
}

static int find_blank_tile(int layer)
{
    uint16_t adr = (uint16_t)PPU_bgTileAdr(g_ppu, layer);
    if (s_blank_tiles_adr[layer] == adr && s_blank_tile[layer] >= -1) return s_blank_tile[layer];
    s_blank_tiles_adr[layer] = adr;
    s_blank_tile[layer] = -1;
    for (int t = 0; t < 1024; t++) {
        const uint16_t *w = &g_ppu->vram[(adr + t * 16) & 0x7fff];
        int blank = 1;
        for (int i = 0; i < 16; i++) if (w[i]) { blank = 0; break; }
        if (blank) { s_blank_tile[layer] = t; break; }
    }
    return s_blank_tile[layer];
}

static void tag_vram_changes(void)
{
    if (!s_prev_vram) s_prev_vram = malloc(0x8000 * sizeof(uint16_t));
    if (!s_prev_vram) return;
    if (s_prev_vram_valid && s_ngroups) {
        for (int l = 0; l < CACHE_LAYERS; l++) {
            uint8_t sc = s_groups[0].sc[l];
            if (s_tag_base[l] != sc) { memset(s_tag[l], 0, sizeof(s_tag[l])); s_tag_base[l] = sc; }
            uint32_t rows = (sc & 2) ? 64 : 32, cols = (sc & 1) ? 64 : 32;
            for (uint32_t row = 0; row < rows; row++) {
                for (uint32_t col = 0; col < cols; col++) {
                    uint16_t a = tilemap_word(sc, col, row);
                    if (g_ppu->vram[a] == s_prev_vram[a]) continue;
                    CellTag *t = &s_tag[l][map_cell(sc, col, row)];
                    t->valid = 0;
                    for (int gi = 0; gi < s_ngroups; gi++) {
                        const ViewGroup *g = &s_groups[gi];
                        if (g->sc[l] != sc) continue;
                        uint32_t cam_x = s_cam[g->view][0];
                        uint32_t tx0 = cam_x >> 3;
                        int dx = (int)((col - tx0) & (cols - 1));
                        if (dx >= (int)cols / 2) dx -= (int)cols;
                        if (dx < -16 || dx > 32 + 16) continue;
                        uint32_t v = (uint32_t)(((int)(s_cam[g->view][1] & 0x3ff) + g->off_y[l]) & 0x3ff);
                        uint32_t map_row0 = (v + (uint32_t)g->y0) >> 3;
                        uint32_t py0 = (uint32_t)(((int)v + g->y0 - g->off_y[l]) & 0xffff);
                        uint32_t ty0 = py0 >> 3;
                        int dy = (int)((row - map_row0) & (rows - 1));
                        if (dy >= (int)rows / 2) dy -= (int)rows;
                        int view_rows = (g->y1 - g->y0 + 7) / 8 + 1;
                        if (dy < -8 || dy > view_rows + 8) continue;
                        t->tx = (uint16_t)((int)tx0 + dx);
                        t->ty = (uint16_t)((int)ty0 + dy);
                        t->view = (uint8_t)g->view;
                        t->valid = 1;
                        TileStore *st = &s_store[g->view][l];
                        if (store_alloc(st) && st->base == sc)
                            store_put(st, t->tx, t->ty, g_ppu->vram[a]);
                        break;
                    }
                }
            }
        }
    }
    memcpy(s_prev_vram, g_ppu->vram, 0x8000 * sizeof(uint16_t));
    s_prev_vram_valid = 1;
}

static void build_groups(const FrameRecord *rec)
{
    s_ngroups = 0;
    unsigned line = 1;
    while (line <= 224 && s_ngroups < MAX_VIEW_GROUPS) {
        const Ppu *a = (const Ppu *)rec->regs[line].b;
        unsigned end = line;
        while (end < 224) {
            const Ppu *b = (const Ppu *)rec->regs[end + 1].b;
            if (b->inidisp != a->inidisp || memcmp(b->hScroll, a->hScroll, 4) != 0 ||
                memcmp(b->vScroll, a->vScroll, 4) != 0 || memcmp(b->bgXsc, a->bgXsc, 2) != 0)
                break;
            end++;
        }
        if (!(a->inidisp & 0x80) && end - line + 1 >= 16 && (a->bgmode & 7) == 1) {
            int view = view_for_scroll(a->hScroll[0], a->vScroll[0]);
            if (view >= 0) {
                ViewGroup *g = &s_groups[s_ngroups++];
                g->y0 = (int)line;
                g->y1 = (int)end + 1;
                g->view = view;
                for (int l = 0; l < CACHE_LAYERS; l++) {
                    g->h[l] = a->hScroll[l];
                    g->v[l] = a->vScroll[l];
                    g->sc[l] = a->bgXsc[l];
                    g->off_y[l] = floor8_signed((int)a->vScroll[l] - (int)(s_cam[view][1] & 0x3ff));
                }
            }
        }
        line = end + 1;
    }
}

static void cache_learn(const FrameRecord *rec)
{
    tag_vram_changes();
    build_groups(rec);
    for (int gi = 0; gi < s_ngroups; gi++) {
        const ViewGroup *g = &s_groups[gi];
        for (int l = 0; l < CACHE_LAYERS; l++) {
            TileStore *st = &s_store[g->view][l];
            if (!store_alloc(st)) return;
            if (st->base != g->sc[l]) { store_clear(st); st->base = g->sc[l]; }
            int32_t cx = (int16_t)(s_cam[g->view][0] & 0xffff), cy = (int16_t)(s_cam[g->view][1] & 0xffff);
            uint32_t tx0 = (uint32_t)(cx >> 3);
            int32_t top = cy + wrap_delta((int)((g->v[l] - g->off_y[l]) & 0x3ff), cy & 0x3ff) + g->y0;
            uint32_t ty0 = (uint32_t)(top >> 3), ty1 = (uint32_t)((top + g->y1 - 1 - g->y0) >> 3);
            uint32_t map_row0 = ((uint32_t)g->v[l] + (uint32_t)g->y0) >> 3;
            for (uint32_t r = 0; r <= ty1 - ty0; r++) {
                uint32_t row = map_row0 + r;
                for (uint32_t c = 0; c <= 32; c++) {
                    uint32_t col = (g->h[l] >> 3) + c;
                    uint16_t e = g_ppu->vram[tilemap_word(g->sc[l], col, row)];
                    store_put(st, tx0 + c, ty0 + r, e);
                    s_stat_learned++;
                }
            }
        }
    }
}

enum {
    MAP_BG1 = 0x3500,
    MAP_BG2 = 0x10000,
    MAP_STRIDE = 96,
    MAP_COLS = 96,
    MAP_ROWS = 114,
    MAP_INDEX_TABLE = 0x15DC0,
    MAP_METATILE_TABLE = 0x189C0,
};

static inline uint16_t ram16(uint32_t a)
{
    a &= 0x1ffff;
    return (uint16_t)(g_ram[a] | (g_ram[(a + 1) & 0x1ffff] << 8));
}


static void map_extents(void)
{
    for (int l = 0; l < CACHE_LAYERS; l++) {
        uint32_t base = l == 0 ? MAP_BG1 : MAP_BG2;
        int left = MAP_COLS, right = -1;
        for (int mx = 0; mx < MAP_COLS; mx++) {
            int used = 0;
            for (int my = 0; my < MAP_ROWS; my++)
                if (ram16(base + ((uint32_t)my * MAP_STRIDE + (uint32_t)mx) * 2)) { used = 1; break; }
            if (used) { if (mx < left) left = mx; right = mx; }
        }
        if (right < 0) { left = 0; right = MAP_COLS - 1; }
        s_map_left[l] = left;
        s_map_right[l] = right;
    }
}

static int map_tile(int layer, uint32_t tx, uint32_t ty, uint16_t *out)
{
    int32_t stx = (int32_t)(int16_t)(tx & 0xffff);
    int32_t mx = stx >> 2, my = (int32_t)(int16_t)(ty & 0xffff);
    uint32_t mt;
    if (mx >= 0 && mx < MAP_COLS && my >= 0 && my < MAP_ROWS) {
        uint32_t base = layer == 0 ? MAP_BG1 : MAP_BG2;
        uint16_t idx = ram16(base + ((uint32_t)my * MAP_STRIDE + (uint32_t)mx) * 2);
        if (idx >= 0xa000) return 0;
        mt = ram16(MAP_INDEX_TABLE + idx);
    } else {
        mt = ram16(MAP_INDEX_TABLE + 4 + ((ty >> 1) & 1) * 4 + (ty & 1) * 2);
    }
    if (mt >= 0x7000) return 0;
    *out = ram16(MAP_METATILE_TABLE + mt + ((uint32_t)stx & 3) * 2);
    return 1;
}

static void cache_serve(int extra)
{
    g_ppu->renderVram = NULL;
    if (!s_cache_enabled || !s_ngroups || extra <= 0) return;
    if (s_map_extent_frame == 0 || (unsigned)snes_frame_counter - s_map_extent_frame >= 300) {
        map_extents();
        s_map_extent_frame = (unsigned)snes_frame_counter ? (unsigned)snes_frame_counter : 1;
    }
    if (!s_vram[0]) s_vram[0] = malloc(0x8000 * sizeof(uint16_t));
    if (!s_vram[1]) s_vram[1] = malloc(0x8000 * sizeof(uint16_t));
    if (!s_vram[0] || !s_vram[1]) return;
    s_vram_cur ^= 1;
    uint16_t *vram = s_vram[s_vram_cur];
    memcpy(vram, g_ppu->vram, 0x8000 * sizeof(uint16_t));
    int margin_cols = extra / 8 + 2;
    for (int gi = 0; gi < s_ngroups; gi++) {
        const ViewGroup *g = &s_groups[gi];
        uint32_t cam_x = s_cam[g->view][0];
        uint32_t cam_y = s_cam[g->view][1];
        for (int l = 0; l < CACHE_LAYERS; l++) {
            const TileStore *st = &s_store[g->view][l];
            if (!st->entry || st->base != g->sc[l]) continue;
            int32_t cx = (int16_t)(cam_x & 0xffff), cy = (int16_t)(cam_y & 0xffff);
            uint32_t tx0 = (uint32_t)(cx >> 3);
            uint32_t v = (uint32_t)(((int)(cam_y & 0x3ff) + g->off_y[l]) & 0x3ff);
            int32_t top = cy + g->y0;
            uint32_t ty0 = (uint32_t)(top >> 3), ty1 = (uint32_t)((top + g->y1 - 1 - g->y0) >> 3);
            uint32_t map_row0 = (v + (uint32_t)g->y0) >> 3;
            for (uint32_t r = 0; r <= ty1 - ty0; r++) {
                uint32_t row = map_row0 + r;
                for (int c = -margin_cols; c <= 32 + margin_cols; c++) {
                    if (c >= 0 && c <= 32) continue;
                    uint32_t tx = (uint32_t)((int)tx0 + c);
                    uint16_t e;
                    uint16_t adr = tilemap_word(g->sc[l], tx, row);
                    if (map_tile(l, tx, ty0 + r, &e) || store_get(st, tx, ty0 + r, &e)) {
                        vram[adr] = e;
                        s_stat_served++;
                        continue;
                    }
                    const CellTag *t = &s_tag[l][map_cell(g->sc[l], tx, row)];
                    if (t->valid && (t->tx != (uint16_t)tx || t->ty != (uint16_t)(ty0 + r))) {
                        int blank = find_blank_tile(l);
                        if (blank >= 0) { vram[adr] = (uint16_t)blank; s_stat_blanked++; }
                    }
                }
            }
        }
    }
    g_ppu->renderVram = vram;
}

static int s_env_applied;

void RnrrDisplayApplyEnv(void)
{
    s_env_applied = 1;
    const char *fps = getenv("RNRR_PRESENT_FPS");
    if (fps && *fps) RnrrDisplaySetPresentationFps(1, (unsigned)strtoul(fps, NULL, 10));
    const char *ws = getenv("RNRR_WIDESCREEN");
    if (ws && *ws) RnrrDisplaySetWidescreen(atoi(ws));
    const char *tc = getenv("RNRR_WS_TILECACHE");
    if (tc && *tc) s_cache_enabled = atoi(tc) != 0;
    const char *oc = getenv("RNRR_OVERCLOCK");
    if (oc && *oc) RnrrSetOverclock((unsigned)strtoul(oc, NULL, 10));
}

static void apply_env_overrides(void)
{
    if (!s_env_applied) RnrrDisplayApplyEnv();
}

void RnrrPrepareFrame(int drawable_w, int drawable_h, int *frame_w, int *frame_h)
{
    apply_env_overrides();
    int width = 256;
    int override = PpuWsExtraOverride();
    if (override >= 0) {
        width = 256 + 2 * override;
    } else if (s_ws_mode != RNRR_WS_OFF) {
        SnesDisplayFrame frame = SnesDisplayAspect_ComputeAdaptiveFrame(
            256, 224, RNRR_MAX_FRAME_WIDTH, target_aspect(drawable_w, drawable_h),
            SnesDisplayAspect_Clamp(g_config.display_aspect));
        width = frame.width;
    }
    if (width > RNRR_MAX_FRAME_WIDTH) width = RNRR_MAX_FRAME_WIDTH;
    if (width < 256) width = 256;
    width -= (width - 256) & 1;
    s_frame_w = width;
    s_extra = (width - 256) / 2;
    *frame_w = width;
    *frame_h = 224;
}

int RnrrInRace(void)
{
    if (!g_ppu) return 0;
    return g_ppu->bgXsc[2] == 0x3d && g_ppu->screenEnabled[0] == 0x17;
}

enum { HUD_SPLIT_ROWS = 30, HUD_MINIMAP_ROWS = 76 };

static void hud_bands_for_view(Ppu *ppu, int extra, int slot, int y0, int y1)
{
    PpuWsElasticSeg split[3] = {
        {0, HUD_LEFT_END, (int16_t)-extra, (int16_t)(HUD_LEFT_END - extra)},
        {HUD_LEFT_END, HUD_RIGHT_START, HUD_LEFT_END, HUD_RIGHT_START},
        {HUD_RIGHT_START, 256, (int16_t)(HUD_RIGHT_START + extra), (int16_t)(256 + extra)},
    };
    PpuWsElasticSeg left_only[2] = {
        {0, HUD_LEFT_END, (int16_t)-extra, (int16_t)(HUD_LEFT_END - extra)},
        {HUD_LEFT_END, 256, HUD_LEFT_END, 256},
    };
    PpuWsElasticSeg identity[1] = {{0, 256, 0, 256}};
    int a1 = y0 + HUD_SPLIT_ROWS < y1 ? y0 + HUD_SPLIT_ROWS : y1;
    int b1 = y0 + HUD_MINIMAP_ROWS < y1 ? y0 + HUD_MINIMAP_ROWS : y1;
    if (a1 > y0) PpuSetWidescreenLayerElasticBandSlot(ppu, (uint8_t)slot, 2, (uint8_t)y0, (uint8_t)a1, split, 3);
    if (b1 > a1) PpuSetWidescreenLayerElasticBandSlot(ppu, (uint8_t)(slot + 1), 2, (uint8_t)a1, (uint8_t)b1, left_only, 2);
    if (y1 > b1) PpuSetWidescreenLayerElasticBandSlot(ppu, (uint8_t)(slot + 2), 2, (uint8_t)b1, (uint8_t)y1, identity, 1);
}

static void apply_hud_policy(Ppu *ppu, int extra)
{
    if (!extra) return;
    PpuSetWidescreenBg3Widen(ppu, 1);
    if (s_ngroups == 0) {
        hud_bands_for_view(ppu, extra, 0, 1, 225);
    } else {
        for (int gi = 0; gi < s_ngroups && gi < 2; gi++)
            hud_bands_for_view(ppu, extra, gi * 3, s_groups[gi].y0, s_groups[gi].y1);
    }
    PpuSetWsHudOamBand(ppu, 225, HUD_LEFT_END, HUD_RIGHT_START);
    PpuSetWsHudOamShiftRange(ppu, 0, 4);
}

void RnrrBeginSimFrame(unsigned number)
{
    (void)number;
    apply_cull_window(s_extra);
    int race = RnrrInRace();
    if (race != s_race_frame) cache_reset();
    s_race_frame = race;
    overclock_apply(race ? s_overclock : 100);
    read_cameras();
    if (!s_extra) { g_ppu->renderVram = NULL; return; }
    if (s_race_frame) {
        apply_hud_policy(g_ppu, s_extra);
        cache_serve(s_extra);
    } else {
        g_ppu->renderVram = NULL;
        PpuSetExtraSpaceCentered(g_ppu, (uint16_t)s_extra);
    }
}

void RnrrRasterObserve(const struct Ppu *ppu, unsigned line, void *context)
{
    (void)context;
    if (line < 1 || line > 224) return;
    memcpy(s_rec[s_cur].regs[line].b, ppu, PPU_SAVESTATE_REGS_SIZE);
}

void RnrrEndSimFrame(const uint8_t *field, unsigned number)
{
    if (s_race_frame && s_extra) cache_learn(&s_rec[s_cur]);
    {
        static const char *wdir; static int wstep, wchecked;
        if (!wchecked) { wchecked = 1; wdir = getenv("RNRR_WRAM_DUMP_DIR"); const char *st = getenv("RNRR_WRAM_DUMP_STEP"); wstep = st && *st ? atoi(st) : 60; if (wstep <= 0) wstep = 60; }
        if (wdir && *wdir && number % (unsigned)wstep == 0) {
            char path[512]; snprintf(path, sizeof(path), "%s/wram_%06u.bin", wdir, number);
            FILE *f = fopen(path, "wb"); if (f) { fwrite(g_ram, 1, 0x20000, f); fclose(f); }
        }
    }
    {
        static const char *ddir; static int dchecked; static FILE *dlog;
        if (!dchecked) { dchecked = 1; ddir = getenv("RNRR_DIAG_DIR");
            if (ddir && *ddir) { char path[512]; snprintf(path, sizeof(path), "%s/diag.log", ddir); dlog = fopen(path, "w"); } }
        if (dlog) {
            const FrameRecord *rec = &s_rec[s_cur];
            { static uint64_t last_mc;
              fprintf(dlog, "f=%u race=%d extra=%d groups=%d served=%llu camfb=%llu be1=%02x 0355=%02x mc=%llu nmi=%d hirq=%d virq=%d vt=%u ht=%u I=%d pc=%06x", number, s_race_frame, s_extra, s_ngroups, (unsigned long long)s_stat_served, (unsigned long long)s_stat_cam_fallback, g_ram[0xBE1], g_ram[0x355],
                      (unsigned long long)(g_cpu.master_cycles - last_mc), g_snes->nmiEnabled, g_snes->hIrqEnabled, g_snes->vIrqEnabled, g_snes->vTimer, g_snes->hTimer, g_cpu._flag_I, interp_bridge_lle_resume_pc());
              last_mc = g_cpu.master_cycles; }
            for (unsigned i = 1; i <= 224;) {
                unsigned j = i;
                while (j < 224 && !memcmp(rec->regs[j + 1].b, rec->regs[i].b, PPU_SAVESTATE_REGS_SIZE)) j++;
                const Ppu *l = (const Ppu *)rec->regs[i].b;
                fprintf(dlog, " | L%u-%u mode=%02x ini=%02x tm=%02x ts=%02x sc=%02x,%02x,%02x tile=%04x h=%u,%u,%u v=%u,%u,%u w1=%u-%u w2=%u-%u wsel=%08x wlog=%04x cgw=%02x cga=%02x",
                        i, j, l->bgmode, l->inidisp, l->screenEnabled[0], l->screenEnabled[1], l->bgXsc[0], l->bgXsc[1], l->bgXsc[2], l->bgTileAdr,
                        l->hScroll[0], l->hScroll[1], l->hScroll[2], l->vScroll[0], l->vScroll[1], l->vScroll[2],
                        l->window1left, l->window1right, l->window2left, l->window2right, l->windowsel, l->wbgobjlog, l->cgwsel, l->cgadsub);
                i = j + 1;
            }
            for (int ch = 0; ch < 8; ch++) {
                const DmaChannel *c = &g_snes->dma->channel[ch];
                if (c->bAdr == 0x18 || c->bAdr == 0x19 || c->bAdr == 0x04)
                    fprintf(dlog, " dma%d[%02x:%04x->21%02x sz=%u m=%u]", ch, c->aBank, c->aAdr, c->bAdr, c->size, c->mode);
            }
            {
                PpuFrameInfo fi;
                if (ppudma_frame_at(0, &fi)) fprintf(dlog, " | nmi=%u irq=%u(cpu %u, raster %u)", fi.nmi_count, fi.irq_count, fi.irq_cpu, fi.irq_raster);
                PpuWlogEntry w[256]; int lost = 0;
                int n = ppu_wlog_collect((uint32_t)snes_frame_counter, w, 256, &lost);
                for (int i = 0; i < n; i++)
                    if (w[i].reg == 0x2105 || w[i].reg == 0x2100 || w[i].reg == 0x420c || w[i].reg == 0x212c || w[i].reg == 0x211a)
                        fprintf(dlog, " w[%04x=%02x@%d s%d]", w[i].reg, w[i].val, w[i].line, w[i].src);
                if (lost) fprintf(dlog, " wlost");
            }
            fputc('\n', dlog);
            if (number % 20 == 0) {
                fflush(dlog);
                char path[512];
                snprintf(path, sizeof(path), "%s/vram_%06u.bin", ddir, number);
                FILE *f = fopen(path, "wb");
                if (f) { fwrite(g_ppu->vram, 2, 0x8000, f); fwrite(g_ppu->oam, 2, 0x100, f); fwrite(g_ppu->highOam, 1, 0x20, f); fwrite(g_ppu->cgram, 2, 0x100, f); fwrite(g_ram, 1, 0x2000, f); fclose(f); }
                snprintf(path, sizeof(path), "%s/frame_%06u.bmp", ddir, number);
                f = fopen(path, "wb");
                if (f) {
                    int w = s_frame_w, h = 224;
                    uint32_t img = (uint32_t)w * h * 4, off = 54, size = off + img;
                    uint8_t hdr[54] = {'B', 'M'}; int32_t neg_h = -h; uint16_t planes = 1, bpp = 32; uint32_t hsz = 40;
                    memcpy(hdr + 2, &size, 4); memcpy(hdr + 10, &off, 4); memcpy(hdr + 14, &hsz, 4); memcpy(hdr + 18, &w, 4); memcpy(hdr + 22, &neg_h, 4);
                    memcpy(hdr + 26, &planes, 2); memcpy(hdr + 28, &bpp, 2); memcpy(hdr + 34, &img, 4);
                    fwrite(hdr, 1, 54, f);
                    for (int y = 0; y < h; y++) fwrite(field + (size_t)y * w * 4, 1, (size_t)w * 4, f);
                    fclose(f);
                }
            }
        }
    }
    if (getenv("RNRR_DEBUG")) {
        static unsigned split_frames, split_missed;
        if (s_race_frame && g_ram[0x356] == 2) {
            const Ppu *a = (const Ppu *)s_rec[s_cur].regs[50].b, *b = (const Ppu *)s_rec[s_cur].regs[170].b;
            split_frames++;
            if (a->hScroll[0] == b->hScroll[0] && a->vScroll[0] == b->vScroll[0]) split_missed++;
        }
        if (number % 600 == 0)
            fprintf(stderr, "[rnrr] f=%u ws=%d extra=%d groups=%d learned=%llu served=%llu blanked=%llu split=%u missed=%u\n", number, s_ws_mode, s_extra, s_ngroups,
                    (unsigned long long)s_stat_learned, (unsigned long long)s_stat_served, (unsigned long long)s_stat_blanked, split_frames, split_missed);
    }
    if (!s_fps_enabled) {
        s_rec[0].valid = s_rec[1].valid = 0;
        key_reset();
        s_snap_valid = 0;
        return;
    }
    if (!s_snap) s_snap = calloc(1, sizeof(Ppu));
    if (!s_scratch) s_scratch = calloc(1, sizeof(Ppu));
    if (!s_snap || !s_scratch) return;
    FrameRecord *rec = &s_rec[s_cur];
    memcpy(rec->oam, g_ppu->oam, sizeof(rec->oam));
    memcpy(rec->high_oam, g_ppu->highOam, sizeof(rec->high_oam));
    rec->number = number;
    rec->extra = s_extra;
    rec->valid = 1;
    {
        const FrameRecord *key = &s_key[s_key_cur];
        s_perf.sim++;
        int regs_changed = !key->valid || memcmp(rec->regs, key->regs, sizeof(rec->regs)) != 0;
        int oam_changed = !key->valid || memcmp(rec->oam, key->oam, sizeof(rec->oam)) != 0 ||
                          memcmp(rec->high_oam, key->high_oam, sizeof(rec->high_oam)) != 0;
        if (regs_changed || (oam_changed && number - s_key_field >= s_key_period)) {
            s_perf.upd++;
            if (key->valid && getenv("RNRR_LERP_LOG")) {
                unsigned nr = 0, no = 0, nh = 0, first = 0;
                for (unsigned l = 1; l <= 224; l++)
                    if (memcmp(rec->regs[l].b, key->regs[l].b, PPU_SAVESTATE_REGS_SIZE)) { nr++; if (!first) first = l; }
                for (unsigned i = 0; i < 0x100; i++) if (rec->oam[i] != key->oam[i]) { no++; if (!first) first = 1000 + i; }
                for (unsigned i = 0; i < 0x20; i++) nh += rec->high_oam[i] != key->high_oam[i];
                const Ppu *a = (const Ppu *)rec->regs[100].b, *b = (const Ppu *)key->regs[100].b;
                fprintf(stderr, "[rnrr-upd] field=%u race=%d oc=%u regs_lines=%u first=%u oam_words=%u hoam=%u h1=%u->%u v1=%u->%u\n",
                        number, s_race_frame, s_overclock, nr, first, no, nh, b->hScroll[0], a->hScroll[0], b->vScroll[0], a->vScroll[0]);
            }
            unsigned gap = number - s_key_field;
            if (key->valid && gap <= KEY_PERIOD_MAX) {
                s_key_hist[s_key_hist_pos++ & 7] = (uint8_t)gap;
                s_key_period = key_period_mode();
            }
            s_key_cur ^= 1;
            memcpy(&s_key[s_key_cur], rec, sizeof(*rec));
            s_key_field = number;
            memcpy(s_snap, g_ppu, sizeof(Ppu));
            s_snap_valid = 1;
        }
    }
    s_cur ^= 1;
}

static int scroll_lerp(uint16_t prev, uint16_t cur, double alpha, uint16_t *out)
{
    int delta = (int)((cur - prev + 512) & 1023) - 512;
    if (delta > INTERP_MAX_SCROLL || delta < -INTERP_MAX_SCROLL) return 0;
    double v = (double)cur - (1.0 - alpha) * delta;
    int iv = (int)(v + (v >= 0 ? 0.5 : -0.5));
    *out = (uint16_t)(iv & 1023);
    return 1;
}

static int same_scene(const FrameRecord *prev, const FrameRecord *cur)
{
    if (!prev->valid || !cur->valid) return 0;
    if (cur->number < prev->number + 1 || cur->number > prev->number + 4) return 0;
    if (prev->extra != cur->extra) return 0;
    for (unsigned line = 1; line <= 224; line++) {
        const Ppu *a = (const Ppu *)prev->regs[line].b;
        const Ppu *b = (const Ppu *)cur->regs[line].b;
        if (a->bgmode != b->bgmode || a->inidisp != b->inidisp ||
            a->screenEnabled[0] != b->screenEnabled[0] ||
            memcmp(a->bgXsc, b->bgXsc, sizeof(a->bgXsc)) != 0)
            return 0;
    }
    return 1;
}

static void lerp_oam(Ppu *dst, const FrameRecord *prev, const FrameRecord *cur, double alpha)
{
    int px[128], py[128], cx[128], cy[128];
    uint8_t psz[128], csz[128], used[128] = {0};
    for (int n = 0; n < 128; n++) {
        uint16_t pa = prev->oam[n * 2], ca = cur->oam[n * 2];
        uint8_t ph = (prev->high_oam[n >> 2] >> ((n & 3) * 2)) & 3;
        uint8_t ch = (cur->high_oam[n >> 2] >> ((n & 3) * 2)) & 3;
        px[n] = (pa & 0xff) | ((ph & 1) << 8); if (px[n] >= 256) px[n] -= 512;
        cx[n] = (ca & 0xff) | ((ch & 1) << 8); if (cx[n] >= 256) cx[n] -= 512;
        py[n] = pa >> 8; cy[n] = ca >> 8;
        psz[n] = ph >> 1; csz[n] = ch >> 1;
    }
    int match[128];
    for (int n = 0; n < 128; n++) {
        match[n] = -1;
        if (cy[n] >= 224 || py[n] >= 224) continue;
        if (prev->oam[n * 2 + 1] != cur->oam[n * 2 + 1] || psz[n] != csz[n]) continue;
        int dx = cx[n] - px[n], dy = cy[n] - py[n];
        if (dx > INTERP_MAX_OBJ || dx < -INTERP_MAX_OBJ || dy > INTERP_MAX_OBJ || dy < -INTERP_MAX_OBJ) continue;
        match[n] = n;
        used[n] = 1;
    }
    for (int n = 0; n < 128; n++) {
        if (match[n] >= 0 || cy[n] >= 224) continue;
        uint16_t cb = cur->oam[n * 2 + 1];
        int found = -1, count = 0;
        for (int m = 0; m < 128; m++) {
            if (used[m] || prev->oam[m * 2 + 1] != cb || psz[m] != csz[n] || py[m] >= 224) continue;
            int dx = cx[n] - px[m], dy = cy[n] - py[m];
            if (dx > INTERP_MAX_OBJ || dx < -INTERP_MAX_OBJ || dy > INTERP_MAX_OBJ || dy < -INTERP_MAX_OBJ) continue;
            found = m;
            count++;
        }
        if (count == 1) { match[n] = found; used[found] = 1; }
    }
    for (int n = 0; n < 128; n++) {
        int best = match[n];
        if (best < 0) continue;
        int dx = cx[n] - px[best], dy = cy[n] - py[best];
        if (!dx && !dy) continue;
        double fx = cx[n] - (1.0 - alpha) * dx, fy = cy[n] - (1.0 - alpha) * dy;
        int ix = (int)(fx + (fx >= 0 ? 0.5 : -0.5));
        int iy = (int)(fy + 0.5);
        if (iy < 0) iy = 0;
        if (iy > 255) iy = 255;
        uint16_t xbits = (uint16_t)(ix & 0x1ff);
        dst->oam[n * 2] = (uint16_t)((iy << 8) | (xbits & 0xff));
        uint8_t shift = (uint8_t)((n & 3) * 2);
        dst->highOam[n >> 2] = (uint8_t)((dst->highOam[n >> 2] & ~(1 << shift)) |
                                         (((xbits >> 8) & 1) << shift));
    }
}

static void capture_frame(const uint8_t *px, size_t pitch, int w, int h)
{
    static int checked, step;
    static const char *dir;
    static int next = -1;
    if (!checked) {
        checked = 1;
        dir = getenv("RNRR_CAPTURE_DIR");
        const char *s = getenv("RNRR_CAPTURE_STEP");
        step = s && *s ? atoi(s) : 200;
        if (step <= 0) step = 200;
        const char *st = getenv("RNRR_CAPTURE_START");
        next = dir && *dir ? (st && *st ? atoi(st) : 0) : -1;
    }
    if (next < 0 || snes_frame_counter < next) return;
    next = snes_frame_counter + step;
    char path[512];
    snprintf(path, sizeof(path), "%s/rnrr_%06d.bmp", dir, snes_frame_counter);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t img = (uint32_t)w * h * 4, off = 54, size = off + img;
    uint8_t hdr[54] = {'B', 'M'};
    int32_t neg_h = -h;
    uint16_t planes = 1, bpp = 32;
    uint32_t hsz = 40;
    memcpy(hdr + 2, &size, 4); memcpy(hdr + 10, &off, 4);
    memcpy(hdr + 14, &hsz, 4); memcpy(hdr + 18, &w, 4); memcpy(hdr + 22, &neg_h, 4);
    memcpy(hdr + 26, &planes, 2); memcpy(hdr + 28, &bpp, 2); memcpy(hdr + 34, &img, 4);
    fwrite(hdr, 1, 54, f);
    for (int y = 0; y < h; y++) fwrite(px + (size_t)y * pitch, 1, (size_t)w * 4, f);
    fclose(f);
}

static int pillarbox(uint8_t *dst, size_t pitch, const uint8_t *field, int frame_w, int frame_h)
{
    int extra = (frame_w - 256) / 2;
    if (extra <= 0) return 0;
    for (int y = 0; y < frame_h; y++) {
        uint8_t *row = dst + (size_t)y * pitch;
        const uint8_t *src = field + (size_t)y * frame_w * 4;
        memset(row, 0, (size_t)extra * 4);
        memcpy(row + (size_t)extra * 4, src + (size_t)extra * 4, 256 * 4);
        memset(row + (size_t)(extra + 256) * 4, 0, (size_t)extra * 4);
    }
    return 1;
}

int RnrrDrawFrame(uint8_t *dst, size_t pitch, const uint8_t *field,
                  int frame_w, int frame_h, double alpha)
{
    if (!s_race_frame && frame_w > 256) {
        pillarbox(dst, pitch, field, frame_w, frame_h);
        capture_frame(dst, pitch, frame_w, frame_h);
        perf_overlay(dst, pitch, frame_w, frame_h, 0);
        return 1;
    }
    const FrameRecord *cur = &s_key[s_key_cur];
    const FrameRecord *prev = &s_key[s_key_cur ^ 1];
    if (alpha > 1.0) alpha = 1.0;
    if (alpha < 0.0) alpha = 0.0;
    unsigned last_field = s_rec[s_cur ^ 1].number;
    unsigned k = last_field >= s_key_field ? last_field - s_key_field : 0;
    alpha = ((double)k + alpha) / (double)s_key_period;
    {
        static int lerp_log = -1;
        if (lerp_log < 0) lerp_log = getenv("RNRR_LERP_LOG") != NULL;
        if (lerp_log)
            fprintf(stderr, "[rnrr-lerp] field=%u key=%u k=%u period=%u alpha=%.3f scene=%d\n",
                    last_field, s_key_field, k, s_key_period, alpha, same_scene(prev, cur));
    }
    if (!s_fps_enabled || !s_snap_valid || alpha >= 1.0 || alpha <= 0.0 ||
        !same_scene(prev, cur) || frame_w != 256 + 2 * cur->extra || frame_h != 224) {
        capture_frame(field, (size_t)frame_w * 4, frame_w, frame_h);
        copy_field(dst, pitch, field, frame_w, frame_h);
        perf_overlay(dst, pitch, frame_w, frame_h, 0);
        return 1;
    }

    memcpy(s_scratch, s_snap, sizeof(Ppu));
    s_scratch->extraObjects = NULL;
    s_scratch->extraObjectCount = 0;
    s_scratch->widescreenLineEnhancer = NULL;
    s_scratch->widescreenLineEnhancerContext = NULL;
    memset(&s_scratch->mode7Hd, 0, sizeof(s_scratch->mode7Hd));
    PpuBeginDrawing(s_scratch, dst, pitch, s_snap->renderFlags);
    lerp_oam(s_scratch, prev, cur, alpha);

    for (unsigned line = 1; line <= 224; line++) {
        RegBlock regs = cur->regs[line];
        Ppu *r = (Ppu *)regs.b;
        const Ppu *p = (const Ppu *)prev->regs[line].b;
        for (int bg = 0; bg < 4; bg++) {
            uint16_t h, v;
            if (scroll_lerp(p->hScroll[bg], r->hScroll[bg], alpha, &h) &&
                scroll_lerp(p->vScroll[bg], r->vScroll[bg], alpha, &v)) {
                r->hScroll[bg] = h;
                r->vScroll[bg] = v;
            }
        }
        memcpy(s_scratch, regs.b, PPU_SAVESTATE_REGS_SIZE);
        ppu_runLine(s_scratch, (int)line);
    }
    capture_frame(dst, pitch, frame_w, frame_h);
    perf_overlay(dst, pitch, frame_w, frame_h, 1);
    return 1;
}

int RnrrWindowBaseWidth(int frame_w)
{
    apply_env_overrides();
    int mode = s_ws_mode != RNRR_WS_OFF ? s_ws_mode : RnrrModsWidescreenMode();
    if (mode == RNRR_WS_OFF) return (frame_w * 5 + 2) / 4;
    double aspect = 16.0 / 9.0;
    if (mode == RNRR_WS_21_9) aspect = 21.0 / 9.0;
    if (mode == RNRR_WS_32_9) aspect = 32.0 / 9.0;
    return (int)(240.0 * aspect + 0.5);
}

double RnrrPresentationHz(double display_refresh)
{
    if (!s_fps_enabled) return 0;
    return snes_host_presentation_hz(s_fps, display_refresh);
}

void RnrrOnReset(void)
{
    s_rec[0].valid = s_rec[1].valid = 0;
    key_reset();
    s_snap_valid = 0;
    cache_reset();
}

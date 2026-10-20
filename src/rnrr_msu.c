#include "rnrr_msu.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"

static int s_active;
static char s_error[256];
static char s_patch_path[1024];

int RnrrMsuActive(void)
{
    return s_active;
}

const char *RnrrMsuError(void)
{
    return s_error;
}

static void set_pack_env(const char *value)
{
#ifdef _WIN32
    _putenv_s("SNESRECOMP_MSU1", value);
#else
    if (value && *value) setenv("SNESRECOMP_MSU1", value, 1);
    else unsetenv("SNESRECOMP_MSU1");
#endif
}

static int has_suffix(const char *name, const char *suffix)
{
    size_t n = strlen(name), m = strlen(suffix);
    if (n < m) return 0;
    for (size_t i = 0; i < m; i++) {
        char a = name[n - m + i], b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

static int find_patch(const char *pack, char *out, size_t cap)
{
    const char *override = getenv("RNRR_MSU1_PATCH");
    if (override && *override) {
        snprintf(out, cap, "%s", override);
        return 1;
    }
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", pack);
    struct stat st;
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        char *slash = strrchr(dir, '/');
#ifdef _WIN32
        char *back = strrchr(dir, '\\');
        if (!slash || (back && back > slash)) slash = back;
#endif
        if (slash) *slash = 0;
        else snprintf(dir, sizeof(dir), ".");
    }
    DIR *d = opendir(dir);
    if (!d) return 0;
    char best[512] = {0};
    int best_rank = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        int rank = has_suffix(e->d_name, ".ips") ? 2 : has_suffix(e->d_name, ".bps") ? 1 : 0;
        if (rank > best_rank || (rank == best_rank && rank && strcmp(e->d_name, best) < 0)) {
            best_rank = rank;
            snprintf(best, sizeof(best), "%s", e->d_name);
        }
    }
    closedir(d);
    if (!best_rank) return 0;
    snprintf(out, cap, "%s/%s", dir, best);
    return 1;
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > (64 << 20)) { fclose(f); return NULL; }
    uint8_t *buf = malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(buf); return NULL; }
    *size = got;
    return buf;
}

static int apply_ips(const uint8_t *p, size_t n, uint8_t *rom, size_t rom_size)
{
    if (n < 8 || memcmp(p, "PATCH", 5)) return 0;
    size_t pos = 5;
    while (pos + 3 <= n) {
        if (!memcmp(p + pos, "EOF", 3)) return 1;
        if (pos + 5 > n) return 0;
        size_t off = ((size_t)p[pos] << 16) | ((size_t)p[pos + 1] << 8) | p[pos + 2];
        size_t len = ((size_t)p[pos + 3] << 8) | p[pos + 4];
        pos += 5;
        if (len == 0) {
            if (pos + 3 > n) return 0;
            size_t rle = ((size_t)p[pos] << 8) | p[pos + 1];
            uint8_t v = p[pos + 2];
            pos += 3;
            if (off + rle > rom_size) return 0;
            memset(rom + off, v, rle);
        } else {
            if (pos + len > n || off + len > rom_size) return 0;
            memcpy(rom + off, p + pos, len);
            pos += len;
        }
    }
    return 0;
}

static int bps_num(const uint8_t *p, size_t n, size_t *pos, uint64_t *out)
{
    uint64_t data = 0, shift = 1;
    while (*pos < n) {
        uint8_t x = p[(*pos)++];
        data += (uint64_t)(x & 0x7f) * shift;
        if (x & 0x80) { *out = data; return 1; }
        shift <<= 7;
        data += shift;
    }
    return 0;
}

static int apply_bps(const uint8_t *p, size_t n, const uint8_t *src, size_t src_size,
                     uint8_t *dst, size_t dst_cap, size_t *dst_size)
{
    if (n < 16 || memcmp(p, "BPS1", 4)) return 0;
    size_t pos = 4;
    uint64_t ssz, tsz, msz;
    if (!bps_num(p, n, &pos, &ssz) || !bps_num(p, n, &pos, &tsz) || !bps_num(p, n, &pos, &msz)) return 0;
    if (ssz != src_size || tsz > dst_cap) return 0;
    pos += (size_t)msz;
    size_t end = n - 12;
    size_t out = 0, src_rel = 0, dst_rel = 0;
    while (pos < end) {
        uint64_t data;
        if (!bps_num(p, n, &pos, &data)) return 0;
        uint64_t cmd = data & 3, len = (data >> 2) + 1;
        if (out + len > tsz) return 0;
        switch (cmd) {
        case 0:
            if (out + len > src_size) return 0;
            memcpy(dst + out, src + out, (size_t)len);
            out += (size_t)len;
            break;
        case 1:
            if (pos + len > end) return 0;
            memcpy(dst + out, p + pos, (size_t)len);
            pos += (size_t)len;
            out += (size_t)len;
            break;
        case 2: {
            uint64_t rel;
            if (!bps_num(p, n, &pos, &rel)) return 0;
            int64_t delta = (rel & 1) ? -(int64_t)(rel >> 1) : (int64_t)(rel >> 1);
            int64_t s = (int64_t)src_rel + delta;
            if (s < 0 || (uint64_t)s + len > src_size) return 0;
            memcpy(dst + out, src + s, (size_t)len);
            src_rel = (size_t)(s + (int64_t)len);
            out += (size_t)len;
            break;
        }
        default: {
            uint64_t rel;
            if (!bps_num(p, n, &pos, &rel)) return 0;
            int64_t delta = (rel & 1) ? -(int64_t)(rel >> 1) : (int64_t)(rel >> 1);
            int64_t t = (int64_t)dst_rel + delta;
            if (t < 0 || (uint64_t)t >= out) return 0;
            for (uint64_t i = 0; i < len; i++) dst[out + i] = dst[(size_t)t + i];
            dst_rel = (size_t)(t + (int64_t)len);
            out += (size_t)len;
            break;
        }
        }
    }
    if (out != tsz) return 0;
    *dst_size = out;
    return 1;
}

void RnrrMsuOnRomLoaded(const uint8_t *rom, size_t size)
{
    s_active = 0;
    s_error[0] = 0;
    const char *pack = getenv("SNESRECOMP_MSU1");
    if (!pack || !*pack) {
        pack = getenv("RNRR_MSU1");
        if (pack && *pack) set_pack_env(pack);
    }
    if (!pack || !*pack || !strcmp(pack, "0") || !strcmp(pack, "off")) return;
    if (!find_patch(pack, s_patch_path, sizeof(s_patch_path))) {
        snprintf(s_error, sizeof(s_error), "No .ips or .bps MSU-1 patch found in %s", pack);
        fprintf(stderr, "[rnrr-msu1] %s; MSU-1 disabled\n", s_error);
        set_pack_env("");
        return;
    }
    size_t psize = 0;
    uint8_t *patch = read_file(s_patch_path, &psize);
    uint8_t *image = malloc(size);
    if (!patch || !image) {
        snprintf(s_error, sizeof(s_error), "Cannot read MSU-1 patch %s", s_patch_path);
        free(patch); free(image);
        fprintf(stderr, "[rnrr-msu1] %s; MSU-1 disabled\n", s_error);
        set_pack_env("");
        return;
    }
    memcpy(image, rom, size);
    int ok;
    size_t out_size = size;
    if (has_suffix(s_patch_path, ".bps"))
        ok = apply_bps(patch, psize, rom, size, image, size, &out_size);
    else
        ok = apply_ips(patch, psize, image, size);
    free(patch);
    if (!ok || out_size != size) {
        snprintf(s_error, sizeof(s_error),
                 "MSU-1 patch %s is invalid or grows the ROM beyond %zu bytes", s_patch_path, size);
        free(image);
        fprintf(stderr, "[rnrr-msu1] %s; MSU-1 disabled\n", s_error);
        set_pack_env("");
        return;
    }
    memcpy((uint8_t *)rom, image, size);
    free(image);
    cpu_select_interpreted_program();
    interp_bridge_set_scheduler_aot_policy(0);
    s_active = 1;
    fprintf(stderr, "[rnrr-msu1] applied %s in memory; patched cartridge runs through the interpreter\n",
            s_patch_path);
}

int RnrrMsuSelectSaveRoot(void)
{
    static int done;
    if (!s_active || done) return 1;
    done = 1;
    char root[96];
    if (snprintf(root, sizeof(root), "%s/msu1", RtlSaveRoot()) >= (int)sizeof(root)) {
        snprintf(s_error, sizeof(s_error), "MSU-1 save directory path is too long");
        return 0;
    }
    RtlEnsureSaveDir();
    RtlSetSaveRoot(root);
    RtlEnsureSaveDir();
    return 1;
}

const char *RnrrMsuSnapshotIdentity(void)
{
    return s_active ? "rnrr-msu1" : "rnrr-stock";
}

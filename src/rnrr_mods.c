#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_runtime.h"
#include "rnrr_display.h"

static const char kPackage[] = "rnrr.display";

static int option(const char *feature, const char *id, char *out, unsigned cap)
{
    out[0] = 0;
    return snes_mod_runtime_feature_option_value_c(kPackage, feature, id, out, cap) != 0 && out[0];
}

static void reset_display(void)
{
    if (getenv("RNRR_DEBUG")) fprintf(stderr, "[rnrr] mods reset\n");
    RnrrDisplaySetWidescreen(RNRR_WS_OFF);
    RnrrDisplaySetPresentationFps(0, 0);
    RnrrSetOverclock(100);
    RnrrDisplayApplyEnv();
}

static int widescreen_mode_from_option(char *value)
{
    int mode = RNRR_WS_16_9;
    if (option("widescreen", "aspect", value, 32)) {
        if (!strcmp(value, "21:9")) mode = RNRR_WS_21_9;
        else if (!strcmp(value, "32:9")) mode = RNRR_WS_32_9;
        else if (!strcmp(value, "fit")) mode = RNRR_WS_FIT;
    }
    return mode;
}

int RnrrModsWidescreenMode(void)
{
    char value[32];
    if (snes_mod_runtime_feature_enabled_c(kPackage, "widescreen") <= 0) return RNRR_WS_OFF;
    return widescreen_mode_from_option(value);
}

static void enable_widescreen(void)
{
    char value[32];
    int mode = widescreen_mode_from_option(value);
    RnrrDisplaySetWidescreen(mode);
    if (getenv("RNRR_DEBUG")) fprintf(stderr, "[rnrr] widescreen plugin: aspect=%s mode=%d\n", value, mode);
}

static void enable_msu1(void)
{
}

static void enable_fps30(void)
{
}

int RnrrModsFps30Enabled(void)
{
    const char *env = getenv("RNRR_FPS30");
    if (env && *env) return atoi(env) != 0;
    return snes_mod_runtime_feature_enabled_c(kPackage, "fps30") > 0;
}

static void enable_overclock(void)
{
    char value[32];
    unsigned pct = 0;
    if (getenv("RNRR_OVERCLOCK")) return;
    if (option("overclock", "percent", value, sizeof(value)) && strcmp(value, "auto"))
        pct = (unsigned)strtoul(value, NULL, 10);
    if (!pct) pct = RnrrModsFps30Enabled() ? 150 : 100;
    RnrrSetOverclock(pct);
    if (getenv("RNRR_DEBUG")) fprintf(stderr, "[rnrr] overclock plugin: %u%%\n", pct);
}

static void enable_presentation_fps(void)
{
    char value[32];
    unsigned fps = 0;
    if (option("presentation-fps", "rate", value, sizeof(value)) && strcmp(value, "auto"))
        fps = (unsigned)strtoul(value, NULL, 10);
    RnrrDisplaySetPresentationFps(1, fps);
}

SNES_MOD_CONSTRUCTOR(register_rnrr_mods)
{
    snes_mod_register_reset_callback(reset_display);
    snes_mod_register_presentation_plugin("rnrr.widescreen", enable_widescreen);
    snes_mod_register_presentation_plugin("rnrr.presentation-fps", enable_presentation_fps);
    snes_mod_register_presentation_plugin("rnrr.overclock", enable_overclock);
    snes_mod_register_activation_plugin("rnrr.msu1", enable_msu1);
    snes_mod_register_activation_plugin("rnrr.fps30", enable_fps30);
}

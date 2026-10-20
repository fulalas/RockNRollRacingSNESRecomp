// Blargg-style NTSC for RockNRollRacingSNESRecomp. Public domain / CC0-1.0.
// Pass 2: FIR demodulation of the 4x oversampled signal, YIQ -> RGB, then
// sharpness, gamma, contrast, brightness and scanlines.

#pragma parameter ntsc_preset "NTSC preset (0 composite 1 svideo 2 rgb 3 mono)" 0.0 0.0 3.0 1.0
#pragma parameter ntsc_contrast "Contrast" 0.0 -1.0 1.0 0.01
#pragma parameter ntsc_brightness "Brightness" 0.0 -1.0 1.0 0.01
#pragma parameter ntsc_sharpness "Sharpness" 0.0 -1.0 1.0 0.01
#pragma parameter ntsc_gamma "Gamma" 0.0 -1.0 1.0 0.01
#pragma parameter ntsc_bleed "Bleed" 0.0 -1.0 1.0 0.01
#pragma parameter ntsc_resolution "Resolution" 0.0 -1.0 1.0 0.01
#pragma parameter ntsc_scanline "Scanline intensity" 0.125 0.0 1.0 0.025
#pragma parameter ntsc_merge_fields "Merge odd and even fields" 1.0 0.0 1.0 1.0

#ifdef VERTEX
in vec2 VertexCoord;
in vec2 TexCoord;
out vec2 vTexCoord;
void main() {
    vTexCoord = TexCoord;
    gl_Position = vec4(VertexCoord * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT
uniform sampler2D Texture;
uniform vec2 InputSize;
uniform vec2 TextureSize;
uniform vec2 OutputSize;
uniform int FrameCount;
#ifdef PARAMETER_UNIFORM
uniform float ntsc_preset;
uniform float ntsc_contrast;
uniform float ntsc_brightness;
uniform float ntsc_sharpness;
uniform float ntsc_gamma;
uniform float ntsc_bleed;
uniform float ntsc_resolution;
uniform float ntsc_scanline;
uniform float ntsc_merge_fields;
#else
#define ntsc_preset 0.0
#define ntsc_contrast 0.0
#define ntsc_brightness 0.0
#define ntsc_sharpness 0.0
#define ntsc_gamma 0.0
#define ntsc_bleed 0.0
#define ntsc_resolution 0.0
#define ntsc_scanline 0.125
#define ntsc_merge_fields 1.0
#endif
in vec2 vTexCoord;
out vec4 FragColor;

const float PI = 3.14159265358979;
const int TAPS = 12;
const mat3 YIQ_TO_RGB = mat3(
    1.0, 0.956, 0.621,
    1.0, -0.272, -0.647,
    1.0, -1.106, 1.703);

float sinc(float x) {
    x *= PI;
    return abs(x) < 1e-4 ? 1.0 : sin(x) / x;
}

float window(int k) {
    float n = float(k) / float(TAPS);
    return 0.5 + 0.5 * cos(PI * n);
}

void main() {
    int preset = int(ntsc_preset + 0.5);
    vec2 texel = 1.0 / TextureSize;
    vec2 px = vTexCoord * TextureSize;
    float sample_x = floor(px.x);
    float line = floor(px.y);
    vec2 base = vec2((sample_x + 0.5) * texel.x, vTexCoord.y);

    float luma_cut = 0.085 * pow(2.0, ntsc_resolution);
    float chroma_cut = 0.055 * pow(2.0, ntsc_bleed);
    if (preset >= 2) { luma_cut = 0.25 * pow(2.0, ntsc_resolution); chroma_cut = luma_cut; }

    float y = 0.0, i = 0.0, q = 0.0, wl = 0.0, wc = 0.0;
    float max_x = TextureSize.x - 1.0;
    for (int k = -TAPS; k <= TAPS; k++) {
        float cx = clamp(sample_x + float(k), 0.0, max_x);
        vec4 sig = texture(Texture, vec2((cx + 0.5) * texel.x, vTexCoord.y));
        float fx = float(k);
        float lw = sinc(2.0 * luma_cut * fx) * window(k);
        float cw = sinc(2.0 * chroma_cut * fx) * window(k);
        float sx = sample_x + fx;
        float phase = sx * (PI / 3.0) + line * (2.0 * PI / 3.0);
        if (ntsc_merge_fields < 0.5) phase += float(FrameCount & 1) * PI;
        float c = cos(phase), s = sin(phase);
        float luma_src = (preset == 0) ? sig.x : sig.w;
        float chroma_src = (preset == 0) ? sig.x : sig.y;
        if (preset >= 2) chroma_src = sig.y;
        y += luma_src * lw;
        i += chroma_src * c * 2.0 * cw;
        q += chroma_src * s * 2.0 * cw;
        wl += lw;
        wc += cw;
    }
    y /= max(wl, 1e-5);
    i /= max(wc, 1e-5);
    q /= max(wc, 1e-5);
    if (preset == 3) { i = 0.0; q = 0.0; }

    if (ntsc_sharpness != 0.0) {
        float yl = texture(Texture, vec2((clamp(sample_x - 4.0, 0.0, max_x) + 0.5) * texel.x, vTexCoord.y)).w;
        float yr = texture(Texture, vec2((clamp(sample_x + 4.0, 0.0, max_x) + 0.5) * texel.x, vTexCoord.y)).w;
        float blur = (yl + yr) * 0.5;
        y += (y - blur) * ntsc_sharpness * 1.5;
    }

    vec3 rgb = vec3(y, i, q) * YIQ_TO_RGB;
    rgb = (rgb - 0.5) * (1.0 + ntsc_contrast) + 0.5 + ntsc_brightness * 0.5;
    rgb = clamp(rgb, 0.0, 1.0);
    rgb = pow(rgb, vec3(1.0 / (1.0 + ntsc_gamma * 0.5)));

    float frac_y = fract(vTexCoord.y * InputSize.y);
    float scan = 1.0 - ntsc_scanline * (0.5 - 0.5 * cos(2.0 * PI * frac_y));
    FragColor = vec4(rgb * scan, 1.0);
}
#endif

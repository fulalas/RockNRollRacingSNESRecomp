// Blargg-style NTSC for RockNRollRacingSNESRecomp. Public domain / CC0-1.0.
// Pass 1: RGB -> YIQ, modulate chroma on the colour subcarrier at 4x
// horizontal oversampling (6 samples per subcarrier cycle, 3-phase).

#pragma parameter ntsc_preset "NTSC preset (0 composite 1 svideo 2 rgb 3 mono)" 0.0 0.0 3.0 1.0
#pragma parameter ntsc_hue "Hue" 0.0 -1.0 1.0 0.01
#pragma parameter ntsc_saturation "Saturation" 0.0 -1.0 1.0 0.01
#pragma parameter ntsc_artifacts "Artifacts" 0.0 -1.0 1.0 0.01
#pragma parameter ntsc_fringing "Fringing" 0.0 -1.0 1.0 0.01
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
uniform float ntsc_hue;
uniform float ntsc_saturation;
uniform float ntsc_artifacts;
uniform float ntsc_fringing;
uniform float ntsc_merge_fields;
#else
#define ntsc_preset 0.0
#define ntsc_hue 0.0
#define ntsc_saturation 0.0
#define ntsc_artifacts 0.0
#define ntsc_fringing 0.0
#define ntsc_merge_fields 1.0
#endif
in vec2 vTexCoord;
out vec4 FragColor;

const float PI = 3.14159265358979;
const mat3 RGB_TO_YIQ = mat3(
    0.299, 0.587, 0.114,
    0.596, -0.274, -0.322,
    0.211, -0.523, 0.312);

void main() {
    int preset = int(ntsc_preset + 0.5);
    float sample_x = floor(vTexCoord.x * TextureSize.x * 4.0);
    float srcx = floor(sample_x / 4.0);
    float line = floor(vTexCoord.y * TextureSize.y);
    srcx = clamp(srcx, 0.0, TextureSize.x - 1.0);
    vec2 uv = vec2((srcx + 0.5) / TextureSize.x, vTexCoord.y);
    vec3 rgb = texture(Texture, uv).rgb;
    vec3 yiq = rgb * RGB_TO_YIQ;

    float sat = 1.0 + ntsc_saturation;
    if (preset == 3) sat = 0.0;
    float hue = ntsc_hue * PI * 0.25;
    float ch = cos(hue), sh = sin(hue);
    yiq.yz = vec2(yiq.y * ch - yiq.z * sh, yiq.y * sh + yiq.z * ch) * sat;

    float artifacts = 1.0 + ntsc_artifacts;
    float fringing = 1.0 + ntsc_fringing;
    if (preset == 1) { artifacts *= 0.25; fringing *= 0.25; }
    if (preset >= 2) { artifacts = 0.0; fringing = 0.0; }

    float phase = sample_x * (PI / 3.0) + line * (2.0 * PI / 3.0);
    float field = (ntsc_merge_fields > 0.5) ? 0.0 : float(FrameCount & 1) * PI;
    phase += field;
    float c = cos(phase), s = sin(phase);

    float chroma = yiq.y * c + yiq.z * s;
    float luma_in_chroma = yiq.x * fringing;
    float chroma_in_luma = chroma * artifacts;
    float composite = yiq.x + chroma_in_luma;
    float chroma_sig = chroma + 0.0 * luma_in_chroma;
    FragColor = vec4(composite, chroma_sig, luma_in_chroma, yiq.x);
}
#endif

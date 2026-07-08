#include "audio.h"
#include "raylib.h"
#include "config.h"
#include <cmath>
#include <cstdlib>

static const int SR = 22050;

// ----------------------------------------------------------------
// tiny synth helpers
// ----------------------------------------------------------------
static Wave MakeWave(float seconds) {
    Wave w{};
    w.frameCount = (unsigned int)(seconds * SR);
    w.sampleRate = SR;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = malloc((size_t)w.frameCount * 2);
    return w;
}
static short* SD(Wave& w) { return (short*)w.data; }
static float NoiseF() { return GetRandomValue(-1000, 1000) / 1000.0f; }
static short Q(float s, float amp) { return (short)(Clampf(s, -1, 1) * amp); }

// ----------------------------------------------------------------
struct SfxPool {                 // aliases allow overlapping playback
    Sound base{};
    Sound alias[3]{};
    int n = 0, idx = 0;
    Sound& Next() {
        if (n == 0) return base;
        idx = (idx + 1) % (n + 1);
        return idx == 0 ? base : alias[idx - 1];
    }
};

static SfxPool g_sfx[SFX_COUNT];
static Sound g_ambient{};
static bool g_ready = false;

static void Register(int id, Wave w, int aliases) {
    g_sfx[id].base = LoadSoundFromWave(w);
    g_sfx[id].n = aliases;
    for (int i = 0; i < aliases; i++)
        g_sfx[id].alias[i] = LoadSoundAlias(g_sfx[id].base);
    UnloadWave(w);
}

// ----------------------------------------------------------------
// sound recipes
// ----------------------------------------------------------------
static Wave GenSlash() {
    Wave w = MakeWave(0.10f); short* d = SD(w);
    float prev = 0;
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = i / (float)SR;
        float env = expf(-t * 30.0f) * (t < 0.006f ? t / 0.006f : 1.0f);
        float nz = NoiseF();
        float hp = nz - prev; prev = nz;                    // crude highpass
        float ring = 0.25f * sinf(2 * PI * 1150 * t) * expf(-t * 45.0f);
        d[i] = Q((hp * 0.75f + ring) * env, 11000);
    }
    return w;
}
static Wave GenHit() {
    Wave w = MakeWave(0.13f); short* d = SD(w);
    float lp = 0;
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = i / (float)SR;
        float env = expf(-t * 34.0f);
        float nz = NoiseF();
        lp += (nz - lp) * 0.35f;
        float thump = 0.6f * sinf(2 * PI * 175 * t);
        d[i] = Q((lp * 0.8f + thump) * env, 13500);
    }
    return w;
}
static Wave GenExplo() {
    Wave w = MakeWave(0.55f); short* d = SD(w);
    float bn = 0;
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = i / (float)SR;
        float env = expf(-t * 7.0f);
        bn = bn * 0.985f + NoiseF() * 0.25f;                // brown-ish noise
        float boom = 0.35f * sinf(2 * PI * (60 + 45 * expf(-t * 8)) * t);
        d[i] = Q((bn * 1.5f + boom) * env, 15000);
    }
    return w;
}
static Wave GenDeath() {
    Wave w = MakeWave(0.32f); short* d = SD(w);
    float ph = 0;
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = i / (float)SR;
        float f = 380 * expf(-t * 6.0f) + 60;               // falling pitch
        ph += 2 * PI * f / SR;
        float sq = sinf(ph) > 0 ? 0.5f : -0.5f;
        d[i] = Q(sq * expf(-t * 9.0f) + NoiseF() * 0.2f * expf(-t * 12.0f), 11500);
    }
    return w;
}
static Wave GenWhooshWave(float len, float lpCoef) {
    Wave w = MakeWave(len); short* d = SD(w);
    float lp = 0;
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = i / (float)SR;
        float env = sinf(PI * t / len);                     // swell in & out
        lp += (NoiseF() - lp) * lpCoef;
        d[i] = Q(lp * 2.0f * env, 10500);
    }
    return w;
}
static Wave GenFire() {
    Wave w = MakeWave(0.42f); short* d = SD(w);
    float lp = 0;
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = i / (float)SR;
        float env = expf(-t * 6.0f);
        lp += (NoiseF() - lp) * 0.3f;
        float crackle = (GetRandomValue(0, 100) < 4) ? NoiseF() * 0.9f : 0.0f;
        float rumble = 0.3f * sinf(2 * PI * 85 * t);
        d[i] = Q((lp * 0.8f + crackle + rumble) * env, 13000);
    }
    return w;
}
static Wave GenStone() {
    Wave w = MakeWave(0.5f); short* d = SD(w);
    float bn = 0;
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = i / (float)SR;
        float env = expf(-t * 5.5f);
        bn = bn * 0.99f + NoiseF() * 0.12f;
        float deep = 0.7f * sinf(2 * PI * 55 * t) + 0.3f * sinf(2 * PI * 38 * t);
        float click = t < 0.012f ? NoiseF() * 0.8f : 0.0f;
        d[i] = Q((deep + bn + click) * env, 15500);
    }
    return w;
}
static Wave GenLove() {
    Wave w = MakeWave(0.28f); short* d = SD(w);
    const float freqs[3] = { 660, 880, 1175 };
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = i / (float)SR;
        float s = 0;
        for (int b = 0; b < 3; b++) {
            float t0 = b * 0.07f;
            if (t >= t0 && t < t0 + 0.09f) {
                float lt = t - t0;
                s += sinf(2 * PI * freqs[b] * lt) * expf(-lt * 28.0f) * 0.5f;
            }
        }
        d[i] = Q(s, 10000);
    }
    return w;
}
static Wave GenHurt() {
    Wave w = MakeWave(0.16f); short* d = SD(w);
    float ph = 0;
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = i / (float)SR;
        ph += 205.0f / SR;
        float saw = fmodf(ph, 1.0f) * 2 - 1;
        d[i] = Q((saw * 0.55f + NoiseF() * 0.4f) * expf(-t * 20.0f), 12000);
    }
    return w;
}
static Wave GenPickup() {
    Wave w = MakeWave(0.18f); short* d = SD(w);
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = i / (float)SR;
        float f = t < 0.08f ? 520.0f : 780.0f;
        float lt = t < 0.08f ? t : t - 0.08f;
        d[i] = Q(sinf(2 * PI * f * t) * expf(-lt * 22.0f) * 0.6f, 9500);
    }
    return w;
}
static Wave GenRoar() {
    Wave w = MakeWave(0.8f); short* d = SD(w);
    float ph = 0, lp = 0;
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = i / (float)SR;
        float env = (t < 0.06f ? t / 0.06f : 1.0f) * expf(-t * 3.2f);
        float f = 88 + 26 * sinf(2 * PI * 5.5f * t);
        ph += 2 * PI * f / SR;
        float growl = sinf(ph) * 0.6f + sinf(ph * 2.02f) * 0.35f;
        lp += (NoiseF() - lp) * 0.18f;
        d[i] = Q((growl + lp * 0.55f) * env, 15000);
    }
    return w;
}
static Wave GenSerpent() {
    // sibilant hiss with a rattling tremolo
    Wave w = MakeWave(0.32f); short* d = SD(w);
    float prev = 0;
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = i / (float)SR;
        float env = sinf(PI * t / 0.32f);
        float nz = NoiseF();
        float hp = nz - prev; prev = nz;                    // hissy highpass
        float rattle = 0.62f + 0.38f * (sinf(2 * PI * 26 * t) > 0 ? 1.0f : 0.2f);
        d[i] = Q(hp * 1.3f * env * rattle, 10500);
    }
    return w;
}
static Wave GenWind() {
    // deep two-gust gale
    Wave w = MakeWave(0.6f); short* d = SD(w);
    float lp = 0;
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = i / (float)SR;
        float env = sinf(PI * t / 0.6f) * (0.7f + 0.3f * sinf(2 * PI * 3.3f * t));
        lp += (NoiseF() - lp) * 0.055f;
        d[i] = Q(lp * 2.6f * env, 14000);
    }
    return w;
}
static Wave GenMist() {
    // soft airy veil with a faint high shimmer
    Wave w = MakeWave(0.38f); short* d = SD(w);
    float lp = 0;
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = i / (float)SR;
        float env = sinf(PI * t / 0.38f);
        lp += (NoiseF() - lp) * 0.05f;
        float shimmer = 0.16f * sinf(2 * PI * 1400 * t) * expf(-t * 6.0f);
        d[i] = Q((lp * 1.7f + shimmer) * env, 9000);
    }
    return w;
}
static Wave GenUpgrade() {
    // rising two-note chime
    Wave w = MakeWave(0.3f); short* d = SD(w);
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = i / (float)SR;
        float s = 0;
        if (t < 0.14f) s += sinf(2 * PI * 523 * t) * expf(-t * 12.0f) * 0.55f;
        if (t >= 0.1f) {
            float lt = t - 0.1f;
            s += (sinf(2 * PI * 784 * lt) + 0.3f * sinf(2 * PI * 1568 * lt))
                 * expf(-lt * 10.0f) * 0.5f;
        }
        d[i] = Q(s, 10000);
    }
    return w;
}
static Wave GenAmbient() {
    const float len = 3.0f;
    Wave w = MakeWave(len); short* d = SD(w);
    float lp = 0;
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = i / (float)SR;
        // LFO uses whole cycles over the loop so the seam is smooth
        float lfo = 0.65f + 0.35f * sinf(2 * PI * t / len);
        lp += (NoiseF() - lp) * 0.045f;
        d[i] = Q(lp * 2.2f * lfo, 3800);
    }
    return w;
}

// ----------------------------------------------------------------
void AudioInit() {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) { g_ready = false; return; }
    SetMasterVolume(0.8f);

    Register(SFX_SLASH,  GenSlash(), 3);
    Register(SFX_HIT,    GenHit(), 3);
    Register(SFX_EXPLO,  GenExplo(), 1);
    Register(SFX_DEATH,  GenDeath(), 3);
    Register(SFX_WATER,  GenWhooshWave(0.30f, 0.12f), 1);
    Register(SFX_FIRE,   GenFire(), 1);
    Register(SFX_STONE,  GenStone(), 1);
    Register(SFX_LOVE,   GenLove(), 2);
    Register(SFX_HURT,   GenHurt(), 1);
    Register(SFX_PICKUP, GenPickup(), 1);
    Register(SFX_ROAR,   GenRoar(), 1);
    Register(SFX_WHOOSH, GenWhooshWave(0.22f, 0.2f), 3);
    Register(SFX_SERPENT, GenSerpent(), 2);
    Register(SFX_WIND,    GenWind(), 1);
    Register(SFX_MIST,    GenMist(), 2);
    Register(SFX_UPGRADE, GenUpgrade(), 1);

    Wave amb = GenAmbient();
    g_ambient = LoadSoundFromWave(amb);
    UnloadWave(amb);
    SetSoundVolume(g_ambient, 0.55f);
    g_ready = true;
}

void AudioUpdate() {
    if (!g_ready) return;
    if (!IsSoundPlaying(g_ambient)) PlaySound(g_ambient);   // endless night wind
}

void AudioShutdown() {
    if (g_ready) {
        for (int i = 0; i < SFX_COUNT; i++) {
            for (int a = 0; a < g_sfx[i].n; a++) UnloadSoundAlias(g_sfx[i].alias[a]);
            UnloadSound(g_sfx[i].base);
        }
        UnloadSound(g_ambient);
    }
    CloseAudioDevice();
}

void PlaySfx(int id, float vol, float pitch, float jitter) {
    if (!g_ready || id < 0 || id >= SFX_COUNT) return;
    Sound& s = g_sfx[id].Next();
    SetSoundVolume(s, Clampf(vol, 0, 1));
    SetSoundPitch(s, pitch * (1.0f + frnd(-jitter, jitter)));
    PlaySound(s);
}

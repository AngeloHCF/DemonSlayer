#pragma once
// ---------------------------------------------------------------
// audio.h — procedural sound effects, synthesized at startup.
// No asset files: every sound is generated from math (raylib only).
// ---------------------------------------------------------------

enum Sfx {
    SFX_SLASH,      // sword swing
    SFX_HIT,        // blade connects
    SFX_EXPLO,      // big explosion
    SFX_DEATH,      // demon dies
    SFX_WATER,      // water breathing whoosh
    SFX_FIRE,       // fire breathing crackle
    SFX_STONE,      // stone breathing slam
    SFX_LOVE,       // love breathing sparkle
    SFX_HURT,       // player damaged
    SFX_PICKUP,     // heal orb
    SFX_ROAR,       // muzan roar
    SFX_WHOOSH,     // dash / teleport
    SFX_SERPENT,    // serpent breathing hiss
    SFX_WIND,       // wind breathing gale
    SFX_MIST,       // mist breathing shimmer
    SFX_UPGRADE,    // upgrade purchased
    SFX_COUNT
};

void AudioInit();       // call once after InitWindow
void AudioUpdate();     // call once per frame (keeps ambient wind looping)
void AudioShutdown();   // call before CloseWindow

// master volume (0..1), used by the settings menu
void  AudioSetMasterVolume(float v);
float AudioGetMasterVolume();

// vol 0..1, pitch multiplier, random pitch jitter
void PlaySfx(int id, float vol = 1.0f, float pitch = 1.0f, float jitter = 0.06f);

// Boss ambience: 0 = normal night, 1 = Kokushibo's oppressive moon-drone.
// While active the night wind ducks and a deep dissonant drone loops beneath.
void SetBossDrone(int mode);

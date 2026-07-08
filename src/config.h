#pragma once
// ---------------------------------------------------------------
// config.h — global tuning constants + tiny shared helpers
// ---------------------------------------------------------------
#include "raylib.h"
#include <cmath>

namespace cfg {
// window / world
inline constexpr int   SCREEN_W  = 1280;
inline constexpr int   SCREEN_H  = 720;
inline constexpr float GROUND_Y  = 620.0f;   // top surface of the floor
inline constexpr float GRAVITY   = 1900.0f;

// player
inline constexpr float P_SPEED    = 330.0f;
inline constexpr float P_JUMP_VEL = -720.0f;
// ================================================================
// >>> TESTING KNOB: change your health here <<<
// e.g. 9999.0f to survive basically anything, 10.0f to test dying.
// ================================================================
inline constexpr float P_MAX_HP   = 100.0f;
inline constexpr float P_IFRAMES  = 0.9f;    // invincibility after being hit
inline constexpr float WATER_CD   = 3.5f;    // Water Breathing cooldown
inline constexpr float FIRE_CD    = 8.0f;    // Fire Breathing cooldown
inline constexpr float STONE_CD   = 10.0f;   // Stone Breathing cooldown
inline constexpr float LOVE_CD    = 6.0f;    // Love Breathing cooldown
inline constexpr float SERPENT_CD = 5.0f;    // Serpent Breathing cooldown
inline constexpr float WIND_CD    = 9.0f;    // Wind Breathing cooldown
inline constexpr float MIST_CD    = 7.0f;    // Mist Breathing cooldown
inline constexpr int   PTS_PER_WAVE = 2;     // upgrade points per cleared wave

// waves / the night's gauntlet
inline constexpr int   BOSS_WAVE  = 6;       // Akaza (Upper Moon Three) after this wave
inline constexpr int   WAVE_DOUMA = 11;      // Douma (Upper Moon Two) after this wave
inline constexpr int   WAVE_KOKU  = 17;      // Kokushibo (Upper Moon One) after this wave
inline constexpr float BOSS_HP    = 3200.0f; // Muzan is a survival wall until sunrise
inline constexpr int   MAX_ALIVE  = 28;      // spawn throttle
} // namespace cfg

// --- tiny helpers shared by all modules -------------------------

inline float frnd(float a, float b) {
    return a + (b - a) * (GetRandomValue(0, 10000) / 10000.0f);
}

inline Color C(int r, int g, int b, int a = 255) {
    return Color{ (unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a };
}

inline float Clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

inline float Lerpf(float a, float b, float t) { return a + (b - a) * t; }

inline float Dist(Vector2 a, Vector2 b) {
    float dx = a.x - b.x, dy = a.y - b.y;
    return sqrtf(dx * dx + dy * dy);
}

// Clamp an entity (pos = center) to the ground. Returns true if on ground.
inline bool GroundClamp(Vector2& pos, Vector2& vel, float halfH) {
    if (pos.y + halfH >= cfg::GROUND_Y) {
        pos.y = cfg::GROUND_Y - halfH;
        if (vel.y > 0) vel.y = 0;
        return true;
    }
    return false;
}

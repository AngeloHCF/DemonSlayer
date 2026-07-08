#pragma once
// ---------------------------------------------------------------
// effects.h — particles, screen shake, hitstop, floating text
// ---------------------------------------------------------------
#include "raylib.h"
#include <vector>

struct Particle {
    Vector2 pos{}, vel{};
    float life = 0, maxLife = 1;
    float size0 = 4, size1 = 0;      // size over lifetime
    Color c0{}, c1{};                // color over lifetime
    float grav = 0;                  // px/s^2 applied to vel.y
    float drag = 0;                  // fraction of velocity lost per second
    int   shape = 0;                 // 0 circle, 1 streak, 2 spinning rect
    float rot = 0, rotVel = 0;
};

struct RingFx {
    Vector2 pos{};
    float r = 0, maxR = 100, speed = 400, thick = 8;
    float life = 1, maxLifeR = 1;
    Color col{};
};

struct FloatText {
    Vector2 pos{};
    float life = 0, maxLife = 0.8f;
    float scale = 1;
    Color col{};
    char txt[24] = {0};
};

class Effects {
public:
    void Reset();
    void Update(float dt);           // call with REAL dt (unscaled by hitstop)
    void DrawWorld() const;          // particles + rings (inside camera)
    void DrawTexts() const;          // floating damage numbers (inside camera)
    void DrawScreen() const;         // full-screen flashes (screen space, over world)

    // emitters -----------------------------------------------------
    void Sparks(Vector2 p, float angleDeg, float spreadDeg, int n,
                Color c, float speed, float size);
    void HitSparks(Vector2 p, int dir, Color c);
    void SlashArc(Vector2 center, float radius, float a0Deg, float a1Deg, Color c);
    void WaterTrail(Vector2 p, int facing);
    void FireCharge(Vector2 p);
    void FireExplosion(Vector2 p);
    void StoneSlam(Vector2 p);
    void LoveSparkle(Vector2 p, int facing);
    void WaterBurst(Vector2 p);
    void SerpentTrail(Vector2 p, int facing);
    void WindSpiral(Vector2 p);
    void MistBurst(Vector2 p);
    void MistWisp(Vector2 p);
    void QuakeTrail(Vector2 p);
    void DeathBurst(Vector2 p, Color c, float scale = 1.0f);
    void Blood(Vector2 p, int dir);
    void BloodSpray(Vector2 p, int dir, float scale = 1.0f);   // brutal arterial gush
    void Dust(Vector2 p);
    void Ember(Vector2 p);
    void MoonWind(Vector2 p, int dir, Color c, float scale = 1.0f); // driven crescent gale
    void Ring(Vector2 p, float startR, float maxR, float speed, float thick, Color c);
    void Text(Vector2 p, Color c, float scale, const char* fmt, ...);

    // game-feel ------------------------------------------------------
    void AddShake(float amount);     // 0..1 trauma
    void AddHitstop(float seconds);
    void Flash(Color c, float strength);  // punch a full-screen tint that decays
    float TimeScale() const;         // ~0.05 while hitstop active, else 1
    Vector2 ShakeOffset() const { return shakeOff; }

private:
    void Push(const Particle& p);
    std::vector<Particle> parts;
    std::vector<RingFx>   rings;
    std::vector<FloatText> texts;
    float trauma = 0.0f;
    float hitstop = 0.0f;
    Vector2 shakeOff{0, 0};
    Color   flashCol{0, 0, 0, 0};
    float   flashA = 0.0f;           // current full-screen flash alpha
    float   flashDecay = 4.0f;       // how fast the current flash fades
};

#include "effects.h"
#include "config.h"
#include <cstdarg>
#include <cstdio>
#include <algorithm>

static Color CL(Color a, Color b, float t) {
    return Color{
        (unsigned char)(a.r + (b.r - a.r) * t),
        (unsigned char)(a.g + (b.g - a.g) * t),
        (unsigned char)(a.b + (b.b - a.b) * t),
        (unsigned char)(a.a + (b.a - a.a) * t)
    };
}

void Effects::Reset() {
    parts.clear(); rings.clear(); texts.clear();
    trauma = 0; hitstop = 0; shakeOff = {0, 0};
    flashCol = {0, 0, 0, 0}; flashA = 0; flashDecay = 4.0f;
}

void Effects::Push(const Particle& p) {
    if (parts.size() < 4200) parts.push_back(p);
}

float Effects::TimeScale() const { return hitstop > 0 ? 0.05f : 1.0f; }

void Effects::AddShake(float amount)   { trauma = Clampf(trauma + amount, 0, 1); }
void Effects::AddHitstop(float seconds){ hitstop = fmaxf(hitstop, seconds); }

void Effects::Flash(Color c, float strength) {
    // brighter/darker flashes win; a stronger punch also lingers a touch longer
    if (strength >= flashA) { flashCol = c; flashA = Clampf(strength, 0, 1); }
    flashDecay = Lerpf(4.0f, 1.6f, Clampf(strength, 0, 1));
}

void Effects::Update(float dt) {
    if (hitstop > 0) hitstop -= dt;
    trauma = Clampf(trauma - 1.7f * dt, 0, 1);
    flashA = fmaxf(flashA - flashDecay * dt, 0);
    float mag = trauma * trauma;
    shakeOff = { frnd(-1, 1) * 18.0f * mag, frnd(-1, 1) * 13.0f * mag };

    for (auto& p : parts) {
        p.life -= dt;
        p.vel.y += p.grav * dt;
        float d = 1.0f - Clampf(p.drag * dt, 0, 0.95f);
        p.vel.x *= d; p.vel.y *= d;
        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
        p.rot += p.rotVel * dt;
    }
    parts.erase(std::remove_if(parts.begin(), parts.end(),
                [](const Particle& p) { return p.life <= 0; }), parts.end());

    for (auto& r : rings) {
        r.r += r.speed * dt;
        r.life -= dt;
    }
    rings.erase(std::remove_if(rings.begin(), rings.end(),
                [](const RingFx& r) { return r.life <= 0 || r.r >= r.maxR; }), rings.end());

    for (auto& t : texts) {
        t.life -= dt;
        t.pos.y -= 42.0f * dt;
    }
    texts.erase(std::remove_if(texts.begin(), texts.end(),
                [](const FloatText& t) { return t.life <= 0; }), texts.end());
}

void Effects::DrawWorld() const {
    for (const auto& r : rings) {
        float a = Clampf(r.life / r.maxLifeR, 0, 1) * (1.0f - r.r / r.maxR);
        DrawRing(r.pos, fmaxf(r.r - r.thick * 0.5f, 0), r.r + r.thick * 0.5f,
                 0, 360, 48, Fade(r.col, Clampf(a, 0, 1)));
    }
    for (const auto& p : parts) {
        float t = 1.0f - Clampf(p.life / p.maxLife, 0, 1);
        Color col = CL(p.c0, p.c1, t);
        float size = Lerpf(p.size0, p.size1, t);
        if (size <= 0.3f) continue;
        switch (p.shape) {
            case 0: DrawCircleV(p.pos, size, col); break;
            case 1: {
                Vector2 tail = { p.pos.x - p.vel.x * 0.035f, p.pos.y - p.vel.y * 0.035f };
                DrawLineEx(tail, p.pos, fmaxf(size * 0.55f, 1.0f), col);
                break;
            }
            case 2: {
                Rectangle rec = { p.pos.x, p.pos.y, size, size };
                DrawRectanglePro(rec, { size * 0.5f, size * 0.5f }, p.rot, col);
                break;
            }
        }
    }
}

void Effects::DrawTexts() const {
    for (const auto& t : texts) {
        float a = Clampf(t.life / t.maxLife, 0, 1);
        int fs = (int)(18 * t.scale);
        int w = MeasureText(t.txt, fs);
        DrawText(t.txt, (int)t.pos.x - w / 2 + 1, (int)t.pos.y + 1, fs, Fade(BLACK, a * 0.7f));
        DrawText(t.txt, (int)t.pos.x - w / 2, (int)t.pos.y, fs, Fade(t.col, a));
    }
}

void Effects::DrawScreen() const {
    if (flashA > 0.003f)
        DrawRectangle(0, 0, cfg::SCREEN_W, cfg::SCREEN_H, Fade(flashCol, flashA));
}

// --- emitters ---------------------------------------------------

void Effects::Sparks(Vector2 p, float angleDeg, float spreadDeg, int n,
                     Color c, float speed, float size) {
    for (int i = 0; i < n; i++) {
        float ang = (angleDeg + frnd(-spreadDeg * 0.5f, spreadDeg * 0.5f)) * DEG2RAD;
        float sp = speed * frnd(0.4f, 1.15f);
        Particle pt;
        pt.pos = p;
        pt.vel = { cosf(ang) * sp, sinf(ang) * sp };
        pt.maxLife = pt.life = frnd(0.15f, 0.45f);
        pt.size0 = size; pt.size1 = 0;
        pt.c0 = c; pt.c1 = Fade(c, 0);
        pt.grav = 500; pt.drag = 2.5f; pt.shape = 1;
        Push(pt);
    }
}

void Effects::HitSparks(Vector2 p, int dir, Color c) {
    Sparks(p, dir > 0 ? -35.0f : -145.0f, 85, 12, c, 460, 3);
    Sparks(p, -90, 360, 5, C(255, 255, 255), 240, 2);
}

void Effects::SlashArc(Vector2 center, float radius, float a0Deg, float a1Deg, Color c) {
    const int n = 15;
    for (int i = 0; i < n; i++) {
        float t = i / (float)(n - 1);
        float ang = Lerpf(a0Deg, a1Deg, t) * DEG2RAD;
        Vector2 dir = { cosf(ang), sinf(ang) };
        Particle pt;
        pt.pos = { center.x + dir.x * radius * frnd(0.8f, 1.0f),
                   center.y + dir.y * radius * frnd(0.8f, 1.0f) };
        // tangential velocity for a sweeping feel
        float tsign = (a1Deg > a0Deg) ? 1.0f : -1.0f;
        pt.vel = { -dir.y * 200.0f * tsign + dir.x * 60.0f,
                    dir.x * 200.0f * tsign + dir.y * 60.0f };
        pt.maxLife = pt.life = 0.12f + t * 0.08f;
        pt.size0 = 3.5f; pt.size1 = 0;
        pt.c0 = c; pt.c1 = Fade(WHITE, 0);
        pt.drag = 4; pt.shape = 1;
        Push(pt);
    }
}

void Effects::WaterTrail(Vector2 p, int facing) {
    for (int i = 0; i < 5; i++) {
        Particle pt;
        pt.pos = { p.x + frnd(-16, 16), p.y + frnd(-24, 24) };
        pt.vel = { -facing * frnd(60, 170), frnd(-50, -5) };
        pt.maxLife = pt.life = frnd(0.25f, 0.5f);
        pt.size0 = frnd(3, 7); pt.size1 = 0;
        pt.c0 = C(90, 170, 255, 220); pt.c1 = C(160, 225, 255, 0);
        pt.grav = -50; pt.drag = 2; pt.shape = 0;
        Push(pt);
    }
    Sparks(p, facing > 0 ? 180.0f : 0.0f, 40, 2, C(200, 240, 255), 260, 2.5f);
}

void Effects::FireCharge(Vector2 p) {
    for (int i = 0; i < 3; i++) {
        float ang = frnd(0, 360) * DEG2RAD;
        Vector2 start = { p.x + cosf(ang) * 46, p.y + sinf(ang) * 46 };
        Particle pt;
        pt.pos = start;
        pt.vel = { (p.x - start.x) * 5.0f, (p.y - start.y) * 5.0f };
        pt.maxLife = pt.life = 0.2f;
        pt.size0 = 3; pt.size1 = 0;
        pt.c0 = C(255, 170, 40); pt.c1 = C(255, 240, 160, 0);
        pt.shape = 1;
        Push(pt);
    }
}

void Effects::FireExplosion(Vector2 p) {
    // fireball core
    for (int i = 0; i < 36; i++) {
        float ang = frnd(0, 360) * DEG2RAD;
        float sp = frnd(80, 620);
        Particle pt;
        pt.pos = p;
        pt.vel = { cosf(ang) * sp, sinf(ang) * sp };
        pt.maxLife = pt.life = frnd(0.3f, 0.7f);
        pt.size0 = frnd(4, 10); pt.size1 = 0;
        pt.c0 = C(255, 190, 60); pt.c1 = C(180, 30, 10, 0);
        pt.grav = 150; pt.drag = 2.2f; pt.shape = 0;
        Push(pt);
    }
    // hot sparks
    Sparks(p, -90, 360, 16, C(255, 245, 170), 700, 3);
    // smoke
    for (int i = 0; i < 10; i++) {
        Particle pt;
        pt.pos = { p.x + frnd(-30, 30), p.y + frnd(-24, 24) };
        pt.vel = { frnd(-40, 40), frnd(-90, -30) };
        pt.maxLife = pt.life = frnd(0.5f, 1.0f);
        pt.size0 = frnd(6, 10); pt.size1 = frnd(14, 20);
        pt.c0 = C(90, 75, 65, 170); pt.c1 = C(40, 40, 45, 0);
        pt.grav = -30; pt.drag = 1; pt.shape = 0;
        Push(pt);
    }
    Ring(p, 12, 175, 640, 13, C(255, 160, 50));
    Ring(p, 6, 120, 430, 6, C(255, 240, 180));
}

void Effects::StoneSlam(Vector2 p) {
    // rock debris
    for (int i = 0; i < 18; i++) {
        Particle pt;
        pt.pos = { p.x + frnd(-30, 30), p.y + frnd(-10, 6) };
        pt.vel = { frnd(-320, 320), frnd(-460, -120) };
        pt.maxLife = pt.life = frnd(0.35f, 0.8f);
        pt.size0 = frnd(4, 9); pt.size1 = 2;
        pt.c0 = C(135, 125, 115); pt.c1 = C(70, 62, 58, 0);
        pt.grav = 1100; pt.shape = 2;
        pt.rotVel = frnd(-600, 600);
        Push(pt);
    }
    // dust wall
    for (int i = 0; i < 14; i++) {
        Particle pt;
        pt.pos = { p.x + frnd(-50, 50), p.y };
        pt.vel = { frnd(-140, 140), frnd(-110, -20) };
        pt.maxLife = pt.life = frnd(0.4f, 0.9f);
        pt.size0 = frnd(6, 11); pt.size1 = frnd(14, 20);
        pt.c0 = C(150, 140, 128, 160); pt.c1 = C(90, 84, 78, 0);
        pt.grav = -20; pt.drag = 1.5f; pt.shape = 0;
        Push(pt);
    }
    Sparks(p, -90, 120, 10, C(230, 225, 210), 420, 3);
    Ring(p, 10, 150, 520, 11, C(170, 160, 150));
    Ring(p, 6, 90, 340, 5, C(230, 225, 205));
}

void Effects::LoveSparkle(Vector2 p, int facing) {
    for (int i = 0; i < 4; i++) {
        Particle pt;
        pt.pos = { p.x + frnd(-18, 18), p.y + frnd(-26, 26) };
        pt.vel = { -facing * frnd(40, 130), frnd(-70, 20) };
        pt.maxLife = pt.life = frnd(0.25f, 0.5f);
        pt.size0 = frnd(2.5f, 6); pt.size1 = 0;
        pt.c0 = C(255, 120, 190, 230); pt.c1 = C(255, 200, 235, 0);
        pt.grav = -40; pt.drag = 2; pt.shape = 0;
        Push(pt);
    }
    Sparks(p, facing > 0 ? 180.0f : 0.0f, 50, 2, C(255, 190, 225), 240, 2.2f);
}

void Effects::WaterBurst(Vector2 p) {
    for (int i = 0; i < 26; i++) {
        float ang = frnd(0, 360) * DEG2RAD;
        float sp = frnd(70, 480);
        Particle pt;
        pt.pos = p;
        pt.vel = { cosf(ang) * sp, sinf(ang) * sp - 80 };
        pt.maxLife = pt.life = frnd(0.3f, 0.65f);
        pt.size0 = frnd(3, 8); pt.size1 = 0;
        pt.c0 = C(90, 170, 255, 230); pt.c1 = C(170, 230, 255, 0);
        pt.grav = 350; pt.drag = 1.6f; pt.shape = 0;
        Push(pt);
    }
    Sparks(p, -90, 360, 10, C(210, 240, 255), 420, 2.5f);
    Ring(p, 12, 130, 520, 9, C(110, 190, 255));
    Ring(p, 6, 90, 380, 5, C(210, 240, 255));
}

void Effects::SerpentTrail(Vector2 p, int facing) {
    for (int i = 0; i < 3; i++) {
        bool venom = (GetRandomValue(0, 1) == 0);
        Particle pt;
        pt.pos = { p.x + frnd(-14, 14), p.y + frnd(-22, 22) };
        pt.vel = { -facing * frnd(70, 180), sinf(frnd(0, 6.28f)) * 90.0f };
        pt.maxLife = pt.life = frnd(0.2f, 0.45f);
        pt.size0 = frnd(2.5f, 5.5f); pt.size1 = 0;
        pt.c0 = venom ? C(120, 220, 90, 220) : C(150, 80, 200, 200);
        pt.c1 = Fade(pt.c0, 0);
        pt.grav = -20; pt.drag = 2.5f; pt.shape = 1;
        Push(pt);
    }
    // venom droplet
    if (GetRandomValue(0, 2) == 0) {
        Particle pt;
        pt.pos = { p.x + frnd(-10, 10), p.y + frnd(-10, 10) };
        pt.vel = { frnd(-40, 40), frnd(30, 90) };
        pt.maxLife = pt.life = frnd(0.3f, 0.5f);
        pt.size0 = frnd(2, 3.5f); pt.size1 = 0;
        pt.c0 = C(140, 230, 80); pt.c1 = C(60, 130, 40, 0);
        pt.grav = 500; pt.shape = 0;
        Push(pt);
    }
}

void Effects::WindSpiral(Vector2 p) {
    for (int i = 0; i < 3; i++) {
        float a = frnd(0, 360) * DEG2RAD;
        Particle pt;
        pt.pos = { p.x + cosf(a) * frnd(10, 40), p.y + frnd(-60, 60) };
        // tangential swirl + strong lift
        pt.vel = { -sinf(a) * frnd(120, 240), frnd(-260, -140) };
        pt.maxLife = pt.life = frnd(0.25f, 0.5f);
        pt.size0 = frnd(2.5f, 5); pt.size1 = 0;
        pt.c0 = C(215, 245, 230, 200); pt.c1 = C(235, 250, 240, 0);
        pt.grav = -60; pt.drag = 1.2f; pt.shape = 1;
        Push(pt);
    }
}

void Effects::MistBurst(Vector2 p) {
    for (int i = 0; i < 13; i++) {
        Particle pt;
        pt.pos = { p.x + frnd(-24, 24), p.y + frnd(-28, 28) };
        float a = frnd(0, 360) * DEG2RAD;
        float sp = frnd(30, 110);
        pt.vel = { cosf(a) * sp, sinf(a) * sp - 20 };
        pt.maxLife = pt.life = frnd(0.5f, 1.1f);
        pt.size0 = frnd(6, 11); pt.size1 = frnd(16, 24);
        pt.c0 = C(190, 195, 212, 150); pt.c1 = C(160, 165, 185, 0);
        pt.grav = -14; pt.drag = 1.8f; pt.shape = 0;
        Push(pt);
    }
}

void Effects::MistWisp(Vector2 p) {
    Particle pt;
    pt.pos = p;
    pt.vel = { frnd(-30, 30), frnd(-40, -10) };
    pt.maxLife = pt.life = frnd(0.6f, 1.2f);
    pt.size0 = frnd(5, 9); pt.size1 = frnd(12, 18);
    pt.c0 = C(185, 190, 208, 110); pt.c1 = C(160, 165, 185, 0);
    pt.grav = -10; pt.drag = 1.5f; pt.shape = 0;
    Push(pt);
}

void Effects::QuakeTrail(Vector2 p) {
    for (int i = 0; i < 2; i++) {
        Particle pt;
        pt.pos = { p.x + frnd(-14, 14), p.y };
        pt.vel = { frnd(-90, 90), frnd(-330, -140) };
        pt.maxLife = pt.life = frnd(0.25f, 0.55f);
        pt.size0 = frnd(3.5f, 7); pt.size1 = 1;
        pt.c0 = C(135, 125, 115); pt.c1 = C(70, 62, 58, 0);
        pt.grav = 1000; pt.shape = 2;
        pt.rotVel = frnd(-500, 500);
        Push(pt);
    }
    Particle d;
    d.pos = { p.x, p.y - 4 };
    d.vel = { frnd(-50, 50), frnd(-70, -20) };
    d.maxLife = d.life = frnd(0.3f, 0.6f);
    d.size0 = frnd(5, 8); d.size1 = frnd(10, 14);
    d.c0 = C(150, 140, 128, 140); d.c1 = C(90, 84, 78, 0);
    d.grav = -20; d.shape = 0;
    Push(d);
}

void Effects::DeathBurst(Vector2 p, Color c, float scale) {
    int n = (int)(28 * scale);
    for (int i = 0; i < n; i++) {
        float ang = frnd(0, 360) * DEG2RAD;
        float sp = frnd(60, 420) * scale;
        Particle pt;
        pt.pos = p;
        pt.vel = { cosf(ang) * sp, sinf(ang) * sp - 60 };
        pt.maxLife = pt.life = frnd(0.3f, 0.65f);
        pt.size0 = frnd(3, 6) * scale; pt.size1 = 0;
        pt.c0 = c; pt.c1 = Fade(c, 0);
        pt.grav = 400; pt.drag = 1.5f; pt.shape = 0;
        Push(pt);
    }
    // dark chunks
    for (int i = 0; i < 10; i++) {
        Particle pt;
        pt.pos = p;
        pt.vel = { frnd(-220, 220), frnd(-320, -80) };
        pt.maxLife = pt.life = frnd(0.4f, 0.8f);
        pt.size0 = frnd(4, 8) * scale; pt.size1 = 1;
        pt.c0 = C(60, 25, 35); pt.c1 = C(30, 12, 18, 0);
        pt.grav = 900; pt.shape = 2;
        pt.rotVel = frnd(-540, 540);
        Push(pt);
    }
    Ring(p, 8, 70 * scale, 320, 6, Fade(c, 0.8f));
}

void Effects::Blood(Vector2 p, int dir) {
    for (int i = 0; i < 11; i++) {
        float ang = ((dir > 0 ? -60.0f : -120.0f) + frnd(-40, 40)) * DEG2RAD;
        float sp = frnd(90, 330);
        Particle pt;
        pt.pos = p;
        pt.vel = { cosf(ang) * sp, sinf(ang) * sp };
        pt.maxLife = pt.life = frnd(0.35f, 0.7f);
        pt.size0 = frnd(2, 4.5f); pt.size1 = 1;
        pt.c0 = C(195, 25, 35); pt.c1 = C(110, 5, 15, 0);
        pt.grav = 950; pt.drag = 0.5f; pt.shape = 0;
        Push(pt);
    }
}

void Effects::BloodSpray(Vector2 p, int dir, float scale) {
    // the main arterial cone: away from the blow and upward
    int n = (int)(26 * scale);
    for (int i = 0; i < n; i++) {
        float ang = ((dir > 0 ? -35.0f : 215.0f) + frnd(-42, 42)) * DEG2RAD;
        float sp = frnd(140, 580) * scale;
        Particle pt;
        pt.pos = { p.x + frnd(-4, 4), p.y + frnd(-8, 8) };
        pt.vel = { cosf(ang) * sp, sinf(ang) * sp };
        pt.maxLife = pt.life = frnd(0.35f, 0.85f);
        pt.size0 = frnd(2, 5) * fminf(scale, 1.6f); pt.size1 = 1;
        pt.c0 = C(200, 22, 32); pt.c1 = C(90, 4, 12, 0);
        pt.grav = 1100; pt.drag = 0.4f;
        pt.shape = (i % 3 == 0) ? 1 : 0;      // streaks among the droplets
        Push(pt);
    }
    // heavy gouts that arc and fall
    int g = (int)(9 * scale);
    for (int i = 0; i < g; i++) {
        Particle pt;
        pt.pos = p;
        pt.vel = { dir * frnd(60, 320) * scale + frnd(-60, 60), frnd(-420, -120) };
        pt.maxLife = pt.life = frnd(0.8f, 1.4f);
        pt.size0 = frnd(3, 6) * fminf(scale, 1.5f); pt.size1 = 2;
        pt.c0 = C(165, 12, 24); pt.c1 = C(70, 2, 8, 0);
        pt.grav = 1350; pt.drag = 0.2f; pt.shape = 0;
        Push(pt);
    }
    // fine crimson mist hanging in the air
    int m = (int)(12 * scale);
    for (int i = 0; i < m; i++) {
        Particle pt;
        pt.pos = { p.x + frnd(-14, 14), p.y + frnd(-14, 14) };
        pt.vel = { dir * frnd(20, 120), frnd(-70, 30) };
        pt.maxLife = pt.life = frnd(0.5f, 1.1f);
        pt.size0 = frnd(2, 4); pt.size1 = frnd(5, 8);
        pt.c0 = C(170, 20, 30, 120); pt.c1 = C(120, 10, 18, 0);
        pt.grav = 60; pt.drag = 1.8f; pt.shape = 0;
        Push(pt);
    }
}

void Effects::Dust(Vector2 p) {
    for (int i = 0; i < 6; i++) {
        Particle pt;
        pt.pos = { p.x + frnd(-10, 10), p.y };
        pt.vel = { frnd(-90, 90), frnd(-60, -10) };
        pt.maxLife = pt.life = frnd(0.2f, 0.45f);
        pt.size0 = frnd(2.5f, 5); pt.size1 = 0;
        pt.c0 = C(150, 135, 120, 150); pt.c1 = C(150, 135, 120, 0);
        pt.grav = -15; pt.drag = 2; pt.shape = 0;
        Push(pt);
    }
}

void Effects::Ember(Vector2 p) {
    Particle pt;
    pt.pos = p;
    pt.vel = { frnd(-25, 25), frnd(-90, -35) };
    pt.maxLife = pt.life = frnd(0.8f, 1.7f);
    pt.size0 = frnd(1.5f, 3); pt.size1 = 0;
    pt.c0 = C(230, 60, 50, 200); pt.c1 = C(120, 20, 30, 0);
    pt.grav = -20; pt.drag = 0.4f; pt.shape = 0;
    Push(pt);
}

void Effects::MoonWind(Vector2 p, int dir, Color c, float scale) {
    // long horizontal streaks torn along the blade's path — a driven gale
    int n = (int)(9 * scale);
    for (int i = 0; i < n; i++) {
        Particle pt;
        pt.pos = { p.x + frnd(-40, 40) * scale, p.y + frnd(-70, 70) * scale };
        pt.vel = { dir * frnd(420, 900) * scale, frnd(-40, 40) };
        pt.maxLife = pt.life = frnd(0.18f, 0.4f);
        pt.size0 = frnd(3, 6) * scale; pt.size1 = 0;
        pt.c0 = Fade(c, 0.85f); pt.c1 = Fade(c, 0);
        pt.drag = 2.0f; pt.shape = 1;
        Push(pt);
    }
    // a few pale motes swept along
    for (int i = 0; i < (int)(4 * scale); i++) {
        Particle pt;
        pt.pos = { p.x + frnd(-30, 30), p.y + frnd(-60, 60) };
        pt.vel = { dir * frnd(240, 560), frnd(-60, 60) };
        pt.maxLife = pt.life = frnd(0.25f, 0.5f);
        pt.size0 = frnd(1.5f, 3); pt.size1 = 0;
        pt.c0 = C(235, 220, 255, 200); pt.c1 = C(200, 170, 245, 0);
        pt.drag = 1.6f; pt.shape = 0;
        Push(pt);
    }
}

void Effects::Ring(Vector2 p, float startR, float maxR, float speed, float thick, Color c) {
    RingFx r;
    r.pos = p; r.r = startR; r.maxR = maxR; r.speed = speed; r.thick = thick;
    r.maxLifeR = r.life = (maxR - startR) / fmaxf(speed, 1.0f) + 0.05f;
    r.col = c;
    rings.push_back(r);
}

void Effects::Text(Vector2 p, Color c, float scale, const char* fmt, ...) {
    if (texts.size() > 60) return;
    FloatText ft;
    ft.pos = p;
    ft.maxLife = ft.life = 0.8f;
    ft.scale = scale;
    ft.col = c;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ft.txt, sizeof(ft.txt), fmt, ap);
    va_end(ap);
    texts.push_back(ft);
}

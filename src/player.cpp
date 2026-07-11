#include "player.h"
#include "combat.h"
#include "effects.h"
#include "config.h"
#include "audio.h"
#include <cmath>
#include <algorithm>

// combo data: windup -> active (hitbox out) -> recovery (chain window)
struct ComboStep { float windup, active, recover, dmg, kb, range; };
static const ComboStep COMBO[4] = {
    { 0.05f, 0.06f, 0.14f, 10, 180, 80 },
    { 0.05f, 0.06f, 0.14f, 10, 210, 80 },
    { 0.07f, 0.06f, 0.15f, 12, 250, 86 },
    { 0.10f, 0.08f, 0.26f, 22, 540, 102 },
};

// ================================================================
// >>> TESTING KNOB: breathing style durations (seconds). <<<
// Raise these to make techniques last longer / feel weightier.
// REACH upgrades scale several of them further in-game.
// ================================================================
static const float WATER_TIME   = 0.38f;   // water dash duration
static const float FIRE_WINDUP  = 0.48f;   // fire charge-up
static const float STONE_WINDUP = 0.65f;   // stone heave before the slam
static const float STONE_TOTAL  = 1.3f;    // full stone technique
static const float LOVE_SEG_T   = 0.18f;   // love: each dash segment
static const float SERP_TIME    = 0.85f;   // serpent weave duration
static const float WIND_WINDUP  = 0.35f;   // wind gathering before the sweep
static const float MIST_BLINK   = 0.16f;   // mist blink dash

static Color FlameCol(int lv) {
    return lv >= 5 ? C(255, 236, 145) : (lv >= 3 ? C(255, 170, 55) : C(255, 115, 45));
}

static Color FlameCore(int lv) {
    return lv >= 4 ? C(255, 250, 205) : C(255, 215, 120);
}

static void FlameWake(Effects& fx, Vector2 p, int facing, float scale, int lv) {
    Color flame = FlameCol(lv);
    Color core = FlameCore(lv);
    int n = (int)(4 * scale + lv);
    for (int i = 0; i < n; i++) {
        fx.Ember({ p.x - facing * frnd(2, 46) * scale, p.y + frnd(-34, 18) * scale });
    }
    fx.Sparks(p, facing > 0 ? 180.0f : 0.0f, 52, 2 + lv / 2, core, 360 * scale, 2.5f * scale);
    if (GetRandomValue(0, 2) == 0)
        fx.SlashArc({ p.x + facing * 8.0f, p.y - 4.0f }, 42.0f * scale,
                    facing > 0 ? -42.0f : 222.0f,
                    facing > 0 ? 42.0f : 138.0f, flame);
}

static void FlameBurstLine(Effects& fx, Vector2 p, int facing, float len, float scale, int lv) {
    Color flame = FlameCol(lv);
    Color core = FlameCore(lv);
    int n = (int)(10 * scale + lv * 2);
    for (int i = 0; i < n; i++) {
        float t = (i + frnd(-0.2f, 0.2f)) / fmaxf((float)(n - 1), 1.0f);
        Vector2 q = { p.x + facing * len * Clampf(t, 0, 1),
                      p.y + frnd(-34, 20) * scale - sinf(t * PI) * 28.0f * scale };
        fx.Sparks(q, facing > 0 ? 0.0f : 180.0f, 70, 1, (i % 3 == 0) ? core : flame,
                  310.0f * scale, 2.8f * scale);
        if (i % 2 == 0) fx.Ember(q);
    }
}

void Player::Reset(Vector2 spawn) {
    pos = spawn; vel = {0, 0};
    facing = 1;
    maxHp = cfg::P_MAX_HP; hp = maxHp;
    for (int i = 0; i < STYLE_COUNT; i++) cd[i] = 0;
    for (int i = 0; i < WATER_FORM_COUNT; i++) waterCd[i] = 0;
    for (int i = 0; i < FLAME_FORM_COUNT; i++) flameCd[i] = 0;
    waterForm = -1; flameForm = -1; formSeg = 0; formTick = 0; deadCalmT = 0; flameGuardT = 0;
    iframes = 0;
    hitMem.Clear();
    state = PState::Normal;
    onGround = false;
    stateTimer = 0; comboStage = 0;
    comboQueued = false; didHitFrame = false;
    multiAttackId = -1; waterTick = 0; waterPass = 0;
    loveSeg = 0; serpentTick = 0;
    crouchT = 0; chillT = 0; hiddenT = 0; mistAmbushT = 0;
    invincible = false;
    hurtLen = 0.28f;
    hurtFlash = 0; runPhase = 0; wasAirborne = false;
    huntX = 0; hasHunt = false;
    techs.clear(); zones.clear(); ghosts.clear();
}

Rectangle Player::Rect() const {
    // crouching lowers the top of the hurtbox — the feet stay planted
    float drop = 20.0f * crouchT;
    return { pos.x - w * 0.5f, pos.y - h * 0.5f + drop, w, h - drop };
}

void Player::AddHit(CombatSystem& cs, Effects& fx, Rectangle r, float dmg,
                    float kbx, float kby, float life, HitKind kind, int id, int style) {
    if (style >= 0 && prog) dmg *= prog->DmgMult(style);
    if (mistAmbushT > 0) {                       // strike from the mist
        dmg *= 2.0f;
        mistAmbushT = 0;
        fx.Text({ pos.x, pos.y - h - 8 }, C(230, 238, 255), 1.25f, "AMBUSH x2");
        fx.MistBurst({ pos.x + facing * 30.0f, pos.y });
        PlaySfx(SFX_HIT, 0.9f, 0.55f);
    }
    cs.Add(r, dmg, kbx, kby, life, Team::Player, kind, id);
}

void Player::StartCombo(int stage) {
    state = PState::Attack;
    comboStage = stage;
    stateTimer = 0;
    comboQueued = false;
    didHitFrame = false;
    PlaySfx(SFX_SLASH, 0.45f, 1.0f + stage * 0.06f);
}

void Player::UpdateAttack(float dt, CombatSystem& cs, Effects& fx) {
    stateTimer += dt;
    const ComboStep& c = COMBO[comboStage - 1];

    // small forward drift through windup+active, then brake
    if (stateTimer < c.windup + c.active) vel.x = facing * 100.0f;
    else vel.x *= 1.0f - Clampf(10.0f * dt, 0, 1);

    if (!didHitFrame && stateTimer >= c.windup) {
        didHitFrame = true;
        Rectangle r = {
            facing > 0 ? pos.x + 4 : pos.x - 4 - c.range,
            pos.y - 36, c.range, 68
        };
        AddHit(cs, fx, r, c.dmg, facing * c.kb, -140, 0.05f,
               HitKind::Basic, cs.NewId(), -1);
        // alternate slash sweep direction per stage
        bool upSweep = (comboStage % 2 == 0);
        float a0 = upSweep ? 55.0f : -75.0f;
        float a1 = upSweep ? -55.0f : (comboStage == 4 ? 85.0f : 55.0f);
        if (facing < 0) { a0 = 180 - a0; a1 = 180 - a1; }
        fx.SlashArc(pos, c.range * 0.85f, a0, a1, C(220, 240, 255));
    }

    bool atkP = IsKeyPressed(KEY_J) || IsKeyPressed(KEY_X);
    if (atkP && comboStage < 4 && stateTimer > c.windup) comboQueued = true;

    if (stateTimer >= c.windup + c.active + c.recover) {
        if (comboQueued) StartCombo(comboStage + 1);
        else { state = PState::Normal; comboStage = 0; }
    }
}

void Player::UpdateTechs(float dt, CombatSystem& cs, Effects& fx) {
    // traveling techniques: wind tornadoes / stone ground-splitter
    for (auto& t : techs) {
        t.life -= dt;
        if (t.isQuake) {
            t.pos.x += t.dir * 540.0f * dt;
            fx.QuakeTrail({ t.pos.x, cfg::GROUND_Y });
            float dmg = 40.0f * (prog ? prog->DmgMult(STYLE_STONE) : 1.0f);
            cs.Add({ t.pos.x - 30, cfg::GROUND_Y - 72, 60, 72 }, dmg,
                   t.dir * 520.0f, -300, 0.03f, Team::Player, HitKind::Stone, t.curId);
        } else {
            t.pos.x += t.dir * 360.0f * dt;
            fx.WindSpiral(t.pos);
            t.tickT -= dt;
            if (t.tickT <= 0) { t.tickT = 0.18f; t.curId = cs.NewId(); }
            float dmg = 8.0f * (prog ? prog->DmgMult(STYLE_WIND) : 1.0f);
            cs.Add({ t.pos.x - 45, t.pos.y - 95, 90, 160 }, dmg,
                   t.dir * 120.0f, -430, 0.03f, Team::Player, HitKind::Wind, t.curId);
        }
        if (t.pos.x < -80 || t.pos.x > cfg::SCREEN_W + 80) t.life = 0;
    }
    techs.erase(std::remove_if(techs.begin(), techs.end(),
                [](const AirTech& t) { return t.life <= 0; }), techs.end());

    // lingering zones: burning ground / slowing mist
    for (auto& z : zones) {
        z.life -= dt;
        z.tickT -= dt;
        if (z.isFire) {
            if (GetRandomValue(0, 1) == 0)
                fx.Ember({ z.pos.x + frnd(-70, 70), z.pos.y - frnd(0, 24) });
            if (z.tickT <= 0) {
                z.tickT = 0.4f;
                float dmg = 6.0f * (prog ? prog->DmgMult(STYLE_FIRE) : 1.0f);
                cs.Add({ z.pos.x - 78, z.pos.y - 56, 156, 60 }, dmg, 0, -50,
                       0.03f, Team::Player, HitKind::Fire, cs.NewId());
            }
        } else {
            if (GetRandomValue(0, 1) == 0)
                fx.MistWisp({ z.pos.x + frnd(-130, 130), z.pos.y - frnd(0, 70) });
            if (z.tickT <= 0) {
                z.tickT = 0.3f;
                // zero damage: pure slow field
                cs.Add({ z.pos.x - 150, z.pos.y - 95, 300, 100 }, 0, 0, 0,
                       0.03f, Team::Player, HitKind::Water, cs.NewId());
            }
        }
    }
    zones.erase(std::remove_if(zones.begin(), zones.end(),
                [](const FieldZone& z) { return z.life <= 0; }), zones.end());

    for (auto& g : ghosts) g.life -= dt;
    ghosts.erase(std::remove_if(ghosts.begin(), ghosts.end(),
                 [](const Ghost& g) { return g.life <= 0; }), ghosts.end());
}

// --- Water Breathing: eleven forms ------------------------------------------
bool Player::TryStartWaterForm(CombatSystem& cs, Effects& fx) {
    static const int FKEY[WATER_FORM_COUNT] = {
        KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE,
        KEY_SIX, KEY_SEVEN, KEY_EIGHT, KEY_NINE, KEY_ZERO, KEY_MINUS
    };
    static const char* FORM_CALL[WATER_FORM_COUNT] = {
        "SURFACE SLASH", "WATER WHEEL", "FLOWING DANCE", "STRIKING TIDE",
        "BLESSED RAIN", "WHIRLPOOL", "DROP RIPPLE", "WATERFALL BASIN",
        "SPLASHING FLOW", "CONSTANT FLUX", "DEAD CALM"
    };
    int f = -1;
    for (int i = 0; i < WATER_FORM_COUNT; i++)
        if (IsKeyPressed(FKEY[i])) { f = i; break; }
    if (f < 0) return false;
    if (waterCd[f] > 0) { PlaySfx(SFX_PICKUP, 0.22f, 0.55f); return false; }  // on cooldown

    waterForm = f;
    state = PState::WaterForm;
    stateTimer = 0; didHitFrame = false; formSeg = 0; formTick = 0;
    multiAttackId = cs.NewId();
    waterCd[f] = WaterFormBaseCd(f) * (prog ? prog->water.CdMult(f) : 1.0f);
    int lv = prog ? prog->water.Level(f) : 1;
    float flair = 0.85f + 0.12f * (lv - 1);

    // startup: face the target for pursuit forms, grant iframes to dash forms
    switch (f) {
        case WF_DROP_RIPPLE:
            if (hasHunt) facing = huntX > pos.x ? 1 : -1;
            iframes = fmaxf(iframes, 0.30f);
            break;
        case WF_CONSTANT_FLUX:  iframes = fmaxf(iframes, 0.42f); break;
        case WF_WATER_WHEEL:
        case WF_FLOWING_DANCE:  iframes = fmaxf(iframes, 0.26f); break;
        case WF_SPLASHING_FLOW:
        case WF_DEAD_CALM:      iframes = fmaxf(iframes, 0.20f); break;
        default: break;
    }
    fx.Text({ pos.x, pos.y - h - 18 }, C(165, 220, 255), 0.92f, "%s", FORM_CALL[f]);
    fx.WaterBurst({ pos.x + facing * 22.0f, pos.y - 4 });
    fx.WaterWake(pos, facing, flair);
    if (lv >= 5) fx.Ring(pos, 12, 86, 500, 6, C(200, 240, 255));
    PlaySfx(SFX_WATER, 0.35f, 1.0f + 0.03f * f);
    return true;
}

void Player::UpdateWaterForm(float dt, CombatSystem& cs, Effects& fx) {
    stateTimer += dt;
    if (!prog) { state = PState::Normal; return; }
    WaterForms& wf = prog->water;
    const int   f    = waterForm;
    const float dmgM = wf.DmgMult(f);
    const float rngM = wf.RangeMult(f);
    const float spdM = wf.SpeedMult(f);
    const int   lv   = wf.Level(f);
    const bool  maxed = wf.Maxed(f);
    const float flair = 0.85f + 0.12f * (lv - 1);
    const Color WCOL = C(120, 200, 255);
    vel.y = 0;                        // horizontal by default; vertical forms override

    switch (f) {
    // 1 — Water Surface Slash: a swift, clean horizontal cut with a short lunge
    case WF_SURFACE_SLASH: {
        if (stateTimer < 0.07f) {
            vel.x = facing * 360.0f * spdM;
            fx.WaterWake(pos, facing, 0.65f * flair);
        }
        else vel.x *= 1.0f - Clampf(12.0f * dt, 0, 1);
        if (!didHitFrame && stateTimer >= 0.07f) {
            didHitFrame = true;
            float range = 98.0f * rngM;
            Rectangle r = { facing > 0 ? pos.x + 4 : pos.x - 4 - range, pos.y - 40, range, 80 };
            AddHit(cs, fx, r, 24.0f * dmgM, facing * 300, -150, 0.06f, HitKind::Water, cs.NewId(), -1);
            float a0 = facing > 0 ? -70.0f : 250.0f, a1 = facing > 0 ? 70.0f : 110.0f;
            fx.SlashArc(pos, range * 0.9f, a0, a1, WCOL);
            fx.WaterSlashWave({ pos.x + facing * 10.0f, pos.y + 8.0f }, facing, range * 1.15f, 46.0f, flair);
            fx.WaterBurst({ pos.x + facing * 42.0f, pos.y - 6 });
            fx.Ring({ pos.x + facing * 36.0f, pos.y }, 6, 78.0f * rngM, 620, 4, C(190, 235, 255));
            fx.AddHitstop(0.03f); PlaySfx(SFX_WATER, 0.7f, 1.25f);
        }
        // Level 5: a second surface cut flows straight out of the first
        if (maxed && formSeg == 0 && stateTimer >= 0.16f) {
            formSeg = 1;
            float range = 118.0f * rngM;
            Rectangle r = { facing > 0 ? pos.x + 4 : pos.x - 4 - range, pos.y - 44, range, 88 };
            AddHit(cs, fx, r, 18.0f * dmgM, facing * 340, -180, 0.06f, HitKind::Water, cs.NewId(), -1);
            float a0 = facing > 0 ? 70.0f : 110.0f, a1 = facing > 0 ? -70.0f : 250.0f;
            fx.SlashArc(pos, range * 0.9f, a0, a1, C(180, 225, 255));
            fx.WaterSlashWave({ pos.x + facing * 12.0f, pos.y - 4.0f }, facing, range * 1.2f, 34.0f, 1.15f * flair);
            fx.WaterWake(pos, facing, 1.1f * flair);
            PlaySfx(SFX_WATER, 0.6f, 1.4f);
        }
        if (stateTimer >= (maxed ? 0.34f : 0.26f)) state = PState::Normal;
        break;
    }
    // 2 — Water Wheel: a forward somersault, the blade carving a full circle
    case WF_WATER_WHEEL: {
        float dur = 0.5f / spdM;
        vel.x = facing * 380.0f * spdM;
        vel.y = -sinf(Clampf(stateTimer / dur, 0, 1) * PI) * 520.0f;   // rise then fall
        formTick -= dt;
        if (formTick <= 0) { formTick = 0.08f; multiAttackId = cs.NewId(); }
        float R = 72.0f * rngM;
        Rectangle r = { pos.x - R, pos.y - R, R * 2, R * 2 };
        AddHit(cs, fx, r, 16.0f * dmgM, facing * 200, maxed ? -380 : -140, 0.05f,
               HitKind::Water, multiAttackId, -1);
        fx.WaterTrail(pos, facing);
        fx.WaterSpiral(pos, R * 0.95f, (float)facing, 0.8f * flair);
        if (fmodf(stateTimer, 0.09f) < dt) {
            fx.Ring(pos, R * 0.35f, R * 1.2f, 340, maxed ? 8 : 5, WCOL);
            if (maxed) fx.WaterWake(pos, facing, 0.8f * flair);
        }
        if (stateTimer >= dur) { state = PState::Normal; }
        break;
    }
    // 3 — Flowing Dance: a gliding chain of graceful slashes
    case WF_FLOWING_DANCE: {
        vel.x = facing * 560.0f * spdM;
        vel.y = sinf(stateTimer * 26.0f) * 55.0f;
        formTick -= dt;
        if (formTick <= 0) {
            formTick = 0.10f; multiAttackId = cs.NewId(); formSeg++;
            float a0 = facing > 0 ? -60.0f : 240.0f, a1 = facing > 0 ? 60.0f : 120.0f;
            if (formSeg % 2 == 0) { float t = a0; a0 = a1; a1 = t; }
            fx.SlashArc(pos, 82.0f * rngM, a0, a1, C(140, 210, 255));
            fx.WaterSlashWave(pos, facing, 94.0f * rngM, 34.0f, 0.75f * flair);
            PlaySfx(SFX_SLASH, 0.34f, 1.35f);
        }
        Rectangle r = { pos.x - 70.0f * rngM + facing * 36, pos.y - 38, 140.0f * rngM, 76 };
        AddHit(cs, fx, r, 11.0f * dmgM, facing * 130, -120, 0.03f, HitKind::Water, multiAttackId, -1);
        fx.WaterTrail(pos, facing);
        fx.WaterWake(pos, facing, 0.45f * flair);
        if (formSeg >= (maxed ? 7 : 5)) { state = PState::Normal; vel.x = facing * 140.0f; }
        break;
    }
    // 4 — Striking Tide: rapid consecutive strikes rooted in place
    case WF_STRIKING_TIDE: {
        vel.x *= 1.0f - Clampf(10.0f * dt, 0, 1);
        int strikes = maxed ? 4 : 3;
        formTick -= dt;
        if (formTick <= 0 && formSeg < strikes) {
            formTick = 0.12f / spdM; formSeg++;
            bool last = (formSeg == strikes);
            vel.x = facing * (last ? 420.0f : 230.0f) * spdM;
            float range = (last ? 122.0f : 96.0f) * rngM;
            Rectangle r = { facing > 0 ? pos.x : pos.x - range, pos.y - 44, range, 88 };
            AddHit(cs, fx, r, (last ? 28.0f : 14.0f) * dmgM, facing * (last ? 540 : 180),
                   last ? -280 : -90, 0.06f, HitKind::Water, cs.NewId(), -1);
            float a0 = facing > 0 ? -75.0f : 255.0f, a1 = facing > 0 ? 75.0f : 105.0f;
            fx.SlashArc(pos, range * 0.9f, a0, a1, last ? C(90, 180, 255) : C(150, 215, 255));
            fx.WaterSlashWave({ pos.x + facing * 8.0f, pos.y + 4.0f }, facing, range * 1.1f,
                              last ? 58.0f : 38.0f, last ? 1.05f * flair : 0.7f * flair);
            fx.WaterWake(pos, facing, last ? 1.05f * flair : 0.55f * flair);
            if (last) { fx.WaterBurst({ pos.x + facing * 52.0f, pos.y }); fx.AddShake(0.25f);
                        fx.AddHitstop(0.05f); }
            PlaySfx(SFX_WATER, last ? 0.8f : 0.4f, last ? 0.9f : 1.35f);
        }
        if (formSeg >= strikes && formTick <= 0) { state = PState::Normal; }
        break;
    }
    // 5 — Blessed Rain After the Drought: one merciful, decisive cut that heals
    case WF_BLESSED_RAIN: {
        vel.x *= 1.0f - Clampf(8.0f * dt, 0, 1);
        if (stateTimer < 0.2f && GetRandomValue(0, 1) == 0) {
            fx.WaterTrail({ pos.x + frnd(-34, 34), pos.y - 44 }, facing);   // falling rain
            if (fmodf(stateTimer, 0.055f) < dt)
                fx.WaterColumn({ pos.x + frnd(-58, 58), pos.y + 18.0f }, 135.0f, 22.0f, 0.35f * flair);
        }
        if (!didHitFrame && stateTimer >= 0.2f) {
            didHitFrame = true;
            float range = 122.0f * rngM;
            Rectangle r = { facing > 0 ? pos.x : pos.x - range, pos.y - 52, range, 104 };
            AddHit(cs, fx, r, 55.0f * dmgM, facing * 360, -220, 0.08f, HitKind::Water, cs.NewId(), -1);
            float a0 = facing > 0 ? -90.0f : 270.0f, a1 = facing > 0 ? 90.0f : 90.0f;
            fx.SlashArc(pos, range, a0, a1, C(205, 238, 255));
            fx.WaterColumn({ pos.x + facing * 50.0f, pos.y + 24.0f }, 160.0f * rngM, 52.0f * rngM, flair);
            fx.WaterSlashWave({ pos.x + facing * 8.0f, pos.y + 10.0f }, facing, range * 1.05f, 28.0f, 0.8f * flair);
            fx.WaterBurst({ pos.x + facing * 52.0f, pos.y - 6 });
            fx.Ring({ pos.x + facing * 52.0f, pos.y }, 10, 124.0f * rngM, 520, 6, C(190, 228, 255));
            fx.AddShake(0.35f); fx.AddHitstop(0.07f);
            Heal(10.0f + 4.0f * (wf.Level(f) - 1), fx);   // relief after the drought
            PlaySfx(SFX_WATER, 0.9f, 0.82f);
        }
        if (stateTimer >= 0.5f) state = PState::Normal;
        break;
    }
    // 6 — Whirlpool: spin in place, dragging the horde inward and shredding it
    case WF_WHIRLPOOL: {
        float dur = 0.72f + 0.03f * (lv - 1);
        vel.x *= 1.0f - Clampf(6.0f * dt, 0, 1);
        formTick -= dt;
        if (formTick <= 0) { formTick = 0.10f; multiAttackId = cs.NewId(); }
        float R = 96.0f * rngM;
        // each side pulls toward the centre (negative knockback inward)
        AddHit(cs, fx, { pos.x, pos.y - R, R, 2 * R }, 10.0f * dmgM, -240, -40, 0.05f,
               HitKind::Water, multiAttackId, -1);
        AddHit(cs, fx, { pos.x - R, pos.y - R, R, 2 * R }, 10.0f * dmgM, 240, -40, 0.05f,
               HitKind::Water, multiAttackId, -1);
        fx.WaterTrail({ pos.x + cosf(stateTimer * 22.0f) * R * 0.6f,
                        pos.y + sinf(stateTimer * 22.0f) * 22.0f }, facing);
        fx.WaterSpiral(pos, R, (float)facing, 0.9f * flair);
        if (fmodf(stateTimer, 0.14f) < dt) fx.Ring(pos, 20, R * 1.25f, 300, 6, WCOL);
        if (stateTimer >= dur) {
            if (maxed) {
                fx.WaterBurst(pos);
                fx.Ring(pos, 28, R * 1.45f, 620, 9, C(205, 240, 255));
                fx.AddShake(0.2f);
            }
            state = PState::Normal;
        }
        break;
    }
    // 7 — Drop Ripple Thrust: the fastest, most direct piercing lunge
    case WF_DROP_RIPPLE: {
        vel.x = facing * 1160.0f * spdM;
        Rectangle r = { pos.x - 30 + facing * 20, pos.y - 26, 60, 52 };
        AddHit(cs, fx, r, 30.0f * dmgM, facing * 260, -120, 0.05f, HitKind::Water, multiAttackId, -1);
        fx.WaterTrail(pos, facing);
        fx.WaterWake(pos, facing, 0.9f * flair);
        formTick -= dt;
        if (formTick <= 0) {
            formTick = 0.045f;
            fx.WaterSlashWave({ pos.x - facing * 8.0f, pos.y + 4.0f }, facing, 110.0f * rngM, 18.0f, 0.55f * flair);
        }
        if (stateTimer < dt) { fx.Ring(pos, 8, 62, 520, 5, C(150, 210, 255));
                               PlaySfx(SFX_WHOOSH, 0.7f, 1.3f); }
        if (stateTimer >= (maxed ? 0.28f : 0.20f)) { state = PState::Normal; vel.x = facing * 150.0f; }
        break;
    }
    // 8 — Waterfall Basin: leap, then crash a column of water straight down
    case WF_WATERFALL_BASIN: {
        float rise = 0.16f;
        if (stateTimer < rise) {
            vel.y = -430.0f; vel.x = facing * 120.0f;
            fx.WaterWake(pos, facing, 0.55f * flair);
        } else {
            vel.y = 980.0f; vel.x = facing * 70.0f;
            fx.WaterColumn({ pos.x + facing * 22.0f, pos.y + 56.0f }, 128.0f, 38.0f, 0.38f * flair);
        }
        if (stateTimer >= rise) {
            Rectangle col = { facing > 0 ? pos.x : pos.x - 72.0f * rngM, pos.y - 30, 72.0f * rngM, 112 };
            AddHit(cs, fx, col, 18.0f * dmgM, facing * 120, 60, 0.04f, HitKind::Water, multiAttackId, -1);
            fx.WaterTrail({ pos.x + facing * 28.0f, pos.y }, facing);
        }
        if (onGround && stateTimer > rise) {
            float R = 132.0f * rngM;
            Rectangle aoe = { pos.x - R, pos.y + h * 0.4f - 30, 2 * R, 56 };
            AddHit(cs, fx, aoe, 34.0f * dmgM, 0, maxed ? -320 : -240, 0.06f, HitKind::Water, cs.NewId(), -1);
            fx.Ring({ pos.x, pos.y + h * 0.4f }, 12, R, 520, 8, C(150, 205, 255));
            fx.WaterColumn({ pos.x, pos.y + h * 0.45f }, 190.0f * rngM, 86.0f * rngM, 1.05f * flair);
            fx.WaterBurst({ pos.x, pos.y + h * 0.3f });
            fx.Dust({ pos.x, pos.y + h * 0.5f });
            fx.AddShake(0.4f); fx.AddHitstop(0.06f);
            PlaySfx(SFX_STONE, 0.55f, 1.15f); PlaySfx(SFX_WATER, 0.6f, 1.0f);
            state = PState::Normal;
        }
        if (stateTimer > 1.3f) state = PState::Normal;   // safety
        break;
    }
    // 9 — Splashing Water Flow, Turbulent: a defensive churn that shields the slayer
    case WF_SPLASHING_FLOW: {
        float dur = 0.8f + (maxed ? 0.3f : 0.0f);
        iframes = fmaxf(iframes, 0.12f);                 // protected through the churn
        vel.x *= 1.0f - Clampf(5.0f * dt, 0, 1);
        formTick -= dt;
        if (formTick <= 0) { formTick = 0.12f; multiAttackId = cs.NewId(); }
        float R = 80.0f * rngM;
        AddHit(cs, fx, { pos.x - R, pos.y - R, 2 * R, 2 * R }, 7.0f * dmgM, facing * 80, -30, 0.05f,
               HitKind::Water, multiAttackId, -1);
        fx.WaterTrail({ pos.x + cosf(stateTimer * 30.0f) * 42.0f,
                        pos.y - 10 + sinf(stateTimer * 30.0f) * 30.0f }, facing);
        fx.WaterSpiral(pos, R * 0.9f, (float)-facing, 0.75f * flair);
        if (fmodf(stateTimer, 0.12f) < dt)
            fx.Ring(pos, R * 0.45f, R * 1.2f, 260, maxed ? 7 : 4, C(170, 225, 255));
        if (stateTimer >= dur) {
            if (maxed) {
                fx.Ring(pos, 22, R * 1.45f, 520, 8, C(220, 248, 255));
                fx.WaterBurst(pos);
            }
            state = PState::Normal;
        }
        break;
    }
    // 10 — Constant Flux: the river flows back and forth through the enemy line
    case WF_CONSTANT_FLUX: {
        float passDur = 0.34f;
        vel.x = facing * 900.0f * spdM;
        formTick -= dt;
        if (formTick <= 0) {
            formTick = 0.06f; multiAttackId = cs.NewId();
            fx.WaterSlashWave({ pos.x - facing * 12.0f, pos.y + 8.0f }, facing, 135.0f * rngM, 28.0f, 0.65f * flair);
        }
        Rectangle r = { pos.x - 80 + facing * 45, pos.y - 42, 160, 84 };
        AddHit(cs, fx, r, 13.0f * dmgM, facing * 140, -150, 0.03f, HitKind::Water, multiAttackId, -1);
        fx.WaterTrail(pos, facing);
        fx.WaterWake(pos, facing, 0.75f * flair);
        if (stateTimer >= passDur) {
            int passes = 2 + (wf.Level(f) - 1) / 2;      // L1:2  L3:3  L5:4
            formSeg++;
            if (formSeg >= passes) {
                if (maxed) {                             // mastery: a final surging burst
                    Rectangle fin = { facing > 0 ? pos.x : pos.x - 150, pos.y - 60, 150, 120 };
                    AddHit(cs, fx, fin, 40.0f * dmgM, facing * 500, -320, 0.07f,
                           HitKind::Water, cs.NewId(), -1);
                    fx.WaterBurst({ pos.x + facing * 50.0f, pos.y });
                    fx.WaterSlashWave(pos, facing, 190.0f * rngM, 58.0f, 1.25f * flair);
                    fx.Ring(pos, 12, 152, 600, 8, WCOL); fx.AddShake(0.4f); fx.AddHitstop(0.06f);
                }
                state = PState::Normal; vel.x = facing * 130.0f;
            } else {
                stateTimer = 0; facing = -facing; multiAttackId = cs.NewId();
                iframes = fmaxf(iframes, passDur + 0.05f);
                fx.WaterBurst(pos);
                fx.Ring(pos, 8, 92.0f * rngM, 520, 5, C(190, 235, 255));
                PlaySfx(SFX_WATER, 0.7f, 1.15f);
            }
        }
        break;
    }
    // 11 — Dead Calm: total stillness; incoming attacks are nullified, then released
    default: {   // WF_DEAD_CALM
        float dur = 0.55f + 0.06f * (wf.Level(f) - 1);
        vel.x *= 1.0f - Clampf(9.0f * dt, 0, 1);
        deadCalmT = fmaxf(deadCalmT, 0.06f);             // nullify enemy hits this frame
        iframes = fmaxf(iframes, 0.12f);
        if (fmodf(stateTimer, 0.2f) < dt) {
            fx.Ring(pos, 10, 92, 150, 4, C(150, 200, 235));
            fx.Ring(pos, 36, 132.0f * rngM, 210, 3, C(215, 245, 255));
        }
        fx.WaterSpiral(pos, 70.0f * rngM, 0.35f, 0.35f * flair);
        if (stateTimer >= dur) {
            float R = 150.0f * rngM;                     // release: a calm-shattering ripple
            AddHit(cs, fx, { pos.x - R, pos.y - 90, 2 * R, 180 }, 20.0f * dmgM, 0, -160, 0.06f,
                   HitKind::Water, cs.NewId(), -1);
            AddHit(cs, fx, { pos.x, pos.y - 90, R, 180 }, 0, 380, -120, 0.05f,
                   HitKind::Water, cs.NewId(), -1);
            AddHit(cs, fx, { pos.x - R, pos.y - 90, R, 180 }, 0, -380, -120, 0.05f,
                   HitKind::Water, cs.NewId(), -1);
            fx.Ring(pos, 14, R, 560, 10, C(170, 215, 245));
            fx.WaterSlashWave({ pos.x - R * 0.15f, pos.y + 10.0f }, 1, R * 1.05f, 42.0f, flair);
            fx.WaterSlashWave({ pos.x + R * 0.15f, pos.y + 10.0f }, -1, R * 1.05f, 42.0f, flair);
            fx.WaterSpiral(pos, R * 0.72f, 1.0f, 1.3f * flair);
            fx.WaterBurst(pos); fx.AddShake(0.35f); fx.AddHitstop(0.05f);
            PlaySfx(SFX_WATER, 0.9f, 0.7f);
            state = PState::Normal;
        }
        break;
    }
    }
}

// --- Flame Breathing: nine forms --------------------------------------------
bool Player::TryStartFlameForm(CombatSystem& cs, Effects& fx) {
    static const int FKEY[FLAME_FORM_COUNT] = {
        KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE,
        KEY_SIX, KEY_SEVEN, KEY_EIGHT, KEY_NINE
    };
    static const char* FORM_CALL[FLAME_FORM_COUNT] = {
        "UNKNOWING FIRE", "RISING SCORCHING SUN", "BLAZING UNIVERSE",
        "BLOOMING FLAME UNDULATION", "FLAME TIGER", "SOLAR HEAT HAZE",
        "INFERNO WHEEL", "CRIMSON LOTUS CREST", "NINTH FORM: RENGOKU"
    };
    int f = -1;
    for (int i = 0; i < FLAME_FORM_COUNT; i++)
        if (IsKeyPressed(FKEY[i])) { f = i; break; }
    if (f < 0) return false;
    if (flameCd[f] > 0) { PlaySfx(SFX_PICKUP, 0.22f, 0.55f); return false; }

    flameForm = f;
    state = PState::FlameForm;
    stateTimer = 0; didHitFrame = false; formSeg = 0; formTick = 0;
    multiAttackId = cs.NewId();
    flameCd[f] = FlameFormBaseCd(f) * (prog ? prog->flame.CdMult(f) : 1.0f);
    int lv = prog ? prog->flame.Level(f) : 1;
    float flair = 0.9f + 0.13f * (lv - 1);
    if (hasHunt && (f == FF_UNKNOWING_FIRE || f == FF_FLAME_TIGER ||
                    f == FF_SOLAR_HEAT_HAZE || f == FF_RENGOKU))
        facing = huntX > pos.x ? 1 : -1;

    switch (f) {
        case FF_UNKNOWING_FIRE:
        case FF_SOLAR_HEAT_HAZE:
            iframes = fmaxf(iframes, 0.22f + 0.03f * lv);
            break;
        case FF_BLOOMING_UNDULATION:
            flameGuardT = 0.78f + 0.09f * lv;
            iframes = fmaxf(iframes, flameGuardT);
            break;
        case FF_RENGOKU:
            iframes = fmaxf(iframes, 0.62f + 0.04f * lv);
            fx.Flash(C(255, 120, 45), 0.18f + lv * 0.025f);
            break;
        default:
            iframes = fmaxf(iframes, 0.12f + 0.02f * lv);
            break;
    }

    fx.Text({ pos.x, pos.y - h - 18 }, FlameCol(lv), 0.92f, "%s", FORM_CALL[f]);
    FlameWake(fx, pos, facing, flair, lv);
    if (lv >= 4) fx.Ring(pos, 12, 92.0f * flair, 520, 6, FlameCol(lv));
    PlaySfx(SFX_FIRE, f == FF_RENGOKU ? 1.0f : 0.65f, 1.18f - 0.035f * f);
    return true;
}

void Player::UpdateFlameForm(float dt, CombatSystem& cs, Effects& fx) {
    stateTimer += dt;
    if (!prog) { state = PState::Normal; return; }
    FlameForms& ff = prog->flame;
    const int f = flameForm;
    const int lv = ff.Level(f);
    const bool maxed = ff.Maxed(f);
    const float dmgM = ff.DmgMult(f);
    const float rngM = ff.RangeMult(f);
    const float spdM = ff.SpeedMult(f);
    const float flair = 0.9f + 0.13f * (lv - 1);
    const Color FC = FlameCol(lv);
    const Color CORE = FlameCore(lv);
    vel.y = 0;

    switch (f) {
    // 1 - Unknowing Fire: explosive straight-line draw slash
    case FF_UNKNOWING_FIRE: {
        float dur = (maxed ? 0.34f : 0.27f) / spdM;
        vel.x = facing * 1120.0f * spdM;
        FlameWake(fx, pos, facing, 0.8f * flair, lv);
        Rectangle r = { pos.x - 70.0f + facing * 48.0f, pos.y - 42,
                        140.0f * rngM, 84 };
        AddHit(cs, fx, r, 22.0f * dmgM, facing * 360, -160, 0.035f,
               HitKind::Fire, multiAttackId, -1);
        if (!didHitFrame && stateTimer > 0.07f) {
            didHitFrame = true;
            fx.SlashArc(pos, 92.0f * rngM, facing > 0 ? -65.0f : 245.0f,
                        facing > 0 ? 65.0f : 115.0f, CORE);
            FlameBurstLine(fx, { pos.x - facing * 38.0f, pos.y + 4 }, facing,
                           170.0f * rngM, flair, lv);
            PlaySfx(SFX_WHOOSH, 0.75f, 1.35f);
        }
        if (stateTimer >= dur) { state = PState::Normal; vel.x = facing * 150.0f; }
        break;
    }
    // 2 - Rising Scorching Sun: vertical launcher and anti-air
    case FF_RISING_SUN: {
        vel.x *= 1.0f - Clampf(9.0f * dt, 0, 1);
        if (stateTimer < 0.18f) {
            vel.y = -360.0f * spdM;
            FlameWake(fx, { pos.x + facing * 18.0f, pos.y }, facing, 0.55f * flair, lv);
        }
        if (!didHitFrame && stateTimer >= 0.08f) {
            didHitFrame = true;
            float wdt = 126.0f * rngM;
            Rectangle r = { pos.x - wdt * 0.5f, pos.y - 118.0f * rngM, wdt, 156.0f * rngM };
            AddHit(cs, fx, r, 30.0f * dmgM, facing * 150, -520, 0.07f,
                   HitKind::Fire, cs.NewId(), -1);
            fx.SlashArc({ pos.x + facing * 8.0f, pos.y - 8.0f }, 98.0f * rngM,
                        facing > 0 ? 135.0f : 45.0f,
                        facing > 0 ? -35.0f : 215.0f, FC);
            fx.Ring({ pos.x + facing * 18.0f, pos.y - 42.0f }, 10, 102.0f * rngM, 560, 7, CORE);
            fx.AddHitstop(0.04f); PlaySfx(SFX_FIRE, 0.75f, 1.25f);
        }
        if (stateTimer >= 0.42f) state = PState::Normal;
        break;
    }
    // 3 - Blazing Universe: heavy overhead sunfall, strong boss punish
    case FF_BLAZING_UNIVERSE: {
        vel.x *= 1.0f - Clampf(8.0f * dt, 0, 1);
        if (stateTimer < 0.28f) {
            fx.FireCharge({ pos.x + facing * 30.0f, pos.y - 44.0f });
            if (fmodf(stateTimer, 0.08f) < dt) fx.Ring(pos, 16, 90.0f * flair, 480, 5, FC);
        } else if (!didHitFrame) {
            didHitFrame = true;
            float reach = 164.0f * rngM;
            Rectangle r = { facing > 0 ? pos.x - 8.0f : pos.x - reach + 8.0f,
                            pos.y - 92.0f, reach, 154.0f };
            AddHit(cs, fx, r, 58.0f * dmgM, facing * 520, -360, 0.08f,
                   HitKind::Fire, cs.NewId(), -1);
            Vector2 c = { pos.x + facing * reach * 0.55f, pos.y - 12.0f };
            fx.SlashArc(c, 122.0f * rngM, facing > 0 ? -140.0f : 320.0f,
                        facing > 0 ? 78.0f : 102.0f, FC);
            fx.FireExplosion(c);
            fx.AddShake(0.45f); fx.AddHitstop(0.08f);
            PlaySfx(SFX_EXPLO, 0.82f, 1.05f);
        }
        if (stateTimer >= 0.68f) state = PState::Normal;
        break;
    }
    // 4 - Blooming Flame Undulation: defensive circular guard
    case FF_BLOOMING_UNDULATION: {
        float dur = 0.82f + 0.06f * lv;
        vel.x *= 1.0f - Clampf(10.0f * dt, 0, 1);
        flameGuardT = fmaxf(flameGuardT, fmaxf(dur - stateTimer, 0));
        formTick -= dt;
        float R = 112.0f * rngM;
        if (formTick <= 0) {
            formTick = 0.11f;
            multiAttackId = cs.NewId();
            formSeg++;
            PlaySfx(SFX_SLASH, 0.35f, 1.2f + 0.06f * formSeg);
        }
        AddHit(cs, fx, { pos.x - R, pos.y - R, R * 2.0f, R * 2.0f },
               8.5f * dmgM, 0, -120, 0.04f, HitKind::Fire, multiAttackId, -1);
        fx.SlashArc(pos, R, stateTimer * 720.0f, stateTimer * 720.0f + 170.0f, FC);
        fx.Ring(pos, R * 0.72f, R * 1.05f, 360, 6, CORE);
        if (fmodf(stateTimer, 0.10f) < dt) {
            fx.Sparks({ pos.x + frnd(-R, R), pos.y + frnd(-82, 64) }, -90, 360,
                      3 + lv / 2, CORE, 280, 2.6f);
            fx.AddShake(0.04f);
        }
        if (stateTimer >= dur) {
            flameGuardT = 0;
            fx.Ring(pos, 24, R * 1.32f, 620, 8, FC);
            state = PState::Normal;
        }
        break;
    }
    // 5 - Flame Tiger: three lunging bites that pin large targets
    case FF_FLAME_TIGER: {
        int bites = maxed ? 4 : 3;
        formTick -= dt;
        if (formTick <= 0 && formSeg < bites) {
            formTick = 0.16f / spdM;
            formSeg++;
            multiAttackId = cs.NewId();
            vel.x = facing * (760.0f + 80.0f * formSeg) * spdM;
            float reach = (142.0f + 14.0f * formSeg) * rngM;
            Rectangle r = { facing > 0 ? pos.x - 10.0f : pos.x - reach + 10.0f,
                            pos.y - 56.0f, reach, 112.0f };
            AddHit(cs, fx, r, (formSeg == bites ? 30.0f : 16.0f) * dmgM,
                   facing * (formSeg == bites ? 540.0f : 260.0f), -210, 0.06f,
                   HitKind::Fire, multiAttackId, -1);
            Vector2 c = { pos.x + facing * reach * 0.45f, pos.y - 6.0f };
            fx.SlashArc(c, 82.0f + formSeg * 12.0f, -48, 62, FC);
            FlameBurstLine(fx, { pos.x, pos.y }, facing, reach, 0.75f * flair, lv);
            if (formSeg == bites) { fx.FireExplosion(c); fx.AddShake(0.34f); fx.AddHitstop(0.05f); }
            PlaySfx(formSeg == bites ? SFX_EXPLO : SFX_FIRE, 0.55f + 0.08f * formSeg,
                    1.0f - 0.04f * formSeg);
        }
        FlameWake(fx, pos, facing, 0.8f * flair, lv);
        if (formSeg >= bites && formTick <= 0) { state = PState::Normal; vel.x = facing * 120.0f; }
        break;
    }
    // 6 - Solar Heat Haze: original feint that phases through and backstabs
    case FF_SOLAR_HEAT_HAZE: {
        float dur = (maxed ? 0.48f : 0.38f) / spdM;
        vel.x = facing * 980.0f * spdM;
        FlameWake(fx, pos, facing, 0.6f * flair, lv);
        formTick -= dt;
        if (formTick <= 0) {
            formTick = 0.09f;
            multiAttackId = cs.NewId();
            Rectangle r = { pos.x - 54.0f + facing * 34.0f, pos.y - 42.0f,
                            108.0f * rngM, 84.0f };
            AddHit(cs, fx, r, 12.0f * dmgM, facing * 80, -80, 0.04f,
                   HitKind::Fire, multiAttackId, -1);
            fx.SlashArc(pos, 72.0f * rngM, facing > 0 ? -32.0f : 212.0f,
                        facing > 0 ? 32.0f : 148.0f, Fade(FC, 0.85f));
        }
        if (!didHitFrame && stateTimer >= dur * 0.62f) {
            didHitFrame = true;
            if (hasHunt) {
                pos.x = Clampf(huntX - facing * 62.0f, 30.0f, (float)cfg::SCREEN_W - 30.0f);
                facing = huntX > pos.x ? 1 : -1;
            }
            Rectangle r = { facing > 0 ? pos.x : pos.x - 132.0f * rngM,
                            pos.y - 48.0f, 132.0f * rngM, 96.0f };
            AddHit(cs, fx, r, 34.0f * dmgM, facing * 420, -190, 0.06f,
                   HitKind::Fire, cs.NewId(), -1);
            fx.FireExplosion({ pos.x + facing * 58.0f, pos.y - 4.0f });
            fx.AddHitstop(0.04f);
            PlaySfx(SFX_WHOOSH, 0.8f, 1.45f);
        }
        if (stateTimer >= dur) state = PState::Normal;
        break;
    }
    // 7 - Inferno Wheel: original rolling crowd-clearer
    case FF_INFERNO_WHEEL: {
        float dur = 0.62f + 0.04f * lv;
        vel.x = facing * 520.0f * spdM;
        formTick -= dt;
        if (formTick <= 0) { formTick = 0.085f; multiAttackId = cs.NewId(); }
        float R = 88.0f * rngM;
        AddHit(cs, fx, { pos.x - R, pos.y - R, R * 2.0f, R * 2.0f },
               13.0f * dmgM, facing * 170, -260, 0.045f, HitKind::Fire, multiAttackId, -1);
        fx.SlashArc(pos, R, stateTimer * 1100.0f, stateTimer * 1100.0f + 230.0f, FC);
        fx.Ring(pos, R * 0.58f, R * 0.98f, 390, 6, CORE);
        FlameWake(fx, pos, facing, 0.7f * flair, lv);
        if (stateTimer >= dur) {
            Rectangle fin = { pos.x - 118.0f * rngM, pos.y - 70.0f,
                              236.0f * rngM, 132.0f };
            AddHit(cs, fx, fin, 26.0f * dmgM, facing * 360, -330, 0.06f,
                   HitKind::Fire, cs.NewId(), -1);
            fx.FireExplosion({ pos.x + facing * 36.0f, pos.y - 6.0f });
            fx.AddShake(0.28f);
            state = PState::Normal;
        }
        break;
    }
    // 8 - Crimson Lotus Crest: original ranged flame crest and burn field
    case FF_CRIMSON_LOTUS: {
        vel.x *= 1.0f - Clampf(8.0f * dt, 0, 1);
        if (stateTimer < 0.20f) {
            fx.FireCharge({ pos.x + facing * 34.0f, pos.y - 12.0f });
        } else {
            if (formSeg == 0) {
                formSeg = 1;
                PlaySfx(SFX_FIRE, 0.8f, 0.82f);
            }
            float len = 270.0f * rngM;
            float waveX = pos.x + facing * (52.0f + stateTimer * 440.0f * spdM);
            Rectangle r = { facing > 0 ? pos.x + 12.0f : pos.x - len - 12.0f,
                            pos.y - 62.0f, len, 124.0f };
            AddHit(cs, fx, r, 10.0f * dmgM, facing * 180, -120, 0.04f,
                   HitKind::Fire, multiAttackId, -1);
            FlameBurstLine(fx, { pos.x + facing * 14.0f, pos.y + 8.0f }, facing,
                           len, 1.05f * flair, lv);
            if (!didHitFrame && stateTimer >= 0.46f) {
                didHitFrame = true;
                Vector2 c = { Clampf(waveX, 60.0f, (float)cfg::SCREEN_W - 60.0f), cfg::GROUND_Y - 36.0f };
                AddHit(cs, fx, { c.x - 90.0f * rngM, c.y - 76.0f,
                                 180.0f * rngM, 108.0f }, 32.0f * dmgM,
                       facing * 320, -260, 0.07f, HitKind::Fire, cs.NewId(), -1);
                fx.FireExplosion(c);
                FieldZone z;
                z.pos = { c.x, cfg::GROUND_Y };
                z.life = (maxed ? 5.4f : 3.8f); z.tickT = 0; z.isFire = true;
                zones.push_back(z);
                fx.AddShake(0.34f); fx.AddHitstop(0.05f);
            }
        }
        if (stateTimer >= 0.62f) state = PState::Normal;
        break;
    }
    // 9 - Rengoku: ultimate destructive charge
    case FF_RENGOKU: {
        float dur = 0.95f + 0.04f * lv;
        if (stateTimer < 0.30f) {
            vel.x *= 1.0f - Clampf(8.0f * dt, 0, 1);
            fx.FireCharge(pos);
            fx.Ring(pos, 20.0f + stateTimer * 170.0f, 120.0f + stateTimer * 170.0f,
                    420, 7, FC);
        } else {
            vel.x = facing * 1320.0f * spdM;
            FlameWake(fx, pos, facing, 1.25f * flair, lv);
            Rectangle r = { pos.x - 126.0f + facing * 86.0f, pos.y - 82.0f,
                            252.0f * rngM, 164.0f };
            AddHit(cs, fx, r, 48.0f * dmgM, facing * 660, -430, 0.04f,
                   HitKind::Fire, multiAttackId, -1);
            if (!didHitFrame && stateTimer >= 0.58f) {
                didHitFrame = true;
                Vector2 c = { pos.x + facing * 88.0f, pos.y - 8.0f };
                AddHit(cs, fx, { c.x - 150.0f * rngM, c.y - 118.0f,
                                 300.0f * rngM, 218.0f }, 72.0f * dmgM,
                       facing * 760, -520, 0.10f, HitKind::Fire, cs.NewId(), -1);
                fx.FireExplosion(c);
                fx.FireExplosion({ c.x + facing * 64.0f, c.y + 8.0f });
                fx.Ring(c, 24, 220.0f * rngM, 620, 14, FC);
                fx.Flash(C(255, 170, 55), 0.24f + lv * 0.025f);
                fx.AddShake(0.75f); fx.AddHitstop(0.12f);
                PlaySfx(SFX_EXPLO, 1.0f, 0.75f);
            }
        }
        if (stateTimer >= dur) { state = PState::Normal; vel.x = facing * 110.0f; }
        break;
    }
    default:
        state = PState::Normal;
        break;
    }
}

void Player::Update(float dt, CombatSystem& cs, Effects& fx) {
    UpdateTechs(dt, cs, fx);     // techniques outlive states, even death throes

    if (state == PState::Dead) {
        vel.x *= 1.0f - Clampf(6.0f * dt, 0, 1);
        vel.y += cfg::GRAVITY * dt;
        pos.x += vel.x * dt; pos.y += vel.y * dt;
        onGround = GroundClamp(pos, vel, h * 0.5f);
        return;
    }

    for (int i = 0; i < STYLE_COUNT; i++) cd[i] = fmaxf(cd[i] - dt, 0);
    for (int i = 0; i < WATER_FORM_COUNT; i++) waterCd[i] = fmaxf(waterCd[i] - dt, 0);
    for (int i = 0; i < FLAME_FORM_COUNT; i++) flameCd[i] = fmaxf(flameCd[i] - dt, 0);
    deadCalmT = fmaxf(deadCalmT - dt, 0);
    flameGuardT = fmaxf(flameGuardT - dt, 0);
    iframes = fmaxf(iframes - dt, 0);
    hurtFlash = fmaxf(hurtFlash - dt, 0);
    hiddenT = fmaxf(hiddenT - dt, 0);
    chillT = fmaxf(chillT - dt, 0);
    mistAmbushT = fmaxf(mistAmbushT - dt, 0);

    bool left  = IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT);
    bool right = IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT);
    bool up    = IsKeyDown(KEY_W) || IsKeyDown(KEY_UP);
    bool down  = IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN);

    // crouch: hold Left Shift while grounded (smooth in, smooth out)
    bool wantCrouch = IsKeyDown(KEY_LEFT_SHIFT) && onGround && state == PState::Normal;
    crouchT += ((wantCrouch ? 1.0f : 0.0f) - crouchT) * Clampf(11.0f * dt, 0, 1);
    if (crouchT < 0.02f) crouchT = wantCrouch ? crouchT : 0.0f;

    float move = (right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f);
    bool jumpP = IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_SPACE);
    bool atkP  = IsKeyPressed(KEY_J) || IsKeyPressed(KEY_X);
    // Breathing Styles activate on the number-key row 1..6. Styles may share a
    // key (Water & Stone are both 1); only the equipped style ever responds.
    const int numKey[6] = { KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE, KEY_SIX };
    bool styleP[STYLE_COUNT];
    for (int i = 0; i < STYLE_COUNT; i++)
        styleP[i] = IsKeyPressed(numKey[StyleKeyNumber(i) - 1]);

    // only the equipped Breathing Style is available for the run; the
    // rest are locked out even if the player presses their keys
    for (int i = 0; i < STYLE_COUNT; i++)
        if (i != equipped) styleP[i] = false;

    switch (state) {
        case PState::Normal: {
            float spdBonus = hiddenT > 0 ? 1.4f : 1.0f;    // swift in the mist
            spdBonus *= 1.0f - 0.55f * crouchT;            // low stance, careful steps
            if (chillT > 0) spdBonus *= 0.6f;              // frozen to the bone
            vel.x = move * cfg::P_SPEED * spdBonus;
            if (move != 0) {
                facing = move > 0 ? 1 : -1;
                runPhase += dt * 11.0f;
            }
            if (jumpP && onGround) {
                vel.y = cfg::P_JUMP_VEL;
                onGround = false;
                fx.Dust({ pos.x, pos.y + h * 0.5f });
            }
            if (atkP) {
                if (up) {                                   // launcher
                    state = PState::UpSlash;
                    stateTimer = 0; didHitFrame = false;
                    PlaySfx(SFX_SLASH, 0.5f, 1.25f);
                } else if (down && !onGround) {             // plunge
                    state = PState::Plunge;
                    stateTimer = 0; didHitFrame = false;
                    multiAttackId = cs.NewId();
                    vel.x *= 0.35f;
                    PlaySfx(SFX_WHOOSH, 0.55f, 1.3f);
                } else {
                    StartCombo(1);
                }
                break;
            }
            if (equipped == STYLE_WATER) {
                // Water Breathing: eleven forms, each on its own number key
                TryStartWaterForm(cs, fx);
            }
            else if (equipped == STYLE_FIRE) {
                // Flame Breathing: nine forms, each on its own number key
                TryStartFlameForm(cs, fx);
            }
            else if (styleP[STYLE_STONE] && cd[STYLE_STONE] <= 0) {
                state = PState::Stone;
                stateTimer = 0; didHitFrame = false;
                vel.x = 0;
                PlaySfx(SFX_WHOOSH, 0.5f, 0.7f);
            }
            else if (styleP[STYLE_LOVE] && cd[STYLE_LOVE] <= 0) {
                state = PState::Love;
                stateTimer = 0;
                loveSeg = 0;
                cd[STYLE_LOVE] = cfg::LOVE_CD * prog->CdMult(STYLE_LOVE);
                multiAttackId = cs.NewId();
                int segs = prog->Mastery(STYLE_LOVE) ? 5 : 3;
                iframes = fmaxf(iframes, segs * LOVE_SEG_T + 0.1f);
                if (hasHunt) facing = huntX > pos.x ? 1 : -1;
                PlaySfx(SFX_LOVE, 0.85f);
            }
            else if (styleP[STYLE_SERPENT] && cd[STYLE_SERPENT] <= 0) {
                state = PState::Serpent;
                stateTimer = 0;
                cd[STYLE_SERPENT] = cfg::SERPENT_CD * prog->CdMult(STYLE_SERPENT);
                multiAttackId = cs.NewId();
                serpentTick = 0.1f;
                iframes = fmaxf(iframes, 0.35f);
                if (hasHunt) facing = huntX > pos.x ? 1 : -1;
                PlaySfx(SFX_SERPENT, 0.9f);
            }
            else if (styleP[STYLE_WIND] && cd[STYLE_WIND] <= 0) {
                state = PState::Wind;
                stateTimer = 0; didHitFrame = false;
                vel.x = 0;
                PlaySfx(SFX_WHOOSH, 0.45f, 0.9f);
            }
            else if (styleP[STYLE_MIST] && cd[STYLE_MIST] <= 0) {
                state = PState::Mist;
                stateTimer = 0;
                cd[STYLE_MIST] = cfg::MIST_CD * prog->CdMult(STYLE_MIST);
                iframes = fmaxf(iframes, MIST_BLINK + 0.1f);
                fx.MistBurst(pos);
                ghosts.push_back({ pos, facing, 0.9f });
                PlaySfx(SFX_MIST, 0.9f);
            }
            break;
        }
        case PState::Attack:
            UpdateAttack(dt, cs, fx);
            break;

        case PState::UpSlash: {
            stateTimer += dt;
            vel.x *= 1.0f - Clampf(8.0f * dt, 0, 1);
            if (!didHitFrame && stateTimer >= 0.07f) {
                didHitFrame = true;
                vel.y = fminf(vel.y, -190.0f);              // small hop with the swing
                Rectangle r = {
                    facing > 0 ? pos.x - 10 : pos.x - 64,
                    pos.y - 100, 74, 104
                };
                AddHit(cs, fx, r, 14, facing * 120, -430, 0.06f,
                       HitKind::Basic, cs.NewId(), -1);     // launches enemies
                float a0 = facing > 0 ? 30.0f : 150.0f;
                float a1 = facing > 0 ? -110.0f : 290.0f;
                fx.SlashArc(pos, 78, a0, a1, C(220, 240, 255));
            }
            if (stateTimer >= 0.07f + 0.08f + 0.2f) { state = PState::Normal; }
            break;
        }
        case PState::Plunge: {
            stateTimer += dt;
            vel.y = 950.0f;
            // falling blade hitbox under the player
            Rectangle r = { pos.x - 24, pos.y, 48, h * 0.9f + 20 };
            AddHit(cs, fx, r, 14, facing * 90, 220, 0.03f,
                   HitKind::Basic, multiAttackId, -1);
            if (onGround) {                                  // impact
                Rectangle aoe = { pos.x - 62, pos.y + h * 0.5f - 42, 124, 52 };
                AddHit(cs, fx, aoe, 16, 0, -360, 0.05f,
                       HitKind::Basic, cs.NewId(), -1);
                fx.Dust({ pos.x, pos.y + h * 0.5f });
                fx.Ring({ pos.x, pos.y + h * 0.5f }, 8, 85, 420, 6, C(200, 210, 230));
                fx.AddShake(0.3f);
                fx.AddHitstop(0.05f);
                PlaySfx(SFX_HIT, 0.7f, 0.8f);
                state = PState::Normal;
                stateTimer = 0;
            }
            break;
        }
        case PState::Water: {
            stateTimer += dt;
            float dur = WATER_TIME * prog->ReachMult(STYLE_WATER);
            vel.x = facing * 830.0f;
            vel.y = 0;
            fx.WaterTrail(pos, facing);
            // multi-hit: re-arm the attack id a few times across the dash
            waterTick -= dt;
            if (waterTick <= 0) {
                waterTick = 0.07f;
                multiAttackId = cs.NewId();
            }
            Rectangle r = { pos.x - 80 + facing * 45, pos.y - 42, 160, 84 };
            AddHit(cs, fx, r, 12, facing * 130, -160, 0.03f,
                   HitKind::Water, multiAttackId, STYLE_WATER);
            if (stateTimer >= dur) {
                if (prog->Mastery(STYLE_WATER) && waterPass == 0) {
                    // constant flux: the river flows back
                    waterPass = 1;
                    stateTimer = 0;
                    facing = -facing;
                    multiAttackId = cs.NewId();
                    waterTick = 0.07f;
                    iframes = fmaxf(iframes, dur + 0.05f);
                    PlaySfx(SFX_WATER, 0.7f, 1.2f);
                } else {
                    state = PState::Normal;
                    vel.x = facing * 120.0f;
                }
            }
            break;
        }
        case PState::WaterForm:
            UpdateWaterForm(dt, cs, fx);
            break;
        case PState::FlameForm:
            UpdateFlameForm(dt, cs, fx);
            break;
        case PState::FireWindup: {
            stateTimer += dt;
            vel.x = 0;
            fx.FireCharge({ pos.x + facing * 20.0f, pos.y - 10 });
            if (stateTimer >= FIRE_WINDUP) {
                float R = 135.0f * prog->ReachMult(STYLE_FIRE)
                          * (prog->Mastery(STYLE_FIRE) ? 1.15f : 1.0f);
                Vector2 c = { pos.x + facing * (60.0f + R * 0.3f), pos.y + 4 };
                AddHit(cs, fx, { c.x - R, c.y - R, R * 2, R * 2 }, 55,
                       facing * 520, -280, 0.06f, HitKind::Fire, cs.NewId(), STYLE_FIRE);
                fx.FireExplosion(c);
                fx.AddShake(0.55f);
                fx.AddHitstop(0.09f);
                PlaySfx(SFX_EXPLO, 0.9f);
                if (prog->Mastery(STYLE_FIRE)) {            // rengoku: burning ground
                    FieldZone z;
                    z.pos = { c.x, cfg::GROUND_Y };
                    z.life = 4.5f; z.tickT = 0; z.isFire = true;
                    zones.push_back(z);
                }
                cd[STYLE_FIRE] = cfg::FIRE_CD * prog->CdMult(STYLE_FIRE);
                state = PState::FireRecover;
                stateTimer = 0;
                vel.x = -facing * 140.0f;   // recoil
            }
            break;
        }
        case PState::FireRecover: {
            stateTimer += dt;
            vel.x *= 1.0f - Clampf(8.0f * dt, 0, 1);
            if (stateTimer > 0.45f) state = PState::Normal;
            break;
        }
        case PState::Stone: {
            stateTimer += dt;
            vel.x = 0;
            if (stateTimer < STONE_WINDUP) {
                if (GetRandomValue(0, 2) == 0)
                    fx.Dust({ pos.x + frnd(-30, 30), pos.y + h * 0.5f });
            }
            else if (!didHitFrame) {
                didHitFrame = true;
                float wdt = 150.0f * prog->ReachMult(STYLE_STONE);
                Vector2 impact = { pos.x + facing * wdt * 0.45f, pos.y + h * 0.5f - 8 };
                Rectangle r = {
                    facing > 0 ? pos.x : pos.x - wdt,
                    pos.y - 70, wdt, 130
                };
                AddHit(cs, fx, r, 70, facing * 620, -320, 0.07f,
                       HitKind::Stone, cs.NewId(), STYLE_STONE);
                fx.StoneSlam(impact);
                fx.AddShake(0.7f);
                fx.AddHitstop(0.12f);
                PlaySfx(SFX_STONE, 1.0f);
                if (prog->Mastery(STYLE_STONE)) {           // earth splitter
                    AirTech t;
                    t.pos = { pos.x + facing * 70.0f, cfg::GROUND_Y - 30 };
                    t.dir = facing;
                    t.life = 0.9f * prog->ReachMult(STYLE_STONE);
                    t.isQuake = true;
                    t.curId = cs.NewId();
                    techs.push_back(t);
                }
                cd[STYLE_STONE] = cfg::STONE_CD * prog->CdMult(STYLE_STONE);
            }
            if (stateTimer >= STONE_TOTAL) state = PState::Normal;
            break;
        }
        case PState::Love: {
            stateTimer += dt;
            int segs = prog->Mastery(STYLE_LOVE) ? 5 : 3;
            if (stateTimer >= LOVE_SEG_T) {
                stateTimer = 0;
                loveSeg++;
                if (loveSeg >= segs) {
                    state = PState::Normal;
                    vel.x = facing * 100.0f;
                    break;
                }
                if (hasHunt) facing = huntX > pos.x ? 1 : -1;
                else facing = -facing;                      // flourish
                multiAttackId = cs.NewId();
                vel.y = -110.0f;                            // flutter hop
                PlaySfx(SFX_SLASH, 0.4f, 1.4f);
            }
            vel.x = facing * 880.0f * prog->ReachMult(STYLE_LOVE);
            fx.LoveSparkle(pos, facing);
            Rectangle r = { pos.x - 65 + facing * 38, pos.y - 36, 130, 72 };
            AddHit(cs, fx, r, 10, facing * 120, -140, 0.03f,
                   HitKind::Love, multiAttackId, STYLE_LOVE);
            break;
        }
        case PState::Serpent: {
            stateTimer += dt;
            if (hasHunt) facing = huntX > pos.x ? 1 : -1;   // twisting pursuit
            vel.x = facing * 460.0f * prog->ReachMult(STYLE_SERPENT);
            vel.y = sinf(stateTimer * 34.0f) * 210.0f;      // serpentine weave
            fx.SerpentTrail(pos, facing);
            serpentTick -= dt;
            if (serpentTick <= 0) {
                serpentTick = 0.1f;
                multiAttackId = cs.NewId();
                PlaySfx(SFX_SLASH, 0.3f, 1.55f);
            }
            Rectangle r = { pos.x - 55 + facing * 30, pos.y - 34, 110, 68 };
            AddHit(cs, fx, r, 8, facing * 80, -120, 0.03f,
                   HitKind::Serpent, multiAttackId, STYLE_SERPENT);
            if (stateTimer >= SERP_TIME) {
                if (prog->Mastery(STYLE_SERPENT)) {         // twin fangs
                    Rectangle fang = {
                        facing > 0 ? pos.x : pos.x - 140,
                        pos.y - 50, 140, 100
                    };
                    AddHit(cs, fx, fang, 30, facing * 420, -260, 0.06f,
                           HitKind::Serpent, cs.NewId(), STYLE_SERPENT);
                    fx.Sparks({ pos.x + facing * 60.0f, pos.y }, facing > 0 ? 0.0f : 180.0f,
                              70, 14, C(140, 230, 80), 420, 3);
                    fx.Ring({ pos.x + facing * 60.0f, pos.y }, 10, 90, 420, 6, C(120, 220, 90));
                    fx.AddHitstop(0.05f);
                    PlaySfx(SFX_SERPENT, 0.9f, 0.75f);
                }
                state = PState::Normal;
                vel.y = 0;
            }
            break;
        }
        case PState::Wind: {
            stateTimer += dt;
            vel.x = 0;
            if (stateTimer < WIND_WINDUP) {
                if (GetRandomValue(0, 1) == 0)
                    fx.WindSpiral({ pos.x + frnd(-40, 40), pos.y + frnd(-30, 30) });
            }
            else if (!didHitFrame) {
                didHitFrame = true;
                float rm = prog->ReachMult(STYLE_WIND);
                Rectangle r = { pos.x - 150 * rm, pos.y - 84, 300 * rm, 154 };
                AddHit(cs, fx, r, 20, facing * 260, -440, 0.06f,
                       HitKind::Wind, cs.NewId(), STYLE_WIND);
                fx.SlashArc(pos, 130 * rm, -180, 180, C(210, 245, 225));
                fx.Ring(pos, 20, 170 * rm, 700, 8, C(210, 245, 225));
                fx.AddShake(0.4f);
                fx.AddHitstop(0.06f);
                PlaySfx(SFX_WIND, 1.0f);
                // tornado(s)
                AirTech t;
                t.pos = { pos.x + facing * 40.0f, pos.y - 20 };
                t.dir = facing;
                t.life = 2.2f * rm;
                t.tickT = 0; t.isQuake = false;
                techs.push_back(t);
                if (prog->Mastery(STYLE_WIND)) {            // twin cyclones
                    t.dir = -facing;
                    t.pos = { pos.x - facing * 40.0f, pos.y - 20 };
                    techs.push_back(t);
                }
                cd[STYLE_WIND] = cfg::WIND_CD * prog->CdMult(STYLE_WIND);
            }
            if (stateTimer >= WIND_WINDUP + 0.35f) state = PState::Normal;
            break;
        }
        case PState::Mist: {
            stateTimer += dt;
            vel.x = facing * 1250.0f * prog->ReachMult(STYLE_MIST);
            vel.y = 0;
            if (stateTimer >= MIST_BLINK) {
                fx.MistBurst(pos);
                ghosts.push_back({ pos, facing, 0.9f });
                hiddenT = 1.8f * prog->ReachMult(STYLE_MIST);
                mistAmbushT = hiddenT + 0.6f;
                if (prog->Mastery(STYLE_MIST)) {            // sea of clouds
                    FieldZone z;
                    z.pos = { pos.x, cfg::GROUND_Y };
                    z.life = 5.5f; z.tickT = 0; z.isFire = false;
                    zones.push_back(z);
                }
                state = PState::Normal;
            }
            break;
        }
        case PState::Hurt: {
            stateTimer += dt;
            vel.x *= 1.0f - Clampf(4.5f * dt, 0, 1);
            if (stateTimer > hurtLen) state = PState::Normal;
            break;
        }
        default: break;
    }

    // physics (dashes and weaves manage their own vertical motion)
    bool noGravity = (state == PState::Water || state == PState::WaterForm ||
                      state == PState::FlameForm ||
                      state == PState::Plunge ||
                      state == PState::Serpent || state == PState::Mist);
    if (!noGravity) vel.y += cfg::GRAVITY * dt;
    if (state == PState::Love) vel.y = fminf(vel.y, 260.0f);
    pos.x += vel.x * dt;
    pos.y += vel.y * dt;
    pos.x = Clampf(pos.x, 30.0f, (float)cfg::SCREEN_W - 30.0f);

    bool grounded = GroundClamp(pos, vel, h * 0.5f);
    if (grounded && wasAirborne && state != PState::Plunge)
        fx.Dust({ pos.x, pos.y + h * 0.5f });
    onGround = grounded;
    wasAirborne = !grounded;
}

bool Player::TakeDamage(float dmg, float kbx, Effects& fx, bool heavy) {
    if (invincible) {
        iframes = fmaxf(iframes, 0.05f);
        return false;
    }
    if (iframes > 0 || state == PState::Dead) return false;
    hp -= dmg;
    hurtFlash = 0.2f;
    iframes = cfg::P_IFRAMES;
    hiddenT = 0;                 // a hit tears away the mist
    hurtLen = heavy ? 0.55f : 0.28f;
    vel.x = kbx;
    vel.y = heavy ? -430.0f : -260.0f;
    // blood answers every wound - the harder the blow, the wider the spray
    fx.BloodSpray({ pos.x, pos.y - 8 }, kbx > 0 ? 1 : -1,
                  heavy ? 2.1f : Clampf(0.9f + dmg * 0.035f, 0.9f, 1.8f));
    if (heavy) {
        // a Demon King's blow: sent flying like in the films
        fx.AddShake(0.65f);
        fx.AddHitstop(0.08f);
        fx.Sparks({ pos.x, pos.y - 6 }, kbx > 0 ? 0.0f : 180.0f, 70, 16,
                  C(255, 90, 70), 540, 3.5f);
        fx.Ring({ pos.x, pos.y - 6 }, 8, 90, 480, 6, C(255, 120, 100));
        PlaySfx(SFX_STONE, 0.65f, 1.25f);
    } else {
        fx.AddShake(Clampf(0.22f + dmg * 0.012f, 0, 0.6f));
        fx.AddHitstop(0.03f);
    }
    fx.Text({ pos.x, pos.y - h }, C(255, 80, 80), 1.1f, "-%.0f", dmg);
    PlaySfx(SFX_HURT, 0.8f);
    if (hp <= 0) {
        hp = 0;
        state = PState::Dead;
        fx.AddShake(0.85f);
        fx.AddHitstop(0.3f);
        fx.DeathBurst(pos, C(60, 140, 150), 1.4f);
        PlaySfx(SFX_DEATH, 1.0f, 0.6f);
    } else {
        state = PState::Hurt;
        stateTimer = 0;
        comboStage = 0;
    }
    return true;
}

void Player::Heal(float amount, Effects& fx) {
    if (state == PState::Dead) return;
    float healed = fminf(amount, maxHp - hp);
    if (healed <= 0.5f) return;
    hp += healed;
    fx.Text({ pos.x, pos.y - h }, C(110, 230, 120), 1.0f, "+%.0f", healed);
    fx.Sparks({ pos.x, pos.y }, -90, 60, 8, C(110, 230, 120), 160, 2.5f);
}

// --- drawing -----------------------------------------------------

void Player::Draw() const {
    float gt = (float)GetTime();

    // lingering zones (under everything)
    for (const auto& z : zones) {
        float a = Clampf(z.life / 1.5f, 0, 1);
        if (z.isFire) {
            float pulse = 0.16f + 0.06f * sinf(gt * 9.0f);
            DrawEllipse((int)z.pos.x, (int)z.pos.y - 4, 84, 18, Fade(C(255, 120, 40), pulse * a));
            DrawEllipse((int)z.pos.x, (int)z.pos.y - 4, 50, 10, Fade(C(255, 210, 120), pulse * a));
        } else {
            DrawEllipse((int)z.pos.x, (int)z.pos.y - 34, 165, 66, Fade(C(180, 188, 205), 0.13f * a));
            DrawEllipse((int)z.pos.x, (int)z.pos.y - 20, 120, 40, Fade(C(200, 205, 220), 0.10f * a));
        }
    }

    // traveling techniques
    for (const auto& t : techs) {
        if (t.isQuake) {
            DrawCircleV({ t.pos.x, cfg::GROUND_Y - 8 }, 10, Fade(C(150, 140, 128), 0.7f));
        } else {
            // stacked wobbling rings = tornado
            for (int k = 0; k < 5; k++) {
                float wob = sinf(gt * 22.0f + k * 1.4f) * (4.0f + k * 2.0f);
                float ry = t.pos.y + 40 - k * 30.0f;
                float rr = 14.0f + k * 8.0f;
                DrawRing({ t.pos.x + wob, ry }, rr - 4, rr + 4, 0, 360, 24,
                         Fade(C(212, 242, 228), 0.30f - k * 0.03f));
            }
        }
    }

    // mist afterimage decoys
    for (const auto& g : ghosts) {
        float a = Clampf(g.life / 0.9f, 0, 1) * 0.35f;
        DrawRectangleRounded({ g.pos.x - 10, g.pos.y - 18, 20, 28 }, 0.35f, 4,
                             Fade(C(150, 165, 190), a));
        DrawCircleV({ g.pos.x + g.facing * 2.0f, g.pos.y - 26 }, 9, Fade(C(150, 165, 190), a));
    }

    // ghost-blink while invincible; translucent while hidden in mist
    float alpha = 1.0f;
    if (hiddenT > 0) alpha = 0.32f;
    else if (iframes > 0 && state != PState::Water && state != PState::Love)
        alpha = fmodf(gt * 16.0f, 2.0f) < 1.0f ? 0.45f : 0.9f;

    // shadow
    DrawEllipse((int)pos.x, (int)(cfg::GROUND_Y + 6), 20, 6, Fade(BLACK, 0.35f * alpha));

    if (state == PState::Dead) {
        Rectangle body = { pos.x, pos.y + h * 0.5f - 12, h * 0.8f, 16 };
        DrawRectanglePro(body, { h * 0.4f, 8 }, 0, C(35, 80, 85));
        DrawCircleV({ pos.x - facing * h * 0.35f, pos.y + h * 0.5f - 12 }, 8, C(232, 196, 168));
        return;
    }

    Color jacket = C(26, 95, 95);       // dark teal haori
    Color jacket2 = C(40, 130, 120);
    Color skin = C(236, 202, 172);
    Color hair = C(32, 26, 38);
    if (hurtFlash > 0) { jacket = C(255, 120, 110); jacket2 = jacket; }
    if (state == PState::Stone && stateTimer < STONE_WINDUP) {
        float t = stateTimer / STONE_WINDUP;
        jacket = C((int)(26 + 90 * t), (int)(95 - 10 * t), (int)(95 - 15 * t));
    }

    float legSwing = (fabsf(vel.x) > 20 && onGround) ? sinf(runPhase) * 7.0f : 0.0f;
    legSwing *= 1.0f - 0.6f * crouchT;
    float bob = (fabsf(vel.x) > 20 && onGround) ? fabsf(cosf(runPhase)) * 2.0f : 0.0f;
    bob *= 1.0f - crouchT;
    float bx = pos.x, by = pos.y - bob;
    float bd = 11.0f * crouchT;              // crouch: the body sinks, feet stay planted

    // legs (bent when crouching)
    DrawRectangle((int)(bx - 8 + legSwing * 0.5f), (int)(by + 8 + bd), 7, (int)(20 - bd), Fade(C(25, 30, 40), alpha));
    DrawRectangle((int)(bx + 2 - legSwing * 0.5f), (int)(by + 8 + bd), 7, (int)(20 - bd), Fade(C(20, 24, 32), alpha));
    by += bd;                                // everything above rides lower
    // torso
    DrawRectangleRounded({ bx - 10, by - 18, 20, 28 }, 0.35f, 4, Fade(jacket, alpha));
    DrawRectangle((int)(bx - 10 + (facing > 0 ? 11 : 1)), (int)(by - 14), 8, 8, Fade(jacket2, alpha));
    DrawRectangle((int)(bx - 10 + (facing > 0 ? 2 : 10)), (int)(by - 4), 8, 8, Fade(jacket2, alpha));
    // belt
    DrawRectangle((int)(bx - 10), (int)(by + 6), 20, 4, Fade(C(220, 215, 200), alpha));
    // frost coating (Douma's chill)
    if (chillT > 0)
        DrawRectangleRounded({ bx - 11, by - 30, 22, 46 }, 0.4f, 4,
                             Fade(C(140, 200, 255), 0.26f * Clampf(chillT, 0, 1)));
    // head
    Vector2 headC = { bx + facing * 2.0f, by - 26 };
    DrawCircleV(headC, 9, Fade(skin, alpha));
    DrawCircleSector(headC, 10, 180, 360, 12, Fade(hair, alpha));   // hair over top half
    DrawRectangle((int)(headC.x - 10), (int)(headC.y - 3), 4, 7, Fade(hair, alpha));
    DrawCircleV({ headC.x + facing * 4.5f, headC.y + 1 }, 1.5f, Fade(C(30, 30, 40), alpha));
    // scabbard on back hip
    Rectangle scab = { bx - facing * 6.0f, by + 2, 26, 4 };
    DrawRectanglePro(scab, { 0, 2 }, facing > 0 ? 150.0f : 30.0f, Fade(C(20, 18, 26), alpha));

    // --- sword -----------------------------------------------------
    Vector2 hand = { bx + facing * 11.0f, by - 4 };
    float ang = 38.0f;     // 0 = pointing right, degrees, y-down clockwise
    bool mirror = facing < 0;
    bool spinBlade = false;
    switch (state) {
        case PState::Attack: {
            const ComboStep& c = COMBO[comboStage - 1];
            float total = c.windup + c.active;
            float t = Clampf(stateTimer / total, 0, 1);
            bool upSweep = (comboStage % 2 == 0);
            float a0 = upSweep ? 60.0f : -80.0f;
            float a1 = upSweep ? -60.0f : (comboStage == 4 ? 85.0f : 60.0f);
            ang = Lerpf(a0, a1, t);
            break;
        }
        case PState::UpSlash: {
            float t = Clampf(stateTimer / 0.15f, 0, 1);
            ang = Lerpf(35.0f, -125.0f, t);
            break;
        }
        case PState::Plunge:     ang = 90.0f + facing * 8.0f; break;
        case PState::Water:      ang = 0; break;
        case PState::WaterForm:
            switch (waterForm) {
                case WF_WATER_WHEEL:
                case WF_WHIRLPOOL:
                case WF_SPLASHING_FLOW:
                    ang = fmodf(gt * 1500.0f, 360.0f); spinBlade = true; break;
                case WF_WATERFALL_BASIN: ang = 78.0f; break;
                case WF_DROP_RIPPLE:
                case WF_CONSTANT_FLUX:   ang = 0; break;
                case WF_DEAD_CALM:       ang = 6.0f; break;
                case WF_BLESSED_RAIN:    ang = stateTimer < 0.2f ? -84.0f : 44.0f; break;
                default:                 ang = -28.0f + sinf(gt * 40.0f) * 22.0f; break;
            }
            break;
        case PState::FlameForm:
            switch (flameForm) {
                case FF_UNKNOWING_FIRE:
                case FF_SOLAR_HEAT_HAZE:
                case FF_RENGOKU:
                    ang = 0; break;
                case FF_RISING_SUN:
                    ang = -78.0f; break;
                case FF_BLAZING_UNIVERSE:
                    ang = stateTimer < 0.28f ? -106.0f : 68.0f; break;
                case FF_BLOOMING_UNDULATION:
                case FF_INFERNO_WHEEL:
                    ang = fmodf(gt * 1550.0f, 360.0f); spinBlade = true; break;
                case FF_FLAME_TIGER:
                    ang = sinf(gt * 42.0f) * 58.0f; break;
                case FF_CRIMSON_LOTUS:
                    ang = stateTimer < 0.20f ? -92.0f : 14.0f; break;
                default:
                    ang = -24.0f; break;
            }
            break;
        case PState::Love:       ang = fmodf(gt * 1300.0f, 360.0f); spinBlade = true; break;
        case PState::Serpent:    ang = 8.0f + sinf(gt * 46.0f) * 55.0f; break;
        case PState::Wind:
            if (stateTimer < WIND_WINDUP) ang = -95.0f;
            else { ang = fmodf((stateTimer - WIND_WINDUP) * 1700.0f, 360.0f); spinBlade = true; }
            break;
        case PState::Mist:       ang = 0; break;
        case PState::FireWindup: ang = -95.0f + sinf(gt * 40) * 4.0f; break;
        case PState::FireRecover: ang = 30; break;
        case PState::Stone:
            ang = stateTimer < STONE_WINDUP
                  ? -110.0f + sinf(gt * 30) * 3.0f          // heaved overhead
                  : 65.0f;                                  // buried in the earth
            break;
        default: break;
    }
    if (mirror && !spinBlade) ang = 180.0f - ang;

    // arm
    Vector2 shoulder = { bx + facing * 6.0f, by - 12 };
    DrawLineEx(shoulder, hand, 5, Fade(jacket2, alpha));
    // blade
    Rectangle blade = { hand.x, hand.y, 46, 4.5f };
    Color bladeCol = C(210, 220, 235);
    if (state == PState::Water || state == PState::WaterForm) bladeCol = C(120, 200, 255);
    if (state == PState::FireWindup || state == PState::FireRecover || state == PState::FlameForm)
        bladeCol = C(255, 170, 90);
    if (state == PState::Stone) bladeCol = C(190, 182, 170);
    if (state == PState::Love)  bladeCol = C(255, 150, 205);
    if (state == PState::Serpent) bladeCol = C(140, 225, 110);
    if (state == PState::Wind)  bladeCol = C(215, 248, 232);
    DrawRectanglePro(blade, { 0, 2.25f }, ang, Fade(bladeCol, alpha));
    Rectangle hilt = { hand.x, hand.y, 8, 7 };
    DrawRectanglePro(hilt, { 4, 3.5f }, ang, Fade(C(120, 40, 40), alpha));

    if (state == PState::WaterForm) {
        float pulse = 0.5f + 0.5f * sinf(gt * 14.0f);
        Color aura = C(120, 205, 255);
        switch (waterForm) {
            case WF_WATER_WHEEL:
            case WF_WHIRLPOOL:
            case WF_SPLASHING_FLOW:
                DrawRing(pos, 44.0f + pulse * 12.0f, 50.0f + pulse * 12.0f,
                         0, 360, 40, Fade(aura, 0.28f * alpha));
                DrawRing(pos, 72.0f, 77.0f, gt * 220.0f, gt * 220.0f + 250.0f,
                         32, Fade(C(210, 245, 255), 0.24f * alpha));
                break;
            case WF_WATERFALL_BASIN:
                DrawRectangle((int)(pos.x - 24), (int)(pos.y - 118), 48, 144,
                              Fade(aura, 0.12f * alpha));
                DrawLineEx({ pos.x - 20.0f, pos.y - 108.0f }, { pos.x - 20.0f, pos.y + 26.0f },
                           3.0f, Fade(C(230, 250, 255), 0.35f * alpha));
                DrawLineEx({ pos.x + 18.0f, pos.y - 108.0f }, { pos.x + 18.0f, pos.y + 26.0f },
                           3.0f, Fade(C(230, 250, 255), 0.28f * alpha));
                break;
            case WF_DROP_RIPPLE:
            case WF_CONSTANT_FLUX:
                DrawLineEx({ pos.x - facing * 78.0f, pos.y + 8.0f },
                           { pos.x + facing * 118.0f, pos.y + 8.0f },
                           8.0f, Fade(aura, 0.18f * alpha));
                DrawLineEx({ pos.x - facing * 52.0f, pos.y - 8.0f },
                           { pos.x + facing * 88.0f, pos.y - 8.0f },
                           4.0f, Fade(C(230, 250, 255), 0.24f * alpha));
                break;
            case WF_DEAD_CALM:
                DrawRing(pos, 52.0f, 55.0f, 0, 360, 48, Fade(C(220, 248, 255), 0.34f * alpha));
                DrawRing(pos, 104.0f, 107.0f, 0, 360, 64, Fade(aura, 0.18f * alpha));
                break;
            default: {
                float s0 = facing > 0 ? -72.0f : 108.0f;
                float s1 = facing > 0 ? 72.0f : 252.0f;
                DrawCircleSector({ pos.x + facing * 10.0f, pos.y + 4.0f },
                                 94.0f, s0, s1, 28, Fade(aura, 0.16f * alpha));
                break;
            }
        }
    }

    if (state == PState::FlameForm) {
        int lv = prog ? prog->flame.Level(flameForm) : 1;
        float pulse = 0.5f + 0.5f * sinf(gt * 16.0f);
        Color aura = FlameCol(lv);
        float scale = 1.0f + 0.12f * (lv - 1);
        switch (flameForm) {
            case FF_BLOOMING_UNDULATION:
                DrawRing(pos, 58.0f * scale, 64.0f * scale, 0, 360, 56,
                         Fade(C(255, 235, 160), 0.34f * alpha));
                DrawRing(pos, 104.0f * scale, 111.0f * scale, gt * 260.0f,
                         gt * 260.0f + 290.0f, 64, Fade(aura, 0.30f * alpha));
                break;
            case FF_RISING_SUN:
                DrawCircleSector({ pos.x + facing * 10.0f, pos.y - 22.0f },
                                 108.0f * scale,
                                 facing > 0 ? 130.0f : 50.0f,
                                 facing > 0 ? 275.0f : -95.0f,
                                 28, Fade(aura, 0.20f * alpha));
                break;
            case FF_BLAZING_UNIVERSE:
            case FF_CRIMSON_LOTUS:
                DrawCircleV({ pos.x + facing * 54.0f, pos.y - 20.0f },
                            30.0f + 10.0f * pulse * scale, Fade(aura, 0.18f * alpha));
                DrawCircleV({ pos.x + facing * 54.0f, pos.y - 20.0f },
                            14.0f + 6.0f * pulse * scale, Fade(C(255, 238, 170), 0.26f * alpha));
                break;
            case FF_FLAME_TIGER:
                DrawCircleSector({ pos.x + facing * 46.0f, pos.y - 2.0f },
                                 92.0f * scale, facing > 0 ? -52.0f : 232.0f,
                                 facing > 0 ? 58.0f : 122.0f, 26, Fade(aura, 0.22f * alpha));
                break;
            case FF_INFERNO_WHEEL:
                DrawRing(pos, 48.0f + pulse * 18.0f, 56.0f + pulse * 18.0f,
                         0, 360, 48, Fade(aura, 0.32f * alpha));
                DrawRing(pos, 88.0f * scale, 94.0f * scale, gt * 300.0f,
                         gt * 300.0f + 250.0f, 48, Fade(C(255, 230, 150), 0.24f * alpha));
                break;
            case FF_RENGOKU:
                DrawLineEx({ pos.x - facing * 104.0f, pos.y + 14.0f },
                           { pos.x + facing * 154.0f, pos.y - 8.0f },
                           12.0f * scale, Fade(aura, 0.22f * alpha));
                DrawRing(pos, 110.0f + pulse * 24.0f, 118.0f + pulse * 24.0f,
                         0, 360, 56, Fade(aura, 0.20f * alpha));
                break;
            default:
                DrawLineEx({ pos.x - facing * 64.0f, pos.y + 10.0f },
                           { pos.x + facing * 110.0f, pos.y - 6.0f },
                           8.0f * scale, Fade(aura, 0.18f * alpha));
                break;
        }
    }

    // slash flash while a combo hit is active
    if (state == PState::Attack && didHitFrame) {
        const ComboStep& c = COMBO[comboStage - 1];
        if (stateTimer < c.windup + c.active + 0.04f) {
            float s0 = facing > 0 ? -65.0f : 115.0f;
            float s1 = facing > 0 ? 65.0f : 245.0f;
            DrawCircleSector(pos, c.range, s0, s1, 24, Fade(C(190, 230, 255), 0.22f));
        }
    }
    if (state == PState::UpSlash && didHitFrame && stateTimer < 0.2f) {
        float s0 = facing > 0 ? -150.0f : 270.0f;
        float s1 = facing > 0 ? -30.0f : 390.0f;
        DrawCircleSector(pos, 92, s0, s1, 24, Fade(C(190, 230, 255), 0.22f));
    }
    // fire windup glow at blade tip
    if (state == PState::FireWindup) {
        Vector2 tip = { hand.x + cosf(ang * DEG2RAD) * 46, hand.y + sinf(ang * DEG2RAD) * 46 };
        DrawCircleV(tip, 5 + stateTimer * 26, Fade(C(255, 160, 50), 0.55f));
        DrawCircleV(tip, 3 + stateTimer * 14, Fade(C(255, 240, 170), 0.7f));
    }
    // stone windup: weight gathering on the blade
    if (state == PState::Stone && stateTimer < STONE_WINDUP) {
        Vector2 tip = { hand.x + cosf(ang * DEG2RAD) * 46, hand.y + sinf(ang * DEG2RAD) * 46 };
        DrawCircleV(tip, 4 + stateTimer * 18, Fade(C(170, 160, 150), 0.5f));
    }
    // wind gathering ring
    if (state == PState::Wind && stateTimer < WIND_WINDUP) {
        DrawRing(pos, 30 + stateTimer * 90, 34 + stateTimer * 90, 0, 360, 32,
                 Fade(C(215, 245, 230), 0.4f));
    }
}

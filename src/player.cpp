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

void Player::Reset(Vector2 spawn) {
    pos = spawn; vel = {0, 0};
    facing = 1;
    maxHp = cfg::P_MAX_HP; hp = maxHp;
    for (int i = 0; i < STYLE_COUNT; i++) cd[i] = 0;
    iframes = 0;
    hitMem.Clear();
    state = PState::Normal;
    onGround = false;
    stateTimer = 0; comboStage = 0;
    comboQueued = false; didHitFrame = false;
    multiAttackId = -1; waterTick = 0; waterPass = 0;
    loveSeg = 0; serpentTick = 0;
    crouchT = 0; chillT = 0; hiddenT = 0; mistAmbushT = 0;
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
    bool styleP[STYLE_COUNT];
    styleP[STYLE_WATER]   = IsKeyPressed(KEY_K) || IsKeyPressed(KEY_ONE);
    styleP[STYLE_FIRE]    = IsKeyPressed(KEY_L) || IsKeyPressed(KEY_TWO);
    styleP[STYLE_STONE]   = IsKeyPressed(KEY_I) || IsKeyPressed(KEY_THREE);
    styleP[STYLE_LOVE]    = IsKeyPressed(KEY_O) || IsKeyPressed(KEY_FOUR);
    styleP[STYLE_SERPENT] = IsKeyPressed(KEY_U) || IsKeyPressed(KEY_FIVE);
    styleP[STYLE_WIND]    = IsKeyPressed(KEY_H) || IsKeyPressed(KEY_SIX);
    styleP[STYLE_MIST]    = IsKeyPressed(KEY_M) || IsKeyPressed(KEY_SEVEN);

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
            if (styleP[STYLE_WATER] && cd[STYLE_WATER] <= 0) {
                state = PState::Water;
                stateTimer = 0;
                waterPass = 0;
                cd[STYLE_WATER] = cfg::WATER_CD * prog->CdMult(STYLE_WATER);
                multiAttackId = cs.NewId();
                waterTick = 0.07f;
                iframes = fmaxf(iframes, WATER_TIME * prog->ReachMult(STYLE_WATER) + 0.05f);
                fx.AddShake(0.08f);
                PlaySfx(SFX_WATER, 0.8f);
            }
            else if (styleP[STYLE_FIRE] && cd[STYLE_FIRE] <= 0) {
                state = PState::FireWindup;
                stateTimer = 0;
                vel.x = 0;
                PlaySfx(SFX_FIRE, 0.6f, 1.15f);
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
    bool noGravity = (state == PState::Water || state == PState::Plunge ||
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
    if (state == PState::Water) bladeCol = C(120, 200, 255);
    if (state == PState::FireWindup || state == PState::FireRecover) bladeCol = C(255, 170, 90);
    if (state == PState::Stone) bladeCol = C(190, 182, 170);
    if (state == PState::Love)  bladeCol = C(255, 150, 205);
    if (state == PState::Serpent) bladeCol = C(140, 225, 110);
    if (state == PState::Wind)  bladeCol = C(215, 248, 232);
    DrawRectanglePro(blade, { 0, 2.25f }, ang, Fade(bladeCol, alpha));
    Rectangle hilt = { hand.x, hand.y, 8, 7 };
    DrawRectanglePro(hilt, { 4, 3.5f }, ang, Fade(C(120, 40, 40), alpha));

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

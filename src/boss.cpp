#include "boss.h"
#include "player.h"
#include "companion.h"
#include "shinobu.h"
#include "rengoku.h"
#include "gyomei.h"
#include "tengen.h"
#include "effects.h"
#include "config.h"
#include "audio.h"
#include <cmath>
#include <algorithm>

static const float RING_SPEED = 560.0f;
static const float MUZAN_REGEN_BASE = 9.0f;
static const float MUZAN_PRESSURE_WINDOW = 2.2f;

// Muzan escalates by survival time and pressure damage. HP is a pressure meter;
// sunrise, not blade damage, ends the fight.

static bool RingIntersectsRect(Vector2 center, float radius, Rectangle r) {
    float cx = Clampf(center.x, r.x, r.x + r.width);
    float cy = Clampf(center.y, r.y, r.y + r.height);
    float minD = Dist(center, { cx, cy });
    Vector2 corners[4] = {
        { r.x, r.y },
        { r.x + r.width, r.y },
        { r.x, r.y + r.height },
        { r.x + r.width, r.y + r.height }
    };
    float maxD = 0;
    for (const auto& p : corners) maxD = fmaxf(maxD, Dist(center, p));
    return radius + 36.0f >= minD && radius - 36.0f <= maxD;
}

void Boss::Reset() {
    active = false;
    state = BState::Inactive;
    pos = {0, 0}; vel = {0, 0};
    facing = -1;
    hp = maxHp = cfg::BOSS_HP;
    phase = 1;
    vulnerable = false;
    guardBroken = 0;
    fightT = 0;
    hitMem.Clear();
    stateTimer = 0; decideTimer = 0;
    tickT = 0;
    comboLeft = 0;
    dashesLeft = 0; dashAttackId = -1;
    slowTimer = 0; openingCd = 0; pressureLock = 0; ultimateCd = 42.0f;
    poisonT = 0; poisonTick = 0;
    hitFlash = 0; auraTimer = 0;
    preyAlly = false;
    preyShinobu = false;
    preyRengoku = false;
    preyGyomei = false;
    preyTengen = false;
    despBlasted = false;
    sunriseDeath = false;
    crescents.clear();
    ringsAtk.clear();
}

void Boss::Activate(Vector2 p) {
    Reset();
    active = true;
    pos = p;
    state = BState::Intro;
    stateTimer = 1.15f;
    decideTimer = 0.55f;
    ultimateCd = 34.0f;
    PlaySfx(SFX_ROAR, 1.0f, 0.9f);
}

Rectangle Boss::Rect() const {
    return { pos.x - w * 0.5f, pos.y - h * 0.5f, w, h };
}

void Boss::BeginSunriseDeath(Effects& fx) {
    if (!active || state == BState::Dead || state == BState::Dying) return;
    state = BState::Dying;
    stateTimer = 6.2f;
    sunriseDeath = true;
    vulnerable = false;
    guardBroken = 0;
    comboLeft = 0;
    vel = { pos.x < cfg::SCREEN_W * 0.5f ? -220.0f : 220.0f, -120.0f };
    facing = vel.x > 0 ? 1 : -1;
    crescents.clear();
    ringsAtk.clear();
    fx.AddShake(1.0f);
    fx.AddHitstop(0.35f);
    fx.Ring(pos, 30, 520, 680, 16, C(255, 210, 130));
    fx.FireExplosion({ pos.x, pos.y - 12 });
    fx.Text({ pos.x, pos.y - h - 26 }, C(255, 225, 160), 1.55f,
            "THE SUN RISES");
    fx.Text({ pos.x, pos.y - h }, C(255, 120, 90), 1.05f,
            "MUZAN BURNS");
    PlaySfx(SFX_EXPLO, 1.0f, 0.55f);
    PlaySfx(SFX_ROAR, 1.0f, 0.45f);
}

void Boss::EnterRecover(float t) {
    state = BState::Recover;
    stateTimer = t;
}

void Boss::ChooseAttack(const Player& player, const Giyu* ally, const Shinobu* shinobu,
                        const Rengoku* rengoku, const Gyomei* gyomei, const Tengen* tengen) {
    // the Demon King actively hunts Hashira as well as the player
    preyAlly = false;
    preyShinobu = false;
    preyRengoku = false;
    preyGyomei = false;
    preyTengen = false;
    bool gActive = ally && ally->Active();
    bool sActive = shinobu && shinobu->Active();
    bool rActive = rengoku && rengoku->Active();
    bool yActive = gyomei && gyomei->Active();
    bool tActive = tengen && tengen->Active();
    int allyTargetChance = phase >= 4 ? 68 : (phase >= 3 ? 58 : 46);
    if ((gActive || sActive || rActive || yActive || tActive) &&
        GetRandomValue(0, 99) < allyTargetChance) {
        float best = 1e9f;
        if (gActive) {
            best = fabsf(ally->pos.x - pos.x);
            preyAlly = true;
        }
        if (sActive && fabsf(shinobu->pos.x - pos.x) < best) {
            best = fabsf(shinobu->pos.x - pos.x);
            preyAlly = false; preyShinobu = true; preyRengoku = false;
        }
        if (rActive && fabsf(rengoku->pos.x - pos.x) < best) {
            best = fabsf(rengoku->pos.x - pos.x);
            preyAlly = false; preyShinobu = false; preyRengoku = true; preyGyomei = false;
        }
        if (yActive && fabsf(gyomei->pos.x - pos.x) < best) {
            best = fabsf(gyomei->pos.x - pos.x);
            preyAlly = false; preyShinobu = false; preyRengoku = false; preyGyomei = true; preyTengen = false;
        }
        if (tActive && fabsf(tengen->pos.x - pos.x) < best) {
            preyAlly = false; preyShinobu = false; preyRengoku = false; preyGyomei = false; preyTengen = true;
        }
    }
    Vector2 prey = preyAlly ? ally->pos
                 : (preyShinobu ? shinobu->pos
                 : (preyRengoku ? rengoku->pos
                 : (preyGyomei ? gyomei->pos
                 : (preyTengen ? tengen->pos : player.pos))));

    float dist = fabsf(prey.x - pos.x);
    int roll = GetRandomValue(0, 99);
    bool far = dist > 360;
    bool chained = comboLeft > 0;

    if (phase == 1) {
        if (far)  { state = roll < 45 ? BState::TeleDash : (roll < 74 ? BState::TeleCrescent : BState::WhipStorm); }
        else      { state = roll < 42 ? BState::TeleClaws : (roll < 74 ? BState::TeleDash : BState::TeleVanish); }
    }
    else if (phase == 2) {
        if (far) {
            if      (roll < 26) state = BState::TeleDash;
            else if (roll < 50) state = BState::TeleCrescent;
            else if (roll < 72) state = BState::WhipStorm;
            else if (roll < 90) state = BState::TeleVanish;
            else                state = BState::Arena;
        } else {
            if      (roll < 30) state = BState::TeleClaws;
            else if (roll < 58) state = BState::TeleBlades;
            else if (roll < 82) state = BState::WhipStorm;
            else                state = BState::TeleVanish;
        }
    }
    else { // phases 3 and 4: relentless
        if (far) {
            if      (roll < 22) state = BState::TeleDash;
            else if (roll < 42) state = BState::TeleVanish;
            else if (roll < 62) state = BState::TeleCrescent;
            else if (roll < 82) state = BState::WhipStorm;
            else                state = BState::Arena;
        } else {
            if      (roll < 28) state = BState::TeleBlades;
            else if (roll < 52) state = BState::WhipStorm;
            else if (roll < 74) state = BState::TeleVanish;
            else                state = BState::Arena;
        }
    }
    if (chained && state == BState::Summon) state = BState::WhipStorm;

    // set telegraph timers
    switch (state) {
        case BState::TeleDash:
            stateTimer = phase >= 3 ? 0.24f : 0.36f;
            dashesLeft = phase >= 4 ? 4 : (phase == 3 ? 3 : 2);
            break;
        case BState::TeleClaws:    stateTimer = phase >= 3 ? 0.30f : 0.46f;  break;
        case BState::TeleCrescent: stateTimer = phase >= 3 ? 0.22f : 0.34f; break;
        case BState::TeleVanish:   stateTimer = vanishDur * (phase >= 3 ? 0.72f : 0.88f); break;
        case BState::TeleBlades:   stateTimer = phase >= 3 ? 0.30f : 0.42f; break;
        case BState::Summon:       stateTimer = 0.65f;                       break;
        case BState::WhipStorm:
            stateTimer = phase >= 4 ? 1.25f : (phase >= 3 ? 1.08f : 0.86f);
            tickT = 0.03f;
            break;
        case BState::Arena:
            stateTimer = phase >= 4 ? 1.55f : 1.25f;
            tickT = 0.08f;
            break;
        default: break;
    }
    // the true form gives no time to breathe
    if (phase >= 4 && state != BState::WhipStorm && state != BState::Arena)
        stateTimer *= 0.68f;
}

void Boss::Update(float dt, Player& player, Giyu* ally, Shinobu* shinobu,
                  Rengoku* rengoku, Gyomei* gyomei, Tengen* tengen,
                  CombatSystem& cs, Effects& fx, int& summonRequest) {
    summonRequest = 0;
    if (!active || state == BState::Dead) return;

    if (state != BState::Dying) fightT += dt;
    hitFlash  = fmaxf(hitFlash - dt, 0);
    slowTimer = fmaxf(slowTimer - dt, 0);
    guardBroken = fmaxf(guardBroken - dt, 0);
    openingCd = fmaxf(openingCd - dt, 0);
    pressureLock = fmaxf(pressureLock - dt, 0);
    ultimateCd = fmaxf(ultimateCd - dt, 0);

    // serpent venom gnaws at him, but cannot deliver the final blow
    if (poisonT > 0 && state != BState::Dying) {
        poisonT -= dt;
        poisonTick -= dt;
        if (poisonTick <= 0) {
            poisonTick = 0.5f;
            if (hp > 3.0f) {
                hp -= 2.0f;
                fx.Text({ pos.x, pos.y - h * 0.5f - 6 }, C(140, 220, 90), 0.75f, "2");
            }
        }
    }

    if (state != BState::Dying && state != BState::PhaseShift &&
        state != BState::Desperation && hp < maxHp) {
        float regen = MUZAN_REGEN_BASE + phase * 5.0f;
        if (pressureLock > 0) regen *= 0.18f;
        if (guardBroken > 0 || slowTimer > 0) regen *= 0.45f;
        hp = fminf(maxHp, hp + regen * dt);
    }

    // survival phase: time guarantees escalation; damage can force it earlier.
    int timePhase = fightT > 225.0f ? 4 : (fightT > 150.0f ? 3 : (fightT > 75.0f ? 2 : 1));
    int damagePhase = hp > maxHp * 0.68f ? 1
                    : hp > maxHp * 0.45f ? 2
                    : hp > maxHp * 0.25f ? 3 : 4;
    int newPhase = std::max(timePhase, damagePhase);
    if (newPhase != phase && state != BState::Dying) {
        phase = newPhase;
        if (phase >= 4) {
            // desperation: his flesh gathers the night... and then ERUPTS
            state = BState::Desperation;        // untouchable while transforming
            stateTimer = 3.8f;                  // 1.3s of charge, then a long blast
            despBlasted = false;
            tickT = 0.05f;
            vel.x = 0;
            crescents.clear();
            ringsAtk.clear();
            fx.AddShake(0.6f);
            fx.AddHitstop(0.2f);
            fx.Text({ pos.x, pos.y - h - 24 }, C(255, 60, 70), 1.5f,
                    "THE DEMON KING REFUSES TO DIE");
            fx.Text({ pos.x, pos.y - h + 2 }, C(230, 190, 190), 1.0f, "RUN.");
            PlaySfx(SFX_ROAR, 1.0f, 0.55f);
        } else {
            state = BState::PhaseShift;
            stateTimer = 1.25f;
            comboLeft = 0;
            tickT = 0.05f;
            fx.AddShake(0.5f);
            fx.Ring(pos, 20, 300, 700, 10, C(220, 30, 50));
            fx.Text({ pos.x, pos.y - h }, C(255, 60, 60), 1.4f,
                    phase == 2 ? "MUZAN GROWS ANGRY" : "MUZAN QUICKENS");
            PlaySfx(SFX_ROAR, 1.0f, 0.9f);
        }
    }

    if (state != BState::Dying && state != BState::Desperation &&
        state != BState::PhaseShift && phase >= 3 && ultimateCd <= 0) {
        state = BState::Desperation;
        stateTimer = phase >= 4 ? 4.2f : 3.2f;
        despBlasted = false;
        comboLeft = 0;
        vel.x = 0;
        ultimateCd = phase >= 4 ? 34.0f : 46.0f;
        tickT = 0.05f;
        fx.AddShake(0.9f);
        fx.Text({ pos.x, pos.y - h - 24 }, C(255, 50, 65), 1.35f,
                "BLOOD WHIPS ERUPT");
        PlaySfx(SFX_ROAR, 1.0f, 0.55f);
    }

    float spdMult = (1.0f + 0.35f * (phase - 1) + fightT / 420.0f) *
                    (slowTimer > 0 ? 0.62f : 1.0f);
    float spd = 390.0f * spdMult;   // the Demon King outpaces even Upper Moons

    // red aura in the late fight; a storm of it in his true form
    auraTimer -= dt;
    if (phase >= 3 && auraTimer <= 0 && state != BState::Dying) {
        auraTimer = phase >= 4 ? 0.025f : 0.05f;
        fx.Ember({ pos.x + frnd(-w, w), pos.y + frnd(-h * 0.5f, h * 0.5f) });
    }

    vulnerable = (state == BState::Recover || state == BState::Summon);

    // whom is he hunting right now?
    bool huntingAlly = preyAlly && ally && ally->Active();
    bool huntingShinobu = preyShinobu && shinobu && shinobu->Active();
    bool huntingRengoku = preyRengoku && rengoku && rengoku->Active();
    bool huntingGyomei = preyGyomei && gyomei && gyomei->Active();
    bool huntingTengen = preyTengen && tengen && tengen->Active();
    Vector2 prey = huntingAlly ? ally->pos
                 : (huntingShinobu ? shinobu->pos
                 : (huntingRengoku ? rengoku->pos
                 : (huntingGyomei ? gyomei->pos
                 : (huntingTengen ? tengen->pos : player.pos))));

    auto chainOrRecover = [&]() {
        if (comboLeft > 0) {
            comboLeft--;
            ChooseAttack(player, ally, shinobu, rengoku, gyomei, tengen);
        } else {
            float opening = phase >= 4 ? 0.22f : (phase == 3 ? 0.34f : 0.48f);
            EnterRecover(opening);
        }
    };

    switch (state) {
        case BState::Intro: {
            stateTimer -= dt;
            fx.Ember({ pos.x + frnd(-40, 40), pos.y + frnd(-50, 30) });
            if (stateTimer <= 0) { state = BState::Stalk; decideTimer = 0.35f; }
            break;
        }
        case BState::Stalk: {
            float dx = prey.x - pos.x;
            facing = dx > 0 ? 1 : -1;
            if (player.hiddenT > 0 && !huntingAlly) {
                // even the Demon King hesitates in the mist
                vel.x *= 1.0f - Clampf(4.0f * dt, 0, 1);
            } else {
                if (fabsf(dx) > 150) vel.x = facing * spd;
                else vel.x *= 1.0f - Clampf(6.0f * dt, 0, 1);
                decideTimer -= dt;
                if (fabsf(dx) < 110 && phase >= 2) vel.x = -facing * spd * 0.45f;
                if (decideTimer <= 0) {
                    comboLeft = 1 + phase + (fightT > 210.0f ? 1 : 0);
                    ChooseAttack(player, ally, shinobu, rengoku, gyomei, tengen);
                }
            }
            break;
        }
        case BState::PhaseShift: {
            stateTimer -= dt;
            tickT -= dt;
            vel.x = 0;
            if (tickT <= 0) {
                tickT = 0.12f;
                fx.Ember({ pos.x + frnd(-w * 1.5f, w * 1.5f),
                           pos.y + frnd(-h * 0.7f, h * 0.4f) });
                fx.BloodSpray({ pos.x + frnd(-16, 16), pos.y - 8 },
                              GetRandomValue(0, 1) ? 1 : -1, 0.65f);
                fx.Ring(pos, 28, 240 + phase * 45.0f, 620, 8, C(220, 25, 45));
            }
            if (stateTimer <= 0) {
                comboLeft = 2 + phase;
                ChooseAttack(player, ally, shinobu, rengoku, gyomei, tengen);
            }
            break;
        }
        case BState::TeleDash: {
            vel.x = 0;
            stateTimer -= dt;
            facing = prey.x > pos.x ? 1 : -1;
            if (stateTimer <= 0) {
                state = BState::Dash;
                stateTimer = phase >= 3 ? 0.24f : 0.29f;
                dashAttackId = cs.NewId();
                fx.AddShake(0.15f);
                PlaySfx(SFX_WHOOSH, 0.8f, 0.85f);
            }
            break;
        }
        case BState::Dash: {
            stateTimer -= dt;
            float dashSpd = (phase >= 4 ? 1680.0f : (phase >= 3 ? 1480.0f : 1300.0f)) *
                            (slowTimer > 0 ? 0.68f : 1.0f);
            vel.x = facing * dashSpd;
            // afterimage streaks
            fx.Sparks({ pos.x - facing * 20.0f, pos.y }, facing > 0 ? 180.0f : 0.0f,
                      25, 3, C(200, 30, 45), 300, 3);
            Rectangle r = { pos.x - w * 0.7f, pos.y - h * 0.45f, w * 1.4f, h * 0.9f };
            cs.Add(r, 42, facing * 1050.0f, -540, 0.035f,
                   Team::Enemy, HitKind::BossDash, dashAttackId);
            if (stateTimer <= 0) {
                dashesLeft--;
                if (dashesLeft > 0) {
                    state = BState::TeleDash;
                    stateTimer = phase >= 3 ? 0.10f : 0.16f;
                } else {
                    chainOrRecover();
                }
            }
            break;
        }
        case BState::TeleVanish: {
            vel.x = 0;
            stateTimer -= dt;
            fx.Ember({ pos.x + frnd(-30, 30), pos.y + frnd(-40, 40) });
            if (stateTimer <= 0) {
                // vanish burst at the old spot...
                fx.DeathBurst(pos, C(120, 20, 40), 0.7f);
                // ...reappear beside his prey
                float side = (GetRandomValue(0, 1) == 0) ? -1.0f : 1.0f;
                pos.x = Clampf(prey.x + side * (phase >= 3 ? 92.0f : 115.0f),
                                60.0f, (float)cfg::SCREEN_W - 60.0f);
                pos.y = cfg::GROUND_Y - h * 0.5f;
                facing = prey.x > pos.x ? 1 : -1;
                fx.DeathBurst(pos, C(200, 30, 55), 0.8f);
                fx.AddShake(0.2f);
                PlaySfx(SFX_WHOOSH, 0.9f, 0.6f);
                state = BState::TeleStrike;
                stateTimer = phase >= 3 ? 0.08f : 0.14f;
            }
            break;
        }
        case BState::TeleStrike: {
            vel.x = 0;
            stateTimer -= dt;
            if (stateTimer <= 0) {
                Rectangle r = {
                    facing > 0 ? pos.x - 8 : pos.x - 190,
                    pos.y - 76, 198, 148
                };
                cs.Add(r, 48, facing * 1180.0f, -560, 0.07f,
                       Team::Enemy, HitKind::BossAoe, cs.NewId());
                float a0 = facing > 0 ? -70.0f : 250.0f;
                float a1 = facing > 0 ? 70.0f : 110.0f;
                fx.SlashArc(pos, 110, a0, a1, C(235, 45, 65));
                PlaySfx(SFX_SLASH, 0.9f, 0.7f);
                chainOrRecover();
            }
            break;
        }
        case BState::TeleBlades: {
            vel.x = 0;
            stateTimer -= dt;
            // blades visibly grow out of his body during the telegraph (drawn in Draw)
            if (stateTimer <= 0) {
                int n = phase >= 4 ? 24 : (phase == 3 ? 18 : 13);
                for (int i = 0; i < n; i++) {
                    float a = (360.0f / n) * i * DEG2RAD + frnd(-0.06f, 0.06f);
                    Crescent c;
                    c.pos = { pos.x, pos.y - 10 };
                    c.vel = { cosf(a) * (560.0f + 35.0f * phase),
                              sinf(a) * (560.0f + 35.0f * phase) };
                    crescents.push_back(c);
                }
                if (phase >= 3) {
                    for (int i = 0; i < 5; i++) {
                        Crescent c;
                        float x = (i + 0.5f) * cfg::SCREEN_W / 5.0f;
                        c.pos = { x, -25.0f };
                        c.vel = { frnd(-110, 110), 640.0f + 60.0f * phase };
                        crescents.push_back(c);
                    }
                }
                fx.Ring({ pos.x, pos.y - 10 }, 14, 220, 640, 9, C(225, 35, 55));
                fx.Sparks({ pos.x, pos.y - 10 }, -90, 360, 20, C(235, 60, 70), 420, 3);
                fx.AddShake(0.3f);
                PlaySfx(SFX_EXPLO, 0.6f, 1.3f);
                chainOrRecover();
            }
            break;
        }
        case BState::TeleClaws: {
            vel.x = 0;
            stateTimer -= dt;
            if (stateTimer <= 0) {
                state = BState::Claws;
                stateTimer = phase >= 3 ? 0.95f : 0.78f;
                ringsAtk.clear();
                int rings = phase >= 4 ? 7 : (phase >= 3 ? 5 : 4);
                for (int i = 0; i < rings; i++) {
                    Vector2 center = { pos.x + (i % 2 == 0 ? 0.0f : facing * 180.0f),
                                       pos.y - 4 };
                    ringsAtk.push_back({ center, 26.0f - i * 58.0f, false });
                }
                fx.AddShake(0.35f);
                fx.Ring(pos, 20, 300, RING_SPEED, 9, C(230, 40, 60));
                fx.Sparks({ pos.x, pos.y + h * 0.4f }, -90, 160, 18, C(230, 50, 60), 420, 3);
                PlaySfx(SFX_STONE, 0.6f, 1.4f);
            }
            break;
        }
        case BState::Claws: {
            stateTimer -= dt;
            for (auto& rg : ringsAtk) {
                rg.r += RING_SPEED * dt;
                if (!rg.hitDone && rg.r > 20) {
                    float d = Dist(player.pos, rg.center);
                    if (fabsf(d - rg.r) < 38 && player.pos.y > cfg::GROUND_Y - 160) {
                        rg.hitDone = true;
                        float dir = player.pos.x >= rg.center.x ? 1.0f : -1.0f;
                        player.TakeDamage(68, dir * 1040.0f, fx, true);
                    }
                    // the shockwave respects no ally either
                    if (!rg.hitDone && ally && ally->Active()) {
                        float dA = Dist(ally->pos, rg.center);
                        if (fabsf(dA - rg.r) < 38 && ally->pos.y > cfg::GROUND_Y - 160) {
                            rg.hitDone = true;
                            float dir = ally->pos.x >= rg.center.x ? 1.0f : -1.0f;
                            ally->TakeDamage(58, dir * 900.0f, HitKind::BossAoe, fx);
                        }
                    }
                    if (!rg.hitDone && shinobu && shinobu->Active()) {
                        float dS = Dist(shinobu->pos, rg.center);
                        if (fabsf(dS - rg.r) < 38 && shinobu->pos.y > cfg::GROUND_Y - 160) {
                            rg.hitDone = true;
                            float dir = shinobu->pos.x >= rg.center.x ? 1.0f : -1.0f;
                            shinobu->TakeDamage(58, dir * 900.0f, HitKind::BossAoe, fx);
                        }
                    }
                    if (!rg.hitDone && rengoku && rengoku->Active()) {
                        float dR = Dist(rengoku->pos, rg.center);
                        if (fabsf(dR - rg.r) < 38 && rengoku->pos.y > cfg::GROUND_Y - 160) {
                            rg.hitDone = true;
                            float dir = rengoku->pos.x >= rg.center.x ? 1.0f : -1.0f;
                            rengoku->TakeDamage(58, dir * 900.0f, HitKind::BossAoe, fx);
                        }
                    }
                    if (!rg.hitDone && gyomei && gyomei->Active()) {
                        float dY = Dist(gyomei->pos, rg.center);
                        if (fabsf(dY - rg.r) < 38 && gyomei->pos.y > cfg::GROUND_Y - 160) {
                            rg.hitDone = true;
                            float dir = gyomei->pos.x >= rg.center.x ? 1.0f : -1.0f;
                            gyomei->TakeDamage(58, dir * 900.0f, HitKind::BossAoe, fx);
                        }
                    }
                    if (!rg.hitDone && tengen && tengen->Active()) {
                        float dT = Dist(tengen->pos, rg.center);
                        if (fabsf(dT - rg.r) < 38 && tengen->pos.y > cfg::GROUND_Y - 160) {
                            rg.hitDone = true;
                            float dir = tengen->pos.x >= rg.center.x ? 1.0f : -1.0f;
                            tengen->TakeDamage(58, dir * 900.0f, HitKind::BossAoe, fx);
                        }
                    }
                }
            }
            if (stateTimer <= 0) {
                ringsAtk.clear();
                chainOrRecover();
            }
            break;
        }
        case BState::Summon: {
            vel.x = 0;
            stateTimer -= dt;
            fx.Ember({ pos.x + frnd(-50, 50), pos.y - h * 0.5f });
            if (stateTimer <= 0) {
                summonRequest = phase >= 4 ? 6 : (2 + phase);      // game spawns the demons
                fx.AddShake(0.25f);
                fx.Text({ pos.x, pos.y - h }, C(230, 60, 200), 1.1f, "RISE, MY DEMONS");
                PlaySfx(SFX_ROAR, 0.6f, 1.2f);
                chainOrRecover();
            }
            break;
        }
        case BState::TeleCrescent: {
            vel.x = 0;
            stateTimer -= dt;
            facing = prey.x > pos.x ? 1 : -1;
            if (stateTimer <= 0) {
                int n = phase >= 4 ? 11 : (phase == 3 ? 8 : 5);
                Vector2 origin = { pos.x + facing * 30.0f, pos.y - 16 };
                float baseAng = atan2f(prey.y - origin.y, prey.x - origin.x);
                for (int i = 0; i < n; i++) {
                    float a = baseAng + (i - (n - 1) * 0.5f) * (phase >= 3 ? 0.12f : 0.16f);
                    Crescent c;
                    c.pos = origin;
                    c.vel = { cosf(a) * (650.0f + phase * 35.0f),
                              sinf(a) * (650.0f + phase * 35.0f) };
                    crescents.push_back(c);
                }
                if (phase >= 3) {
                    for (int i = 0; i < 2; i++) {
                        Crescent c;
                        float side = i == 0 ? -30.0f : cfg::SCREEN_W + 30.0f;
                        c.pos = { side, prey.y - 80.0f + i * 120.0f };
                        float a = atan2f(prey.y - c.pos.y, prey.x - c.pos.x);
                        c.vel = { cosf(a) * 720.0f, sinf(a) * 720.0f };
                        crescents.push_back(c);
                    }
                }
                fx.Sparks(origin, facing > 0 ? 0.0f : 180.0f, 60, 10, C(255, 40, 60), 380, 3);
                PlaySfx(SFX_SLASH, 0.7f, 0.6f);
                chainOrRecover();
            }
            break;
        }
        case BState::WhipStorm: {
            stateTimer -= dt;
            tickT -= dt;
            float dx = prey.x - pos.x;
            facing = dx > 0 ? 1 : -1;
            vel.x = facing * spd * 0.35f;
            if (tickT <= 0) {
                tickT = phase >= 4 ? 0.105f : (phase >= 3 ? 0.13f : 0.16f);
                int id = cs.NewId();
                int side = GetRandomValue(0, 1) == 0 ? -1 : 1;
                float y = Clampf(prey.y + frnd(-95, 85), 150.0f, cfg::GROUND_Y - 34.0f);
                Rectangle sweep = side < 0
                    ? Rectangle{ -20.0f, y - 24.0f, prey.x + 190.0f, 48.0f }
                    : Rectangle{ prey.x - 190.0f, y - 24.0f,
                                 cfg::SCREEN_W - prey.x + 210.0f, 48.0f };
                cs.Add(sweep, 40 + 5 * phase, -side * 1120.0f, -470.0f, 0.055f,
                       Team::Enemy, HitKind::BossAoe, id);
                fx.SlashArc({ side < 0 ? 0.0f : (float)cfg::SCREEN_W, y },
                            230 + phase * 35.0f, side < 0 ? -30.0f : 210.0f,
                            side < 0 ? 30.0f : 150.0f, C(220, 25, 45));
                fx.Sparks({ side < 0 ? -10.0f : (float)cfg::SCREEN_W + 10.0f, y },
                          side < 0 ? 0.0f : 180.0f, 18, 10, C(255, 55, 70), 760, 3);

                if (phase >= 2) {
                    float x = Clampf(prey.x + frnd(-190, 190), 65.0f, cfg::SCREEN_W - 65.0f);
                    Rectangle drop = { x - 28.0f, 0.0f, 56.0f, cfg::GROUND_Y - 22.0f };
                    cs.Add(drop, 34 + 5 * phase, (x < prey.x ? 1.0f : -1.0f) * 800.0f,
                           -560.0f, 0.045f, Team::Enemy, HitKind::BossAoe, cs.NewId());
                    fx.Sparks({ x, 0 }, 90, 18, 9, C(255, 45, 65), 780, 3);
                    fx.Ring({ x, cfg::GROUND_Y - 35.0f }, 8, 80, 520, 5, C(230, 35, 55));
                }
                if (phase >= 4 && GetRandomValue(0, 1) == 0) {
                    Rectangle floor = { 0.0f, cfg::GROUND_Y - 74.0f,
                                        (float)cfg::SCREEN_W, 64.0f };
                    cs.Add(floor, 32, prey.x < cfg::SCREEN_W * 0.5f ? 880.0f : -880.0f,
                           -390.0f, 0.045f, Team::Enemy, HitKind::BossAoe, cs.NewId());
                    fx.Ring({ cfg::SCREEN_W * 0.5f, cfg::GROUND_Y - 38.0f },
                            20, 620, 920, 7, C(185, 18, 35));
                }
                fx.AddShake(phase >= 4 ? 0.22f : 0.12f);
                PlaySfx(SFX_SLASH, 0.45f, 0.55f);
            }
            if (stateTimer <= 0) chainOrRecover();
            break;
        }
        case BState::Arena: {
            stateTimer -= dt;
            tickT -= dt;
            vel.x *= 1.0f - Clampf(7.0f * dt, 0, 1);
            if (tickT <= 0) {
                tickT = phase >= 4 ? 0.20f : 0.26f;
                int lane = GetRandomValue(0, 3);
                Rectangle zone{};
                float kb = 0;
                if (lane == 0) {
                    zone = { 0.0f, cfg::GROUND_Y - 210.0f,
                             cfg::SCREEN_W * 0.48f, 190.0f };
                    kb = 980.0f;
                } else if (lane == 1) {
                    zone = { cfg::SCREEN_W * 0.52f, cfg::GROUND_Y - 210.0f,
                             cfg::SCREEN_W * 0.48f, 190.0f };
                    kb = -980.0f;
                } else if (lane == 2) {
                    zone = { cfg::SCREEN_W * 0.24f, 70.0f,
                             cfg::SCREEN_W * 0.52f, cfg::GROUND_Y - 100.0f };
                    kb = prey.x < cfg::SCREEN_W * 0.5f ? -860.0f : 860.0f;
                } else {
                    zone = { 0.0f, cfg::GROUND_Y - 92.0f,
                             (float)cfg::SCREEN_W, 82.0f };
                    kb = prey.x < cfg::SCREEN_W * 0.5f ? 900.0f : -900.0f;
                }
                cs.Add(zone, 46 + 5 * phase, kb, -520.0f, 0.06f,
                       Team::Enemy, HitKind::BossAoe, cs.NewId());
                Vector2 center = { zone.x + zone.width * 0.5f, zone.y + zone.height * 0.5f };
                fx.Ring(center, 16, fmaxf(zone.width, zone.height) * 0.55f,
                        960, 9, C(230, 28, 48));
                fx.FireExplosion({ center.x, fminf(center.y, cfg::GROUND_Y - 48.0f) });
                fx.AddShake(phase >= 4 ? 0.38f : 0.25f);
                PlaySfx(SFX_EXPLO, 0.45f, 0.65f);
            }
            if (stateTimer <= 0) chainOrRecover();
            break;
        }
        case BState::Desperation: {
            stateTimer -= dt;
            tickT -= dt;
            vel.x = 0;
            fx.AddShake(phase >= 4 ? 0.035f : 0.02f);
            if (!despBlasted) {
                // the night itself is drawn into his flesh
                for (int i = 0; i < 4; i++) {
                    float a = frnd(0, 360);
                    Vector2 sp = { pos.x + cosf(a * DEG2RAD) * 110.0f,
                                   pos.y - 10 + sinf(a * DEG2RAD) * 110.0f };
                    fx.Sparks(sp, a + 180.0f, 8, 1, C(220, 30, 50), 520, 3);
                }
                fx.Ember({ pos.x + frnd(-w, w), pos.y + frnd(-h * 0.5f, h * 0.5f) });
                if (tickT <= 0) {
                    tickT = 0.18f;
                    Rectangle warning = { frnd(0, cfg::SCREEN_W - 180.0f),
                                          cfg::GROUND_Y - frnd(130, 250),
                                          frnd(160, 360), frnd(42, 85) };
                    cs.Add(warning, 22 + 3 * phase,
                           warning.x < player.pos.x ? 640.0f : -640.0f,
                           -360.0f, 0.035f, Team::Enemy, HitKind::BossAoe, cs.NewId());
                    fx.Sparks({ warning.x + warning.width * 0.5f,
                                warning.y + warning.height * 0.5f },
                              -90, 360, 8, C(255, 45, 65), 620, 3);
                }
                if (stateTimer <= (phase >= 4 ? 3.0f : 2.2f)) {
                    despBlasted = true;
                    // THE ERUPTION - wave after wave of it
                    ringsAtk.clear();
                    for (int i = 0; i < (phase >= 4 ? 10 : 7); i++)
                        ringsAtk.push_back({ { pos.x, pos.y - 8 }, 26.0f - i * 190.0f, false });
                    for (int i = 0; i < 14 + phase * 3; i++) {
                        Crescent c;
                        float side = (i % 4 == 0) ? -40.0f
                                   : (i % 4 == 1) ? cfg::SCREEN_W + 40.0f
                                   : frnd(40.0f, cfg::SCREEN_W - 40.0f);
                        c.pos = { side, (i % 4 < 2) ? frnd(90.0f, cfg::GROUND_Y - 70.0f) : -35.0f };
                        Vector2 tgt = { frnd(120.0f, cfg::SCREEN_W - 120.0f),
                                        frnd(150.0f, cfg::GROUND_Y - 55.0f) };
                        float a = atan2f(tgt.y - c.pos.y, tgt.x - c.pos.x);
                        c.vel = { cosf(a) * (760.0f + phase * 70.0f),
                                  sinf(a) * (760.0f + phase * 70.0f) };
                        crescents.push_back(c);
                    }
                    fx.FireExplosion(pos);
                    fx.DeathBurst({ pos.x, pos.y - 10 }, C(210, 25, 45), 3.0f);
                    fx.BloodSpray({ pos.x, pos.y - 10 }, 1, 2.5f);
                    fx.BloodSpray({ pos.x, pos.y - 10 }, -1, 2.5f);
                    fx.Ring(pos, 20, 640, 900, 16, C(255, 60, 60));
                    fx.Ring(pos, 10, 420, 1200, 8, C(255, 160, 160));
                    fx.AddShake(1.0f);
                    fx.AddHitstop(0.3f);
                    fx.Text({ pos.x, pos.y - h - 24 }, C(255, 40, 55), 1.7f,
                            "MUZAN REVEALS HIS TRUE FORM");
                    PlaySfx(SFX_EXPLO, 1.0f, 0.55f);
                    PlaySfx(SFX_ROAR, 1.0f, 0.5f);
                }
            } else {
                // the blast wave rolls outward - outrun it or be swallowed
                for (auto& rg : ringsAtk) {
                    rg.r += (960.0f + phase * 80.0f) * dt;
                    if (!rg.hitDone && rg.r > 20 && rg.r < 720) {
                        if (fabsf(Dist(player.pos, rg.center) - rg.r) < 42) {
                            rg.hitDone = true;
                            float dir = player.pos.x >= rg.center.x ? 1.0f : -1.0f;
                            player.TakeDamage(86, dir * 1250.0f, fx, true);
                        }
                        if (!rg.hitDone && ally && ally->Active() &&
                            fabsf(Dist(ally->pos, rg.center) - rg.r) < 42) {
                            rg.hitDone = true;
                            float dir = ally->pos.x >= rg.center.x ? 1.0f : -1.0f;
                            ally->TakeDamage(68, dir * 1050.0f, HitKind::BossAoe, fx);
                        }
                        if (!rg.hitDone && shinobu && shinobu->Active() &&
                            fabsf(Dist(shinobu->pos, rg.center) - rg.r) < 42) {
                            rg.hitDone = true;
                            float dir = shinobu->pos.x >= rg.center.x ? 1.0f : -1.0f;
                            shinobu->TakeDamage(68, dir * 1050.0f, HitKind::BossAoe, fx);
                        }
                        if (!rg.hitDone && rengoku && rengoku->Active() &&
                            fabsf(Dist(rengoku->pos, rg.center) - rg.r) < 42) {
                            rg.hitDone = true;
                            float dir = rengoku->pos.x >= rg.center.x ? 1.0f : -1.0f;
                            rengoku->TakeDamage(68, dir * 1050.0f, HitKind::BossAoe, fx);
                        }
                        if (!rg.hitDone && gyomei && gyomei->Active() &&
                            fabsf(Dist(gyomei->pos, rg.center) - rg.r) < 42) {
                            rg.hitDone = true;
                            float dir = gyomei->pos.x >= rg.center.x ? 1.0f : -1.0f;
                            gyomei->TakeDamage(68, dir * 1050.0f, HitKind::BossAoe, fx);
                        }
                        if (!rg.hitDone && tengen && tengen->Active() &&
                            fabsf(Dist(tengen->pos, rg.center) - rg.r) < 42) {
                            rg.hitDone = true;
                            float dir = tengen->pos.x >= rg.center.x ? 1.0f : -1.0f;
                            tengen->TakeDamage(68, dir * 1050.0f, HitKind::BossAoe, fx);
                        }
                    }
                }
                if (tickT <= 0) {
                    tickT = phase >= 4 ? 0.16f : 0.22f;
                    Rectangle lane = { frnd(0, cfg::SCREEN_W - 140.0f),
                                       frnd(75.0f, cfg::GROUND_Y - 145.0f),
                                       frnd(120.0f, 300.0f), frnd(85.0f, 165.0f) };
                    cs.Add(lane, 40 + 5 * phase,
                           lane.x < player.pos.x ? 940.0f : -940.0f,
                           -520.0f, 0.055f, Team::Enemy, HitKind::BossAoe, cs.NewId());
                    fx.FireExplosion({ lane.x + lane.width * 0.5f, lane.y + lane.height * 0.5f });
                    fx.AddShake(0.22f);
                }
            }
            if (stateTimer <= 0) {
                ringsAtk.clear();
                comboLeft = 2 + phase;
                ChooseAttack(player, ally, shinobu, rengoku, gyomei, tengen);
            }
            break;
        }
        case BState::Recover: {
            stateTimer -= dt;
            vel.x *= 1.0f - Clampf(5.0f * dt, 0, 1);
            if (stateTimer <= 0) {
                state = BState::Stalk;
                decideTimer = frnd(0.16f, 0.42f) / (0.85f + 0.22f * phase);
            }
            break;
        }
        case BState::Dying: {
            stateTimer -= dt;
            if (sunriseDeath) {
                vel.x += facing * 24.0f * dt;
                vel.y = fminf(vel.y + cfg::GRAVITY * 0.18f * dt, 220.0f);
                if (pos.x < 80) facing = 1;
                if (pos.x > cfg::SCREEN_W - 80) facing = -1;
                fx.AddShake(0.03f);
                if (fmodf(stateTimer, 0.12f) < 0.05f) {
                    fx.FireExplosion({ pos.x + frnd(-w * 0.8f, w * 0.8f),
                                       pos.y + frnd(-h * 0.6f, h * 0.45f) });
                    fx.DeathBurst({ pos.x + frnd(-w, w), pos.y + frnd(-h * 0.5f, h * 0.5f) },
                                  C(255, 170, 90), 0.85f);
                    fx.Text({ pos.x, pos.y - h - 18 }, C(255, 225, 170), 0.75f, "SUNLIGHT");
                }
            } else {
                vel.x = 0;
                if (fmodf(stateTimer, 0.18f) < 0.05f) {
                    fx.DeathBurst({ pos.x + frnd(-w, w), pos.y + frnd(-h * 0.5f, h * 0.5f) },
                                  C(220, 40, 60), 0.8f);
                    fx.AddShake(0.2f);
                }
            }
            if (stateTimer <= 0) {
                state = BState::Dead;
                fx.FireExplosion(pos);
                fx.DeathBurst(pos, sunriseDeath ? C(255, 210, 120) : C(230, 40, 60), 2.6f);
                fx.AddShake(1.0f);
                fx.AddHitstop(0.35f);
                PlaySfx(SFX_EXPLO, 1.0f, 0.7f);
                PlaySfx(SFX_ROAR, 1.0f, 0.5f);
            }
            break;
        }
        default: break;
    }

    // projectiles fly even during other states
    for (auto& c : crescents) {
        if (!c.alive) continue;
        c.pos.x += c.vel.x * dt;
        c.pos.y += c.vel.y * dt;
        c.spin += 720.0f * dt;
        if (CheckCollisionCircleRec(c.pos, 13, player.Rect())) {
            if (player.TakeDamage(34 + 3 * phase, (c.vel.x > 0 ? 1 : -1) * 720.0f, fx,
                                  phase >= 3))
                c.alive = false;
        }
        if (c.alive && ally && ally->Active() &&
            CheckCollisionCircleRec(c.pos, 13, ally->Rect())) {
            ally->TakeDamage(34 + 3 * phase, (c.vel.x > 0 ? 1 : -1) * 720.0f,
                             HitKind::BossProjectile, fx);
            c.alive = false;
        }
        if (c.alive && shinobu && shinobu->Active() &&
            CheckCollisionCircleRec(c.pos, 13, shinobu->Rect())) {
            shinobu->TakeDamage(34 + 3 * phase, (c.vel.x > 0 ? 1 : -1) * 720.0f,
                                HitKind::BossProjectile, fx);
            c.alive = false;
        }
        if (c.alive && rengoku && rengoku->Active() &&
            CheckCollisionCircleRec(c.pos, 13, rengoku->Rect())) {
            rengoku->TakeDamage(34 + 3 * phase, (c.vel.x > 0 ? 1 : -1) * 720.0f,
                                HitKind::BossProjectile, fx);
            c.alive = false;
        }
        if (c.alive && gyomei && gyomei->Active() &&
            CheckCollisionCircleRec(c.pos, 13, gyomei->Rect())) {
            gyomei->TakeDamage(34 + 3 * phase, (c.vel.x > 0 ? 1 : -1) * 720.0f,
                               HitKind::BossProjectile, fx);
            c.alive = false;
        }
        if (c.alive && tengen && tengen->Active() &&
            CheckCollisionCircleRec(c.pos, 13, tengen->Rect())) {
            tengen->TakeDamage(34 + 3 * phase, (c.vel.x > 0 ? 1 : -1) * 720.0f,
                               HitKind::BossProjectile, fx);
            c.alive = false;
        }
        if (c.pos.x < -60 || c.pos.x > cfg::SCREEN_W + 60 ||
            c.pos.y < -60 || c.pos.y > cfg::SCREEN_H + 60)
            c.alive = false;
        if (c.pos.y > cfg::GROUND_Y - 4) {          // splash on the ground
            c.alive = false;
            fx.Sparks({ c.pos.x, cfg::GROUND_Y - 4 }, -90, 120, 6, C(220, 40, 60), 260, 2.5f);
        }
    }
    crescents.erase(std::remove_if(crescents.begin(), crescents.end(),
                    [](const Crescent& c) { return !c.alive; }), crescents.end());

    // physics
    if (state != BState::Dash) vel.y += cfg::GRAVITY * dt;
    else vel.y = 0;
    pos.x += vel.x * dt;
    pos.y += vel.y * dt;
    pos.x = Clampf(pos.x, 50.0f, (float)cfg::SCREEN_W - 50.0f);
    GroundClamp(pos, vel, h * 0.5f);
}

void Boss::TakeDamage(float dmg, float kbx, HitKind kind, Effects& fx) {
    if (!active || state == BState::Dying || state == BState::Dead ||
        state == BState::Intro || state == BState::TeleVanish ||
        state == BState::Desperation || state == BState::PhaseShift) return;

    if (dmg <= 0) {                          // status-only field (mist cloud)
        if (kind == HitKind::Water) slowTimer = fmaxf(slowTimer, 0.8f);
        if (kind == HitKind::Shinobu) poisonT = fmaxf(poisonT, 2.5f);
        return;
    }

    float mult = (vulnerable || guardBroken > 0) ? 1.0f : 0.35f;
    if (kind == HitKind::Fire && vulnerable) mult = 1.35f;
    if (kind == HitKind::Rengoku && vulnerable) mult *= 1.25f;
    if (kind == HitKind::Water) slowTimer = 1.6f;
    if (kind == HitKind::Serpent) poisonT = 3.0f;
    if (kind == HitKind::Giyu) {
        // a Hashira alone cannot fell the Demon King
        mult *= 0.22f;
        slowTimer = fmaxf(slowTimer, 0.6f);
    }
    if (kind == HitKind::Shinobu) {
        mult *= 0.18f;
        poisonT = fmaxf(poisonT, 4.5f);
    }
    if (kind == HitKind::Rengoku) {
        mult *= 0.30f;
        slowTimer = fmaxf(slowTimer, 0.35f);
    }
    if (kind == HitKind::Gyomei) {
        mult *= 0.38f;                    // strongest Hashira, still not the finisher
        if (guardBroken <= 0) {
            guardBroken = 3.6f;
            fx.Text({ pos.x, pos.y - h * 0.5f - 34 }, C(210, 200, 185), 1.2f,
                    "STONE HASHIRA BREAKS GUARD");
            fx.Ring(pos, 18, 150, 500, 8, C(210, 200, 185));
        }
    }
    if (kind == HitKind::Tengen) {
        mult *= 0.28f;                    // rapid pressure, not solo boss damage
        slowTimer = fmaxf(slowTimer, 0.25f);
    }
    if (kind == HitKind::Stone && guardBroken <= 0) {
        guardBroken = 4.0f;
        fx.Text({ pos.x, pos.y - h * 0.5f - 34 }, C(210, 200, 185), 1.2f, "GUARD BROKEN");
        fx.Ring(pos, 16, 130, 480, 7, C(210, 200, 185));
    }

    float dealt = dmg * mult;
    hp -= dealt;
    pressureLock = fmaxf(pressureLock, MUZAN_PRESSURE_WINDOW + dealt * 0.015f);
    hitFlash = 0.14f;

    Color tcol = (vulnerable || guardBroken > 0) ? C(255, 220, 90) : C(200, 200, 200);
    if (kind == HitKind::Fire)    tcol = C(255, 160, 60);
    if (kind == HitKind::Water)   tcol = C(110, 190, 255);
    if (kind == HitKind::Love)    tcol = C(255, 150, 205);
    if (kind == HitKind::Serpent) tcol = C(140, 220, 90);
    if (kind == HitKind::Wind)    tcol = C(215, 245, 230);
    if (kind == HitKind::Giyu)    tcol = C(120, 190, 255);
    if (kind == HitKind::Shinobu) tcol = C(190, 150, 255);
    if (kind == HitKind::Rengoku) tcol = C(255, 150, 55);
    if (kind == HitKind::Gyomei)  tcol = C(188, 178, 158);
    if (kind == HitKind::Tengen)  tcol = C(255, 212, 88);
    fx.Text({ pos.x, pos.y - h * 0.5f - 16 }, tcol,
            (vulnerable || guardBroken > 0) ? 1.25f : 0.95f, "%.0f", dealt);
    fx.HitSparks({ pos.x, pos.y - 10 }, kbx >= 0 ? 1 : -1, tcol);

    if (hp <= maxHp * 0.08f) {
        hp = maxHp * 0.08f;
        pressureLock = fmaxf(pressureLock, 4.0f);
        slowTimer = fmaxf(slowTimer, 0.6f);
        if (state != BState::Recover && state != BState::PhaseShift) {
            comboLeft = 0;
            EnterRecover(phase >= 4 ? 0.18f : 0.35f);
            fx.Text({ pos.x, pos.y - h - 30 }, C(255, 220, 90), 1.1f,
                    "STALL HIM UNTIL SUNRISE");
            fx.Ring(pos, 18, 190, 620, 8, C(255, 220, 90));
            fx.AddShake(0.35f);
        }
    }
}

bool Boss::ForceOpening(Effects& fx) {
    if (!active || openingCd > 0) return false;
    bool interruptible =
        state == BState::Stalk || state == BState::TeleDash ||
        state == BState::TeleClaws || state == BState::TeleCrescent ||
        state == BState::TeleBlades || state == BState::WhipStorm ||
        state == BState::Arena;
    if (!interruptible) return false;
    openingCd = 7.0f;
    pressureLock = fmaxf(pressureLock, 3.0f);
    dashesLeft = 0;
    comboLeft = 0;
    EnterRecover(phase >= 4 ? 0.35f : 0.65f);
    fx.Text({ pos.x, pos.y - h - 14 }, C(120, 190, 255), 1.25f, "GIYU CREATES AN OPENING");
    fx.Ring(pos, 16, 150, 520, 8, C(120, 190, 255));
    fx.AddShake(0.25f);
    return true;
}

int Boss::NullifyCrescents(Vector2 c, float r) {
    int cut = 0;
    for (auto& cr : crescents) {
        if (cr.alive && Dist(cr.pos, c) < r) {
            cr.alive = false;
            cut++;
        }
    }
    return cut;
}

int Boss::NullifyCrescentsInRect(Rectangle r) {
    int cut = 0;
    for (auto& cr : crescents) {
        if (cr.alive && CheckCollisionPointRec(cr.pos, r)) {
            cr.alive = false;
            cut++;
        }
    }
    return cut;
}

int Boss::NullifyRings(Vector2 c, float r) {
    int cut = 0;
    for (auto& rg : ringsAtk) {
        if (!rg.hitDone && rg.r > 20 && fabsf(Dist(c, rg.center) - rg.r) < r) {
            rg.hitDone = true;
            cut++;
        }
    }
    return cut;
}

int Boss::NullifyRingsInRect(Rectangle r) {
    int cut = 0;
    for (auto& rg : ringsAtk) {
        if (!rg.hitDone && rg.r > 20 && RingIntersectsRect(rg.center, rg.r, r)) {
            rg.hitDone = true;
            cut++;
        }
    }
    return cut;
}

int Boss::CrescentsNear(Vector2 c, float r) const {
    int n = 0;
    for (const auto& cr : crescents)
        if (cr.alive && Dist(cr.pos, c) < r) n++;
    return n;
}

void Boss::Draw() const {
    if (!active || state == BState::Dead) return;
    float gt = (float)GetTime();
    bool finalForm = phase >= 4;

    // fading out mid-teleport
    float bodyA = 1.0f;
    if (state == BState::TeleVanish) bodyA = Clampf(stateTimer / vanishDur, 0.15f, 1.0f);
    if (state == BState::Dying && sunriseDeath)
        bodyA = 0.45f + 0.35f * sinf(gt * 18.0f);

    // shadow
    DrawEllipse((int)pos.x, (int)(cfg::GROUND_Y + 6), 26, 7, Fade(BLACK, 0.4f * bodyA));

    // claw shockwave rings (live attack visual)
    for (const auto& rg : ringsAtk) {
        if (rg.r < 12) continue;
        DrawRing(rg.center, rg.r - 7, rg.r + 7, 0, 360, 48, Fade(C(225, 35, 55), 0.55f));
        DrawRing(rg.center, rg.r + 5, rg.r + 9, 0, 360, 48, Fade(C(255, 130, 130), 0.35f));
    }

    float lean = 0;
    if (state == BState::TeleDash) lean = -facing * 8.0f;    // crouch back
    if (state == BState::Dash)     lean = facing * 10.0f;

    // in his true form the white suit is gone — bare, pale flesh
    Color suit = finalForm ? C(236, 223, 212) : C(235, 232, 238);
    Color suitShade = finalForm ? C(214, 198, 188) : C(200, 196, 210);
    Color hair = C(18, 16, 22);
    Color skin = C(238, 222, 214);
    if (hitFlash > 0) { suit = C(255, 160, 150); suitShade = suit; }

    float bx = pos.x + lean, by = pos.y;
    bool telegraphing = (state == BState::TeleDash || state == BState::TeleClaws ||
                         state == BState::TeleCrescent || state == BState::TeleBlades ||
                         state == BState::TeleStrike || state == BState::WhipStorm ||
                         state == BState::Arena || state == BState::PhaseShift);
    if (telegraphing && fmodf(gt * 12.0f, 1.0f) < 0.5f) suit = C(255, 120, 120);

    // vulnerable golden outline / guard broken gray outline
    if (vulnerable) {
        float pulse = 0.45f + 0.3f * sinf(gt * 10.0f);
        DrawRectangleLinesEx({ bx - w * 0.5f - 4, by - h * 0.5f - 4, w + 8, h + 8 },
                             3, Fade(C(255, 210, 80), pulse));
    } else if (guardBroken > 0) {
        float pulse = 0.35f + 0.25f * sinf(gt * 12.0f);
        DrawRectangleLinesEx({ bx - w * 0.5f - 4, by - h * 0.5f - 4, w + 8, h + 8 },
                             3, Fade(C(210, 200, 185), pulse));
    }

    // blade manifestation telegraph: blood blades grow from his body
    if (state == BState::TeleBlades) {
        float dur = (phase >= 3 ? 0.42f : 0.55f) * (phase >= 4 ? 0.75f : 1.0f);
        float t = 1.0f - Clampf(stateTimer / dur, 0, 1);
        for (int i = 0; i < 8; i++) {
            float a = (360.0f / 8) * i * DEG2RAD + 0.3f;
            Vector2 root = { bx, by - 10 };
            Vector2 tip = { root.x + cosf(a) * 55.0f * t, root.y + sinf(a) * 55.0f * t };
            DrawLineEx(root, tip, 4.0f * t + 1, Fade(C(200, 25, 45), 0.85f));
            DrawLineEx(root, tip, 1.5f, Fade(C(255, 120, 130), 0.85f));
        }
    }
    if (state == BState::PhaseShift) {
        float p = 0.45f + 0.35f * sinf(gt * 16.0f);
        DrawRing(pos, 72, 78, 0, 360, 48, Fade(C(255, 45, 65), p));
        DrawRing(pos, 124, 131, 0, 360, 64, Fade(C(255, 120, 120), p * 0.6f));
        DrawRectangleLinesEx({ bx - w * 0.7f, by - h * 0.7f, w * 1.4f, h * 1.35f },
                             4, Fade(C(255, 70, 80), p));
    }
    if (state == BState::WhipStorm) {
        float p = 0.18f + 0.12f * sinf(gt * 28.0f);
        DrawRectangle(0, (int)cfg::GROUND_Y - 235, cfg::SCREEN_W, 198,
                      Fade(C(180, 10, 30), p * 0.45f));
        for (int i = 0; i < 7; i++) {
            float y = cfg::GROUND_Y - 45.0f - i * 34.0f;
            DrawLineEx({ 0.0f, y + sinf(gt * 12.0f + i) * 12.0f },
                       { (float)cfg::SCREEN_W, y + cosf(gt * 13.0f + i) * 12.0f },
                       3.0f, Fade(C(230, 30, 48), 0.34f));
        }
    }
    if (state == BState::Arena) {
        float p = 0.16f + 0.12f * sinf(gt * 20.0f);
        DrawRectangle(0, (int)cfg::GROUND_Y - 230, cfg::SCREEN_W / 2, 210,
                      Fade(C(210, 20, 40), p));
        DrawRectangle(cfg::SCREEN_W / 2, 90, cfg::SCREEN_W / 2, (int)cfg::GROUND_Y - 120,
                      Fade(C(210, 20, 40), p * 0.75f));
        DrawRing({ cfg::SCREEN_W * 0.5f, cfg::GROUND_Y - 88.0f },
                 170, 178, 0, 360, 64, Fade(C(255, 80, 90), p));
    }
    if (state == BState::Desperation) {
        float p = 0.35f + 0.28f * sinf(gt * 26.0f);
        DrawRing(pos, 190, 202, 0, 360, 72, Fade(C(255, 40, 55), p));
        DrawRing(pos, 330, 342, 0, 360, 96, Fade(C(255, 120, 120), p * 0.45f));
    }

    // legs (black slacks)
    DrawRectangle((int)(bx - 9), (int)(by + 12), 8, 25, Fade(C(24, 22, 30), bodyA));
    DrawRectangle((int)(bx + 1), (int)(by + 12), 8, 25, Fade(C(30, 28, 36), bodyA));
    // torso
    DrawRectangleRounded({ bx - 12, by - 22, 24, 36 }, 0.3f, 4, Fade(suit, bodyA));
    DrawRectangle((int)(bx - 12 + (facing > 0 ? 14 : 2)), (int)(by - 20), 8, 30, Fade(suitShade, bodyA));
    if (!finalForm) {
        // red tie
        DrawRectangle((int)(bx - 1 + facing * 2), (int)(by - 18), 3, 14, Fade(C(190, 30, 40), bodyA));
    } else {
        // dark, pulsating blood veins across chest, shoulders and back
        float pulse = 0.5f + 0.4f * sinf(gt * 7.0f);
        Color vein = Fade(C(150, 12, 35), pulse * bodyA);
        Vector2 heart = { bx, by - 10 };
        DrawCircleV(heart, 3.4f, vein);
        DrawLineEx(heart, { bx - 8, by - 18 }, 2.6f, vein);              // to left shoulder
        DrawLineEx({ bx - 8, by - 18 }, { bx - 16, by - 15 }, 2.0f, vein);
        DrawLineEx(heart, { bx + 8, by - 17 }, 2.6f, vein);              // to right shoulder
        DrawLineEx({ bx + 8, by - 17 }, { bx + 16, by - 13 }, 2.0f, vein);
        DrawLineEx(heart, { bx - 4, by + 2 }, 2.2f, vein);               // down the abdomen
        DrawLineEx({ bx - 4, by + 2 }, { bx + 3, by + 11 }, 1.8f, vein);
        DrawLineEx(heart, { bx + 6, by - 3 }, 2.0f, vein);
        DrawLineEx({ bx + facing * 2.0f, by - 21 },                      // up the neck
                   { bx + facing * 3.0f, by - 27 }, 2.0f, vein);
        DrawCircleV({ bx - 16, by - 15 }, 2.0f, vein);
        DrawCircleV({ bx + 16, by - 13 }, 2.0f, vein);
    }
    // arms
    if (state == BState::TeleClaws || state == BState::Summon || state == BState::TeleBlades) {
        // arms raised
        DrawLineEx({ bx - 10, by - 16 }, { bx - 20, by - 38 }, 5, Fade(suit, bodyA));
        DrawLineEx({ bx + 10, by - 16 }, { bx + 20, by - 38 }, 5, Fade(suit, bodyA));
    } else {
        DrawLineEx({ bx + facing * 8.0f, by - 14 }, { bx + facing * 18.0f, by + 4 }, 5, Fade(suit, bodyA));
    }
    // claws (red-tipped)
    if (state == BState::Claws || state == BState::Dash || state == BState::TeleStrike || telegraphing) {
        for (int i = 0; i < 4; i++) {
            Vector2 a = { bx + facing * 18.0f, by + 2.0f + i * 3.0f };
            Vector2 b = { a.x + facing * 14.0f, a.y + 2 };
            DrawLineEx(a, b, 2, Fade(C(255, 70, 80), bodyA));
        }
    }
    // head
    Vector2 headC = { bx + facing * 2.0f, by - 32 };
    DrawCircleV(headC, 10, Fade(skin, bodyA));
    // wavy black hair
    DrawCircleSector(headC, 11.5f, 160, 380, 14, Fade(hair, bodyA));
    DrawCircleV({ headC.x - facing * 7.0f, headC.y + 3 }, 4, Fade(hair, bodyA));
    // red eyes with vertical pupils — blazing in his true form
    float eyeR = finalForm ? 3.0f : 2.4f;
    DrawCircleV({ headC.x + facing * 4.0f, headC.y + 1 }, eyeR,
                Fade(finalForm ? C(255, 40, 50) : C(220, 30, 40), bodyA));
    DrawRectangle((int)(headC.x + facing * 4.0f), (int)(headC.y - 2), 1, 6, Fade(C(80, 0, 10), bodyA));

    if (!finalForm) {
        // white fedora — torn away with the rest in his final form
        DrawRectangle((int)(headC.x - 12), (int)(headC.y - 9), 24, 3, Fade(suit, bodyA));
        DrawRectangleRounded({ headC.x - 8, headC.y - 20, 16, 12 }, 0.3f, 4, Fade(suit, bodyA));
        DrawRectangle((int)(headC.x - 8), (int)(headC.y - 11), 16, 3, Fade(C(120, 20, 30), bodyA));
    }

    // telegraph markers
    if (state == BState::TeleClaws) {
        float a = 0.25f + 0.2f * sinf(gt * 14.0f);
        for (int i = 1; i <= 3; i++)
            DrawRing(pos, i * 85.0f - 3, i * 85.0f + 3, 0, 360, 40, Fade(C(230, 40, 60), a));
    }
    if (state == BState::TeleCrescent) {
        DrawCircleV({ bx + facing * 26.0f, by - 16 }, 7 + 3 * sinf(gt * 20.0f), Fade(C(255, 40, 60), 0.7f));
    }
    if (state == BState::TeleStrike) {
        // flash where the slash is about to land
        Rectangle warn = { facing > 0 ? pos.x - 8 : pos.x - 190, pos.y - 76, 198, 148 };
        DrawRectangleRec(warn, Fade(C(230, 40, 60), 0.14f + 0.1f * sinf(gt * 30.0f)));
    }

    // crescent projectiles
    for (const auto& c : crescents) {
        if (!c.alive) continue;
        float angDeg = atan2f(c.vel.y, c.vel.x) * RAD2DEG;
        DrawRing(c.pos, 8, 14, angDeg - 100, angDeg + 100, 16, C(220, 35, 55));
        DrawRing(c.pos, 10, 12, angDeg - 80, angDeg + 80, 12, C(255, 140, 150));
    }
}

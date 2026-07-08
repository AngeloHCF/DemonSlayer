#include "boss.h"
#include "player.h"
#include "companion.h"
#include "shinobu.h"
#include "rengoku.h"
#include "effects.h"
#include "config.h"
#include "audio.h"
#include <cmath>
#include <algorithm>

static const float RING_SPEED = 380.0f;

// phase thresholds: 1 > 66% > 2 > 40% > 3 > 27% > 4 (true form)

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
    hitMem.Clear();
    stateTimer = 0; decideTimer = 0;
    dashesLeft = 0; dashAttackId = -1;
    slowTimer = 0; openingCd = 0; poisonT = 0; poisonTick = 0;
    hitFlash = 0; auraTimer = 0;
    preyAlly = false;
    preyShinobu = false;
    preyRengoku = false;
    despBlasted = false;
    crescents.clear();
    ringsAtk.clear();
}

void Boss::Activate(Vector2 p) {
    Reset();
    active = true;
    pos = p;
    state = BState::Intro;
    stateTimer = 1.4f;
    PlaySfx(SFX_ROAR, 1.0f, 0.9f);
}

Rectangle Boss::Rect() const {
    return { pos.x - w * 0.5f, pos.y - h * 0.5f, w, h };
}

void Boss::EnterRecover(float t) {
    state = BState::Recover;
    stateTimer = t;
}

void Boss::ChooseAttack(const Player& player, const Giyu* ally, const Shinobu* shinobu,
                        const Rengoku* rengoku) {
    // sometimes the Demon King turns his attention to a Hashira
    preyAlly = false;
    preyShinobu = false;
    preyRengoku = false;
    bool gActive = ally && ally->Active();
    bool sActive = shinobu && shinobu->Active();
    bool rActive = rengoku && rengoku->Active();
    if ((gActive || sActive || rActive) && GetRandomValue(0, 99) < 35) {
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
            preyAlly = false; preyShinobu = false; preyRengoku = true;
        }
    }
    Vector2 prey = preyAlly ? ally->pos
                 : (preyShinobu ? shinobu->pos
                 : (preyRengoku ? rengoku->pos : player.pos));

    float dist = fabsf(prey.x - pos.x);
    int roll = GetRandomValue(0, 99);
    bool far = dist > 320;

    if (phase == 1) {
        if (far)  { state = roll < 55 ? BState::TeleDash : BState::Summon; }
        else      { state = roll < 60 ? BState::TeleClaws : BState::TeleDash; }
    }
    else if (phase == 2) {
        if (far) {
            if      (roll < 30) state = BState::TeleDash;
            else if (roll < 50) state = BState::TeleCrescent;
            else if (roll < 72) state = BState::TeleVanish;
            else                state = BState::Summon;
        } else {
            if      (roll < 38) state = BState::TeleClaws;
            else if (roll < 68) state = BState::TeleBlades;
            else                state = BState::TeleVanish;
        }
    }
    else { // phases 3 and 4: relentless
        if (far) {
            if      (roll < 25) state = BState::TeleDash;
            else if (roll < 50) state = BState::TeleVanish;
            else if (roll < 68) state = BState::TeleCrescent;
            else if (roll < 85) state = BState::TeleBlades;
            else                state = BState::Summon;
        } else {
            if      (roll < 35) state = BState::TeleBlades;
            else if (roll < 65) state = BState::TeleVanish;
            else                state = BState::TeleClaws;
        }
    }

    // set telegraph timers
    switch (state) {
        case BState::TeleDash:
            stateTimer = phase >= 3 ? 0.35f : 0.5f;
            dashesLeft = phase >= 4 ? 3 : (phase == 3 ? 2 : 1);
            break;
        case BState::TeleClaws:    stateTimer = phase >= 3 ? 0.45f : 0.6f;  break;
        case BState::TeleCrescent: stateTimer = phase >= 3 ? 0.35f : 0.45f; break;
        case BState::TeleVanish:   stateTimer = vanishDur;                  break;
        case BState::TeleBlades:   stateTimer = phase >= 3 ? 0.42f : 0.55f; break;
        case BState::Summon:       stateTimer = 0.9f;                       break;
        default: break;
    }
    // the true form gives no time to breathe
    if (phase >= 4) stateTimer *= 0.75f;
}

void Boss::Update(float dt, Player& player, Giyu* ally, Shinobu* shinobu,
                  Rengoku* rengoku, CombatSystem& cs, Effects& fx, int& summonRequest) {
    summonRequest = 0;
    if (!active || state == BState::Dead) return;

    hitFlash  = fmaxf(hitFlash - dt, 0);
    slowTimer = fmaxf(slowTimer - dt, 0);
    guardBroken = fmaxf(guardBroken - dt, 0);
    openingCd = fmaxf(openingCd - dt, 0);

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

    // phase by remaining health
    int newPhase = hp > maxHp * 0.66f ? 1
                 : hp > maxHp * 0.40f ? 2
                 : hp > maxHp * 0.27f ? 3 : 4;
    if (newPhase != phase && state != BState::Dying) {
        phase = newPhase;
        if (phase >= 4) {
            // desperation: his flesh gathers the night... and then ERUPTS
            state = BState::Desperation;        // untouchable while transforming
            stateTimer = 3.8f;                  // 1.3s of charge, then a long blast
            despBlasted = false;
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
            fx.AddShake(0.5f);
            fx.Ring(pos, 20, 300, 700, 10, C(220, 30, 50));
            fx.Text({ pos.x, pos.y - h }, C(255, 60, 60), 1.4f,
                    phase == 2 ? "MUZAN GROWS ANGRY" : "MUZAN QUICKENS");
            PlaySfx(SFX_ROAR, 1.0f, 0.9f);
        }
    }

    float spdMult = (1.0f + 0.22f * (phase - 1)) * (slowTimer > 0 ? 0.55f : 1.0f);
    float spd = 285.0f * spdMult;   // the Demon King outpaces even Upper Moons

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
    Vector2 prey = huntingAlly ? ally->pos
                 : (huntingShinobu ? shinobu->pos
                 : (huntingRengoku ? rengoku->pos : player.pos));

    switch (state) {
        case BState::Intro: {
            stateTimer -= dt;
            fx.Ember({ pos.x + frnd(-40, 40), pos.y + frnd(-50, 30) });
            if (stateTimer <= 0) { state = BState::Stalk; decideTimer = 1.2f; }
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
                if (decideTimer <= 0) ChooseAttack(player, ally, shinobu, rengoku);
            }
            break;
        }
        case BState::TeleDash: {
            vel.x = 0;
            stateTimer -= dt;
            facing = prey.x > pos.x ? 1 : -1;
            if (stateTimer <= 0) {
                state = BState::Dash;
                stateTimer = 0.32f;
                dashAttackId = cs.NewId();
                fx.AddShake(0.15f);
                PlaySfx(SFX_WHOOSH, 0.8f, 0.85f);
            }
            break;
        }
        case BState::Dash: {
            stateTimer -= dt;
            float dashSpd = (phase >= 4 ? 1250.0f : 1100.0f) * (slowTimer > 0 ? 0.6f : 1.0f);
            vel.x = facing * dashSpd;
            // afterimage streaks
            fx.Sparks({ pos.x - facing * 20.0f, pos.y }, facing > 0 ? 180.0f : 0.0f,
                      25, 3, C(200, 30, 45), 300, 3);
            Rectangle r = { pos.x - w * 0.7f, pos.y - h * 0.45f, w * 1.4f, h * 0.9f };
            cs.Add(r, 24, facing * 650.0f, -380, 0.03f,
                   Team::Enemy, HitKind::BossDash, dashAttackId);
            if (stateTimer <= 0) {
                dashesLeft--;
                if (dashesLeft > 0) {
                    state = BState::TeleDash;
                    stateTimer = 0.22f;
                } else {
                    EnterRecover(phase >= 4 ? 0.6f : (phase == 3 ? 0.75f : 1.0f));
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
                pos.x = Clampf(prey.x + side * 125.0f, 60.0f, (float)cfg::SCREEN_W - 60.0f);
                pos.y = cfg::GROUND_Y - h * 0.5f;
                facing = prey.x > pos.x ? 1 : -1;
                fx.DeathBurst(pos, C(200, 30, 55), 0.8f);
                fx.AddShake(0.2f);
                PlaySfx(SFX_WHOOSH, 0.9f, 0.6f);
                state = BState::TeleStrike;
                stateTimer = phase >= 3 ? 0.14f : 0.2f;
            }
            break;
        }
        case BState::TeleStrike: {
            vel.x = 0;
            stateTimer -= dt;
            if (stateTimer <= 0) {
                Rectangle r = {
                    facing > 0 ? pos.x : pos.x - 130,
                    pos.y - 60, 130, 120
                };
                cs.Add(r, 26, facing * 700.0f, -420, 0.06f,
                       Team::Enemy, HitKind::BossAoe, cs.NewId());
                float a0 = facing > 0 ? -70.0f : 250.0f;
                float a1 = facing > 0 ? 70.0f : 110.0f;
                fx.SlashArc(pos, 110, a0, a1, C(235, 45, 65));
                PlaySfx(SFX_SLASH, 0.9f, 0.7f);
                EnterRecover(phase >= 4 ? 0.55f : (phase == 3 ? 0.7f : 0.95f));
            }
            break;
        }
        case BState::TeleBlades: {
            vel.x = 0;
            stateTimer -= dt;
            // blades visibly grow out of his body during the telegraph (drawn in Draw)
            if (stateTimer <= 0) {
                int n = phase >= 4 ? 16 : (phase == 3 ? 14 : 10);
                for (int i = 0; i < n; i++) {
                    float a = (360.0f / n) * i * DEG2RAD + frnd(-0.06f, 0.06f);
                    Crescent c;
                    c.pos = { pos.x, pos.y - 10 };
                    c.vel = { cosf(a) * 430.0f, sinf(a) * 430.0f };
                    crescents.push_back(c);
                }
                fx.Ring({ pos.x, pos.y - 10 }, 14, 220, 640, 9, C(225, 35, 55));
                fx.Sparks({ pos.x, pos.y - 10 }, -90, 360, 20, C(235, 60, 70), 420, 3);
                fx.AddShake(0.3f);
                PlaySfx(SFX_EXPLO, 0.6f, 1.3f);
                EnterRecover(phase >= 3 ? 0.7f : 0.9f);
            }
            break;
        }
        case BState::TeleClaws: {
            vel.x = 0;
            stateTimer -= dt;
            if (stateTimer <= 0) {
                state = BState::Claws;
                stateTimer = 0.8f;
                ringsAtk.clear();
                for (int i = 0; i < 3; i++)
                    ringsAtk.push_back({ pos, 26.0f - i * 22.0f, false });
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
                    if (fabsf(d - rg.r) < 30 && player.pos.y > cfg::GROUND_Y - 130) {
                        rg.hitDone = true;
                        float dir = player.pos.x >= rg.center.x ? 1.0f : -1.0f;
                        player.TakeDamage(50, dir * 550.0f, fx, true);
                    }
                    // the shockwave respects no ally either
                    if (!rg.hitDone && ally && ally->Active()) {
                        float dA = Dist(ally->pos, rg.center);
                        if (fabsf(dA - rg.r) < 30 && ally->pos.y > cfg::GROUND_Y - 130) {
                            rg.hitDone = true;
                            float dir = ally->pos.x >= rg.center.x ? 1.0f : -1.0f;
                            ally->TakeDamage(40, dir * 550.0f, HitKind::BossAoe, fx);
                        }
                    }
                    if (!rg.hitDone && shinobu && shinobu->Active()) {
                        float dS = Dist(shinobu->pos, rg.center);
                        if (fabsf(dS - rg.r) < 30 && shinobu->pos.y > cfg::GROUND_Y - 130) {
                            rg.hitDone = true;
                            float dir = shinobu->pos.x >= rg.center.x ? 1.0f : -1.0f;
                            shinobu->TakeDamage(40, dir * 550.0f, HitKind::BossAoe, fx);
                        }
                    }
                    if (!rg.hitDone && rengoku && rengoku->Active()) {
                        float dR = Dist(rengoku->pos, rg.center);
                        if (fabsf(dR - rg.r) < 30 && rengoku->pos.y > cfg::GROUND_Y - 130) {
                            rg.hitDone = true;
                            float dir = rengoku->pos.x >= rg.center.x ? 1.0f : -1.0f;
                            rengoku->TakeDamage(40, dir * 550.0f, HitKind::BossAoe, fx);
                        }
                    }
                }
            }
            if (stateTimer <= 0) {
                ringsAtk.clear();
                EnterRecover(phase >= 4 ? 0.7f : (phase == 3 ? 0.85f : 1.15f));
            }
            break;
        }
        case BState::Summon: {
            vel.x = 0;
            stateTimer -= dt;
            fx.Ember({ pos.x + frnd(-50, 50), pos.y - h * 0.5f });
            if (stateTimer <= 0) {
                summonRequest = 1 + phase;      // game spawns the demons
                fx.AddShake(0.25f);
                fx.Text({ pos.x, pos.y - h }, C(230, 60, 200), 1.1f, "RISE, MY DEMONS");
                PlaySfx(SFX_ROAR, 0.6f, 1.2f);
                state = BState::Stalk;
                decideTimer = frnd(1.2f, 1.8f);
            }
            break;
        }
        case BState::TeleCrescent: {
            vel.x = 0;
            stateTimer -= dt;
            facing = prey.x > pos.x ? 1 : -1;
            if (stateTimer <= 0) {
                int n = phase >= 4 ? 7 : (phase == 3 ? 5 : 3);
                Vector2 origin = { pos.x + facing * 30.0f, pos.y - 16 };
                float baseAng = atan2f(prey.y - origin.y, prey.x - origin.x);
                for (int i = 0; i < n; i++) {
                    float a = baseAng + (i - (n - 1) * 0.5f) * 0.16f;
                    Crescent c;
                    c.pos = origin;
                    c.vel = { cosf(a) * 470.0f, sinf(a) * 470.0f };
                    crescents.push_back(c);
                }
                fx.Sparks(origin, facing > 0 ? 0.0f : 180.0f, 60, 10, C(255, 40, 60), 380, 3);
                PlaySfx(SFX_SLASH, 0.7f, 0.6f);
                EnterRecover(0.7f);
            }
            break;
        }
        case BState::Desperation: {
            stateTimer -= dt;
            vel.x = 0;
            if (!despBlasted) {
                // the night itself is drawn into his flesh
                for (int i = 0; i < 2; i++) {
                    float a = frnd(0, 360);
                    Vector2 sp = { pos.x + cosf(a * DEG2RAD) * 110.0f,
                                   pos.y - 10 + sinf(a * DEG2RAD) * 110.0f };
                    fx.Sparks(sp, a + 180.0f, 8, 1, C(220, 30, 50), 520, 3);
                }
                fx.Ember({ pos.x + frnd(-w, w), pos.y + frnd(-h * 0.5f, h * 0.5f) });
                if (stateTimer <= 2.5f) {
                    despBlasted = true;
                    // THE ERUPTION - wave after wave of it
                    ringsAtk.clear();
                    for (int i = 0; i < 6; i++)
                        ringsAtk.push_back({ { pos.x, pos.y - 8 }, 26.0f - i * 240.0f, false });
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
                    rg.r += 860.0f * dt;
                    if (!rg.hitDone && rg.r > 20 && rg.r < 560) {
                        if (fabsf(Dist(player.pos, rg.center) - rg.r) < 34) {
                            rg.hitDone = true;
                            float dir = player.pos.x >= rg.center.x ? 1.0f : -1.0f;
                            player.TakeDamage(70, dir * 820.0f, fx, true);
                        }
                        if (!rg.hitDone && ally && ally->Active() &&
                            fabsf(Dist(ally->pos, rg.center) - rg.r) < 34) {
                            rg.hitDone = true;
                            float dir = ally->pos.x >= rg.center.x ? 1.0f : -1.0f;
                            ally->TakeDamage(50, dir * 820.0f, HitKind::BossAoe, fx);
                        }
                        if (!rg.hitDone && shinobu && shinobu->Active() &&
                            fabsf(Dist(shinobu->pos, rg.center) - rg.r) < 34) {
                            rg.hitDone = true;
                            float dir = shinobu->pos.x >= rg.center.x ? 1.0f : -1.0f;
                            shinobu->TakeDamage(50, dir * 820.0f, HitKind::BossAoe, fx);
                        }
                        if (!rg.hitDone && rengoku && rengoku->Active() &&
                            fabsf(Dist(rengoku->pos, rg.center) - rg.r) < 34) {
                            rg.hitDone = true;
                            float dir = rengoku->pos.x >= rg.center.x ? 1.0f : -1.0f;
                            rengoku->TakeDamage(50, dir * 820.0f, HitKind::BossAoe, fx);
                        }
                    }
                }
            }
            if (stateTimer <= 0) {
                ringsAtk.clear();
                state = BState::Stalk;
                decideTimer = 0.8f;
            }
            break;
        }
        case BState::Recover: {
            stateTimer -= dt;
            vel.x *= 1.0f - Clampf(5.0f * dt, 0, 1);
            if (stateTimer <= 0) {
                state = BState::Stalk;
                decideTimer = frnd(0.7f, 1.3f) / (0.8f + 0.25f * phase);
            }
            break;
        }
        case BState::Dying: {
            stateTimer -= dt;
            vel.x = 0;
            if (fmodf(stateTimer, 0.18f) < 0.05f) {
                fx.DeathBurst({ pos.x + frnd(-w, w), pos.y + frnd(-h * 0.5f, h * 0.5f) },
                              C(220, 40, 60), 0.8f);
                fx.AddShake(0.2f);
            }
            if (stateTimer <= 0) {
                state = BState::Dead;
                fx.FireExplosion(pos);
                fx.DeathBurst(pos, C(230, 40, 60), 2.6f);
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
            if (player.TakeDamage(24, (c.vel.x > 0 ? 1 : -1) * 380.0f, fx))
                c.alive = false;
        }
        if (c.alive && ally && ally->Active() &&
            CheckCollisionCircleRec(c.pos, 13, ally->Rect())) {
            ally->TakeDamage(24, (c.vel.x > 0 ? 1 : -1) * 380.0f,
                             HitKind::BossProjectile, fx);
            c.alive = false;
        }
        if (c.alive && shinobu && shinobu->Active() &&
            CheckCollisionCircleRec(c.pos, 13, shinobu->Rect())) {
            shinobu->TakeDamage(24, (c.vel.x > 0 ? 1 : -1) * 380.0f,
                                HitKind::BossProjectile, fx);
            c.alive = false;
        }
        if (c.alive && rengoku && rengoku->Active() &&
            CheckCollisionCircleRec(c.pos, 13, rengoku->Rect())) {
            rengoku->TakeDamage(24, (c.vel.x > 0 ? 1 : -1) * 380.0f,
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
        state == BState::Desperation) return;

    if (dmg <= 0) {                          // status-only field (mist cloud)
        if (kind == HitKind::Water) slowTimer = fmaxf(slowTimer, 0.8f);
        if (kind == HitKind::Shinobu) poisonT = fmaxf(poisonT, 2.5f);
        return;
    }

    float mult = (vulnerable || guardBroken > 0) ? 1.0f : 0.55f;
    if (kind == HitKind::Fire && vulnerable) mult = 1.5f;
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
    if (kind == HitKind::Stone && guardBroken <= 0) {
        guardBroken = 4.0f;
        fx.Text({ pos.x, pos.y - h * 0.5f - 34 }, C(210, 200, 185), 1.2f, "GUARD BROKEN");
        fx.Ring(pos, 16, 130, 480, 7, C(210, 200, 185));
    }

    float dealt = dmg * mult;
    hp -= dealt;
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
    fx.Text({ pos.x, pos.y - h * 0.5f - 16 }, tcol,
            (vulnerable || guardBroken > 0) ? 1.25f : 0.95f, "%.0f", dealt);
    fx.HitSparks({ pos.x, pos.y - 10 }, kbx >= 0 ? 1 : -1, tcol);

    if (hp <= 0) {
        hp = 0;
        state = BState::Dying;
        stateTimer = 2.2f;
        vulnerable = false;
        crescents.clear();
        ringsAtk.clear();
        fx.AddShake(0.8f);
        fx.AddHitstop(0.5f);
    }
}

bool Boss::ForceOpening(Effects& fx) {
    if (!active || openingCd > 0) return false;
    bool interruptible =
        state == BState::Stalk || state == BState::TeleDash ||
        state == BState::TeleClaws || state == BState::TeleCrescent ||
        state == BState::TeleBlades;
    if (!interruptible) return false;
    openingCd = 12.0f;
    dashesLeft = 0;
    EnterRecover(1.1f);
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
                         state == BState::TeleStrike);
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
        Rectangle warn = { facing > 0 ? pos.x : pos.x - 130, pos.y - 60, 130, 120 };
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

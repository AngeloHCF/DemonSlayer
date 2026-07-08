#include "moons.h"
#include "player.h"
#include "companion.h"
#include "shinobu.h"
#include "effects.h"
#include "config.h"
#include "audio.h"
#include <cmath>
#include <algorithm>

// per-kind tuning: Douma < Kokushibo, both above Akaza, below Muzan
static float MoonHp(int k)    { return k == MOON_DOUMA ? 1000.0f : 1150.0f; }
static float MoonSpd(int k)   { return k == MOON_DOUMA ? 240.0f : 290.0f; }
static float MoonGuard(int k) { return k == MOON_DOUMA ? 0.62f : 0.58f; }
static Color MoonCol(int k)   { return k == MOON_DOUMA ? Color{ 150, 220, 245, 255 }
                                                       : Color{ 190, 130, 235, 255 }; }

void UpperMoon::Reset() {
    active = false;
    state = MState::Inactive;
    pos = {0, 0}; vel = {0, 0};
    facing = -1;
    hp = maxHp = MoonHp(kind);
    phase = 1;
    vulnerable = false;
    guardBroken = 0;
    hitMem.Clear();
    stateTimer = 0; decideTimer = 0; tickT = 0;
    volley = 0;
    slowTimer = 0; openingCd = 0;
    poisonT = 0; poisonTick = 0;
    hitFlash = 0;
    ghostA = 1.0f;
    preyAlly = false;
    preyShinobu = false;
    shards.clear();
    lotus.clear();
    slashArmed = false;
}

void UpperMoon::Activate(Vector2 p) {
    Reset();
    active = true;
    pos = p;
    state = MState::Intro;
    stateTimer = 1.6f;
    if (kind == MOON_DOUMA) PlaySfx(SFX_MIST, 1.0f, 0.7f);
    else                    PlaySfx(SFX_ROAR, 0.9f, 0.8f);
}

Rectangle UpperMoon::Rect() const {
    return { pos.x - w * 0.5f, pos.y - h * 0.5f, w, h };
}

void UpperMoon::EnterRecover(float t) {
    state = MState::Recover;
    stateTimer = t;
}

void UpperMoon::ChooseAttack(const Player& player, const Giyu* ally, const Shinobu* shinobu) {
    preyAlly = false;
    preyShinobu = false;
    bool gActive = ally && ally->Active();
    bool sActive = shinobu && shinobu->Active();
    if ((gActive || sActive) && GetRandomValue(0, 99) < 40) {
        if (gActive && sActive) {
            float dg = fabsf(ally->pos.x - pos.x);
            float ds = fabsf(shinobu->pos.x - pos.x);
            preyAlly = dg <= ds;
            preyShinobu = !preyAlly;
        } else {
            preyAlly = gActive;
            preyShinobu = sActive;
        }
    }
    Vector2 prey = preyAlly ? ally->pos : (preyShinobu ? shinobu->pos : player.pos);
    float dist = fabsf(prey.x - pos.x);
    int roll = GetRandomValue(0, 99);

    if (kind == MOON_DOUMA) {
        // a caster: prefers range, blooms lotus underfoot when pressed
        if (dist < 200)      state = roll < 55 ? MState::TeleB : MState::TeleC;
        else                 state = roll < 45 ? MState::TeleA
                                   : roll < 80 ? MState::TeleB : MState::TeleC;
        switch (state) {
            case MState::TeleA: stateTimer = 0.45f; break;
            case MState::TeleB: stateTimer = 0.50f; break;
            default:            stateTimer = 0.42f; break;
        }
    } else {
        // a swordsman: pressure, reach, and sudden appearances
        if (dist > 320)      state = roll < 50 ? MState::TeleB : MState::TeleC;
        else                 state = roll < 40 ? MState::TeleA
                                   : roll < 70 ? MState::TeleB : MState::TeleC;
        switch (state) {
            case MState::TeleA: stateTimer = 0.40f; break;
            case MState::TeleB: stateTimer = 0.55f; break;   // the long slash telegraph
            default:            stateTimer = 0.38f; break;
        }
    }
    if (phase >= 2) stateTimer *= 0.75f;
}

void UpperMoon::Update(float dt, Player& player, Giyu* ally, Shinobu* shinobu,
                       CombatSystem& cs, Effects& fx) {
    if (!active || state == MState::Dead) return;

    hitFlash  = fmaxf(hitFlash - dt, 0);
    slowTimer = fmaxf(slowTimer - dt, 0);
    guardBroken = fmaxf(guardBroken - dt, 0);
    openingCd = fmaxf(openingCd - dt, 0);
    ghostA += (1.0f - ghostA) * Clampf(6.0f * dt, 0, 1);

    if (poisonT > 0 && state != MState::Dying) {
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

    int newPhase = hp > maxHp * 0.40f ? 1 : 2;
    if (newPhase != phase && state != MState::Dying && state != MState::Desperation) {
        phase = newPhase;
        if (kind == MOON_KOKU) {
            // desperation: the strongest swordsman alive refuses the grave
            state = MState::Desperation;
            stateTimer = 20.0f;
            tickT = 0;
            vel.x = 0;
            slashArmed = false;
            fx.AddShake(0.90f);
            fx.AddHitstop(0.25f);
            fx.Ring(pos, 20, 420, 800, 12, MoonCol(kind));
            fx.Text({ pos.x, pos.y - h - 26 }, C(255, 70, 85), 4.0f, "I  WILL  NOT  DIE");
            PlaySfx(SFX_ROAR, 1.0f, 0.5f);
        } else {
            fx.AddShake(0.55f);
            fx.Ring(pos, 18, 320, 720, 10, MoonCol(kind));
            fx.Text({ pos.x, pos.y - h - 10 }, MoonCol(kind), 1.4f, "DOUMA STOPS SMILING");
            PlaySfx(SFX_ROAR, 0.9f, 1.05f);
        }
    }

    float spd = (phase >= 2 ? MoonSpd(kind) * 1.15f : MoonSpd(kind))
              * (slowTimer > 0 ? 0.55f : 1.0f);
    vulnerable = (state == MState::Recover);

    bool huntingAlly = preyAlly && ally && ally->Active();
    bool huntingShinobu = preyShinobu && shinobu && shinobu->Active();
    Vector2 prey = huntingAlly ? ally->pos : (huntingShinobu ? shinobu->pos : player.pos);

    switch (state) {
        case MState::Intro: {
            stateTimer -= dt;
            if (kind == MOON_DOUMA) fx.MistWisp({ pos.x + frnd(-50, 50), pos.y + frnd(-60, 30) });
            else                    fx.Ember({ pos.x + frnd(-40, 40), pos.y + frnd(-50, 30) });
            if (stateTimer <= 0) { state = MState::Stalk; decideTimer = 1.1f; }
            break;
        }
        case MState::Stalk: {
            float dx = prey.x - pos.x;
            facing = dx > 0 ? 1 : -1;
            if (player.hiddenT > 0 && !huntingAlly) {
                vel.x *= 1.0f - Clampf(4.0f * dt, 0, 1);
            } else {
                float want = (kind == MOON_DOUMA) ? 260.0f : 150.0f;
                if (kind == MOON_DOUMA && fabsf(dx) < want - 60)
                    vel.x = -facing * spd;                   // glides away, smiling
                else if (fabsf(dx) > want)
                    vel.x = facing * spd;
                else
                    vel.x *= 1.0f - Clampf(6.0f * dt, 0, 1);
                decideTimer -= dt;
                if (decideTimer <= 0) ChooseAttack(player, ally, shinobu);
            }
            break;
        }
        case MState::TeleA: {
            vel.x = 0;
            stateTimer -= dt;
            facing = prey.x > pos.x ? 1 : -1;
            if (stateTimer <= 0) {
                state = MState::AtkA;
                volley = 0;
                tickT = 0;
                stateTimer = kind == MOON_DOUMA ? 0.6f : 0.8f;
            }
            break;
        }
        case MState::AtkA: {
            stateTimer -= dt;
            tickT -= dt;
            vel.x = 0;
            int volleys = (kind == MOON_DOUMA)
                          ? (phase >= 2 ? 3 : 2)
                          : (phase >= 2 ? 4 : 3);
            if (tickT <= 0 && volley < volleys) {
                tickT = 0.22f;
                volley++;
                int n = kind == MOON_DOUMA ? (phase >= 2 ? 8 : 6) : 4;
                Vector2 origin = { pos.x + facing * 26.0f, pos.y - 14 };
                float baseAng = atan2f(prey.y - origin.y, prey.x - origin.x);
                for (int i = 0; i < n; i++) {
                    float a = baseAng + (i - (n - 1) * 0.5f) * (kind == MOON_DOUMA ? 0.12f : 0.2f)
                              + frnd(-0.04f, 0.04f);
                    Shard s;
                    s.pos = origin;
                    s.vel = { cosf(a) * 500.0f, sinf(a) * 500.0f };
                    shards.push_back(s);
                }
                fx.Sparks(origin, facing > 0 ? 0.0f : 180.0f, 60, 8, MoonCol(kind), 380, 2.5f);
                PlaySfx(SFX_SLASH, 0.6f, kind == MOON_DOUMA ? 1.3f : 0.65f);
            }
            if (stateTimer <= 0) EnterRecover(phase >= 2 ? 0.65f : 0.85f);
            break;
        }
        case MState::TeleB: {
            vel.x = 0;
            stateTimer -= dt;
            facing = prey.x > pos.x ? 1 : -1;
            if (kind == MOON_KOKU) {
                // show where the moon-blade will sweep
                slashBand = { facing > 0 ? pos.x + 20 : pos.x - 20 - 560,
                              cfg::GROUND_Y - 100, 560, 58 };
                slashArmed = true;
            }
            if (stateTimer <= 0) {
                if (kind == MOON_DOUMA) {
                    // frozen lotus: ice blooms beneath his prey
                    state = MState::AtkB;
                    stateTimer = 1.7f;
                    lotus.clear();
                    for (int i = 0; i < (phase >= 2 ? 4 : 3); i++) {
                        Lotus lz;
                        lz.pos = { Clampf(prey.x + (i - 1) * 130.0f + frnd(-30, 30),
                                          60.0f, (float)cfg::SCREEN_W - 60.0f),
                                   cfg::GROUND_Y };
                        lz.fuse = 0.85f + i * 0.22f;
                        lotus.push_back(lz);
                    }
                    PlaySfx(SFX_MIST, 0.8f, 0.8f);
                } else {
                    // the long slash: an arena-wide cut at chest height. DUCK.
                    state = MState::AtkB;
                    stateTimer = 0.35f;
                    slashArmed = false;
                    cs.Add(slashBand, 24, facing * 560.0f, -240, 0.08f,
                           Team::Enemy, HitKind::BossAoe, cs.NewId());
                    fx.SlashArc({ slashBand.x + slashBand.width * 0.5f,
                                  slashBand.y + slashBand.height * 0.5f },
                                260, facing > 0 ? -10.0f : 190.0f,
                                facing > 0 ? 10.0f : 170.0f, MoonCol(kind));
                    fx.AddShake(0.35f);
                    PlaySfx(SFX_SLASH, 1.0f, 0.55f);
                }
            }
            break;
        }
        case MState::AtkB: {
            stateTimer -= dt;
            vel.x = 0;
            if (kind == MOON_DOUMA) {
                for (auto& lz : lotus) {
                    if (lz.done) continue;
                    lz.fuse -= dt;
                    if (lz.fuse <= 0) {
                        lz.done = true;
                        // eruption of ice petals
                        fx.Ring({ lz.pos.x, lz.pos.y - 10 }, 10, 110, 520, 8, MoonCol(kind));
                        fx.Sparks({ lz.pos.x, lz.pos.y - 10 }, -90, 120, 14, C(220, 245, 255), 460, 3);
                        fx.AddShake(0.25f);
                        PlaySfx(SFX_STONE, 0.6f, 1.5f);
                        // direct radial hit
                        if (Dist(player.pos, { lz.pos.x, lz.pos.y - 20 }) < 90) {
                            float dir = player.pos.x >= lz.pos.x ? 1.0f : -1.0f;
                            if (player.TakeDamage(20, dir * 480.0f, fx, true))
                                player.chillT = 2.2f;
                        }
                        if (ally && ally->Active() &&
                            Dist(ally->pos, { lz.pos.x, lz.pos.y - 20 }) < 90) {
                            float dir = ally->pos.x >= lz.pos.x ? 1.0f : -1.0f;
                            ally->TakeDamage(20, dir * 480.0f, HitKind::BossAoe, fx);
                        }
                        if (shinobu && shinobu->Active() &&
                            Dist(shinobu->pos, { lz.pos.x, lz.pos.y - 20 }) < 90) {
                            float dir = shinobu->pos.x >= lz.pos.x ? 1.0f : -1.0f;
                            shinobu->TakeDamage(20, dir * 480.0f, HitKind::BossAoe, fx);
                        }
                    }
                }
            }
            if (stateTimer <= 0) {
                lotus.clear();
                EnterRecover(phase >= 2 ? 0.7f : 0.95f);
            }
            break;
        }
        case MState::TeleC: {
            vel.x = 0;
            stateTimer -= dt;
            facing = prey.x > pos.x ? 1 : -1;
            if (kind == MOON_KOKU) ghostA = fmaxf(ghostA - 3.0f * dt, 0.2f);  // fading out
            if (stateTimer <= 0) {
                if (kind == MOON_DOUMA) {
                    state = MState::AtkC;                    // freezing breath
                    stateTimer = 1.1f;
                    tickT = 0;
                    PlaySfx(SFX_WIND, 0.8f, 1.35f);
                } else {
                    // flash cross: he is simply... behind you
                    float side = (GetRandomValue(0, 1) == 0) ? -1.0f : 1.0f;
                    pos.x = Clampf(prey.x + side * 120.0f, 60.0f, (float)cfg::SCREEN_W - 60.0f);
                    pos.y = cfg::GROUND_Y - h * 0.5f;
                    facing = prey.x > pos.x ? 1 : -1;
                    ghostA = 0.25f;
                    fx.DeathBurst(pos, MoonCol(kind), 0.7f);
                    PlaySfx(SFX_WHOOSH, 0.9f, 0.55f);
                    state = MState::AtkC;
                    stateTimer = 0.16f;
                }
            }
            break;
        }
        case MState::AtkC: {
            stateTimer -= dt;
            vel.x = 0;
            if (kind == MOON_DOUMA) {
                // freezing breath: a cone of white cold
                tickT -= dt;
                Rectangle cone = { facing > 0 ? pos.x + 10 : pos.x - 10 - 250,
                                   pos.y - 64, 250, 116 };
                for (int i = 0; i < 3; i++)
                    fx.MistWisp({ cone.x + frnd(0, cone.width),
                                  cone.y + frnd(0, cone.height) });
                if (tickT <= 0) {
                    tickT = 0.25f;
                    if (CheckCollisionRecs(cone, player.Rect())) {
                        if (player.TakeDamage(6, facing * 140.0f, fx, false))
                            player.chillT = 2.4f;
                        else
                            player.chillT = fmaxf(player.chillT, 1.2f);  // cold seeps through
                    }
                    if (ally && ally->Active() && CheckCollisionRecs(cone, ally->Rect()))
                        ally->TakeDamage(6, facing * 140.0f, HitKind::BossProjectile, fx);
                    if (shinobu && shinobu->Active() && CheckCollisionRecs(cone, shinobu->Rect()))
                        shinobu->TakeDamage(6, facing * 140.0f, HitKind::BossProjectile, fx);
                }
                if (stateTimer <= 0) EnterRecover(phase >= 2 ? 0.7f : 0.95f);
            } else {
                if (stateTimer <= 0) {
                    Rectangle r = { facing > 0 ? pos.x : pos.x - 130, pos.y - 60, 130, 120 };
                    cs.Add(r, 26, facing * 640.0f, -400, 0.06f,
                           Team::Enemy, HitKind::BossAoe, cs.NewId());
                    float a0 = facing > 0 ? -70.0f : 250.0f;
                    float a1 = facing > 0 ? 70.0f : 110.0f;
                    fx.SlashArc(pos, 110, a0, a1, MoonCol(kind));
                    PlaySfx(SFX_SLASH, 0.9f, 0.6f);
                    EnterRecover(phase >= 2 ? 0.6f : 0.85f);
                }
            }
            break;
        }
        case MState::Desperation: {
            // moon crescents erupt from the earth across the entire field
            stateTimer -= dt;
            vel.x = 0;
            tickT -= dt;
            if (tickT <= 0) {
                tickT = 0.05f;
                for (int i = 0; i < 5; i++) {
                    Shard s;
                    float sx = frnd(30.0f, cfg::SCREEN_W - 30.0f);
                    s.pos = { sx, cfg::GROUND_Y - 10.0f };
                    float a = frnd(-155.0f, -25.0f) * DEG2RAD;
                    float sp = frnd(300.0f, 640.0f);
                    s.vel = { cosf(a) * sp, sinf(a) * sp };
                    s.spin = frnd(0, 360);
                    shards.push_back(s);
                    if (GetRandomValue(0, 2) == 0)
                        fx.Sparks({ sx, cfg::GROUND_Y - 4 }, -90, 60, 3,
                                  MoonCol(kind), 320, 2.5f);
                }
            }
            if (fmodf(stateTimer, 0.5f) < 0.02f) {
                fx.AddShake(0.3f);
                PlaySfx(SFX_SLASH, 0.5f, 0.5f);
            }
            if (stateTimer <= 0) EnterRecover(1.2f);   // spent, for a breath
            break;
        }
        case MState::Recover: {
            stateTimer -= dt;
            vel.x *= 1.0f - Clampf(5.0f * dt, 0, 1);
            if (stateTimer <= 0) {
                state = MState::Stalk;
                decideTimer = frnd(0.6f, 1.1f) / (phase >= 2 ? 1.25f : 1.0f);
            }
            break;
        }
        case MState::Dying: {
            stateTimer -= dt;
            vel.x = 0;
            if (fmodf(stateTimer, 0.2f) < 0.05f) {
                fx.DeathBurst({ pos.x + frnd(-w, w), pos.y + frnd(-h * 0.5f, h * 0.5f) },
                              MoonCol(kind), 0.8f);
                fx.AddShake(0.18f);
            }
            if (stateTimer <= 0) {
                state = MState::Dead;
                fx.DeathBurst(pos, MoonCol(kind), 2.2f);
                fx.Ring(pos, 20, 340, 700, 10, MoonCol(kind));
                fx.AddShake(0.9f);
                fx.AddHitstop(0.3f);
                PlaySfx(SFX_EXPLO, 0.9f, 0.85f);
                PlaySfx(SFX_ROAR, 0.9f, kind == MOON_DOUMA ? 1.25f : 0.6f);
            }
            break;
        }
        default: break;
    }

    // shards fly on
    for (auto& s : shards) {
        if (!s.alive) continue;
        s.pos.x += s.vel.x * dt;
        s.pos.y += s.vel.y * dt;
        s.spin += 640.0f * dt;
        float shardDmg = state == MState::Desperation ? 43.0f : 23.0f;
        if (CheckCollisionCircleRec(s.pos, 12, player.Rect())) {
            if (player.TakeDamage(shardDmg, (s.vel.x > 0 ? 1 : -1) * 320.0f, fx)) {
                if (kind == MOON_DOUMA) player.chillT = fmaxf(player.chillT, 1.6f);
                s.alive = false;
            }
        }
        if (s.alive && ally && ally->Active() &&
            CheckCollisionCircleRec(s.pos, 12, ally->Rect())) {
            ally->TakeDamage(shardDmg, (s.vel.x > 0 ? 1 : -1) * 320.0f,
                             HitKind::BossProjectile, fx);
            s.alive = false;
        }
        if (s.alive && shinobu && shinobu->Active() &&
            CheckCollisionCircleRec(s.pos, 12, shinobu->Rect())) {
            shinobu->TakeDamage(shardDmg, (s.vel.x > 0 ? 1 : -1) * 320.0f,
                                HitKind::BossProjectile, fx);
            s.alive = false;
        }
        if (s.pos.x < -60 || s.pos.x > cfg::SCREEN_W + 60 ||
            s.pos.y < -60 || s.pos.y > cfg::SCREEN_H + 60)
            s.alive = false;
        if (s.pos.y > cfg::GROUND_Y - 4) {
            s.alive = false;
            fx.Sparks({ s.pos.x, cfg::GROUND_Y - 4 }, -90, 110, 5, MoonCol(kind), 240, 2.2f);
        }
    }
    shards.erase(std::remove_if(shards.begin(), shards.end(),
                 [](const Shard& s) { return !s.alive; }), shards.end());

    // physics (Douma drifts weightlessly, Kokushibo strides)
    vel.y += cfg::GRAVITY * dt;
    pos.x += vel.x * dt;
    pos.y += vel.y * dt;
    pos.x = Clampf(pos.x, 50.0f, (float)cfg::SCREEN_W - 50.0f);
    GroundClamp(pos, vel, h * 0.5f);
}

void UpperMoon::TakeDamage(float dmg, float kbx, HitKind kindHit, Effects& fx) {
    if (!active || state == MState::Dying || state == MState::Dead ||
        state == MState::Intro) return;

    if (dmg <= 0) {
        if (kindHit == HitKind::Water) slowTimer = fmaxf(slowTimer, 0.8f);
        if (kindHit == HitKind::Shinobu) poisonT = fmaxf(poisonT, 2.5f);
        return;
    }

    float mult = (vulnerable || guardBroken > 0) ? 1.0f : MoonGuard(kind);
    if (kindHit == HitKind::Fire && vulnerable) mult = 1.4f;
    if (kindHit == HitKind::Water) slowTimer = 1.4f;
    if (kindHit == HitKind::Serpent) poisonT = 3.0f;
    if (kindHit == HitKind::Giyu) mult *= 0.3f;   // Hashira alone cannot fell an Upper Moon
    if (kindHit == HitKind::Shinobu) {
        mult *= 0.25f;
        poisonT = fmaxf(poisonT, 4.5f);
    }
    if (kindHit == HitKind::Stone && guardBroken <= 0) {
        guardBroken = 4.0f;
        fx.Text({ pos.x, pos.y - h * 0.5f - 34 }, C(210, 200, 185), 1.2f, "GUARD BROKEN");
        fx.Ring(pos, 16, 130, 480, 7, C(210, 200, 185));
    }

    float dealt = dmg * mult;
    hp -= dealt;
    hitFlash = 0.14f;

    Color tcol = (vulnerable || guardBroken > 0) ? C(255, 220, 90) : C(200, 200, 200);
    if (kindHit == HitKind::Fire)    tcol = C(255, 160, 60);
    if (kindHit == HitKind::Water)   tcol = C(110, 190, 255);
    if (kindHit == HitKind::Love)    tcol = C(255, 150, 205);
    if (kindHit == HitKind::Serpent) tcol = C(140, 220, 90);
    if (kindHit == HitKind::Wind)    tcol = C(215, 245, 230);
    if (kindHit == HitKind::Giyu)    tcol = C(120, 190, 255);
    if (kindHit == HitKind::Shinobu) tcol = C(190, 150, 255);
    fx.Text({ pos.x, pos.y - h * 0.5f - 16 }, tcol,
            (vulnerable || guardBroken > 0) ? 1.25f : 0.95f, "%.0f", dealt);
    fx.HitSparks({ pos.x, pos.y - 10 }, kbx >= 0 ? 1 : -1, tcol);

    if (hp <= 0) {
        hp = 0;
        state = MState::Dying;
        stateTimer = 2.0f;
        vulnerable = false;
        shards.clear();
        lotus.clear();
        slashArmed = false;
        fx.AddShake(0.7f);
        fx.AddHitstop(0.4f);
    }
}

bool UpperMoon::ForceOpening(Effects& fx) {
    if (!active || openingCd > 0) return false;
    bool interruptible =
        state == MState::Stalk || state == MState::TeleA ||
        state == MState::TeleB || state == MState::TeleC;
    if (!interruptible) return false;
    openingCd = 9.0f;
    slashArmed = false;
    EnterRecover(1.0f);
    fx.Text({ pos.x, pos.y - h - 14 }, C(120, 190, 255), 1.25f, "GIYU CREATES AN OPENING");
    fx.Ring(pos, 16, 150, 520, 8, C(120, 190, 255));
    fx.AddShake(0.22f);
    return true;
}

int UpperMoon::NullifyShards(Vector2 c, float r) {
    int cut = 0;
    for (auto& s : shards)
        if (s.alive && Dist(s.pos, c) < r) { s.alive = false; cut++; }
    return cut;
}

int UpperMoon::ShardsNear(Vector2 c, float r) const {
    int n = 0;
    for (const auto& s : shards)
        if (s.alive && Dist(s.pos, c) < r) n++;
    return n;
}

bool UpperMoon::Menacing(Vector2 a, Vector2 b) const {
    if (!Alive()) return false;
    if (state == MState::AtkA || state == MState::AtkB || state == MState::AtkC)
        return true;
    return ShardsNear(a, 260) >= 2 || ShardsNear(b, 220) >= 2;
}

// ---------------------------------------------------------------- drawing

void UpperMoon::Draw() const {
    if (!active || state == MState::Dead) return;
    float gt = (float)GetTime();
    Color mc = MoonCol(kind);
    float bodyA = kind == MOON_KOKU ? ghostA : 1.0f;

    // lotus telegraphs bloom on the ground
    for (const auto& lz : lotus) {
        if (lz.done) continue;
        float p = 1.0f - Clampf(lz.fuse / 1.0f, 0, 1);
        DrawCircleV({ lz.pos.x, lz.pos.y - 6 }, 20 + 60 * p, Fade(mc, 0.13f + 0.1f * p));
        DrawRing({ lz.pos.x, lz.pos.y - 6 }, 78, 84, 0, 360, 32,
                 Fade(mc, 0.25f + 0.2f * sinf(gt * 12.0f)));
        // six frost petals closing inward
        for (int i = 0; i < 6; i++) {
            float a = (60.0f * i + gt * 40.0f) * DEG2RAD;
            Vector2 pp = { lz.pos.x + cosf(a) * (70 - 40 * p), lz.pos.y - 6 + sinf(a) * (24 - 14 * p) };
            DrawCircleV(pp, 4, Fade(C(230, 248, 255), 0.6f));
        }
    }

    // the long-slash warning band
    if (kind == MOON_KOKU && slashArmed) {
        float pulse = 0.16f + 0.12f * sinf(gt * 26.0f);
        DrawRectangleRec(slashBand, Fade(mc, pulse));
        DrawRectangle((int)slashBand.x, (int)(slashBand.y + slashBand.height * 0.5f - 2),
                      (int)slashBand.width, 4, Fade(C(255, 240, 255), pulse + 0.15f));
    }

    // shadow
    DrawEllipse((int)pos.x, (int)(cfg::GROUND_Y + 6), 24, 7, Fade(BLACK, 0.4f * bodyA));

    float lean = 0;
    float hover = kind == MOON_DOUMA ? sinf(gt * 3.0f) * 4.0f : 0.0f;
    float bx = pos.x + lean, by = pos.y - hover;
    bool telegraphing = (state == MState::TeleA || state == MState::TeleB ||
                         state == MState::TeleC);

    Color robe = kind == MOON_DOUMA ? C(236, 240, 246) : C(58, 42, 82);
    Color robe2 = kind == MOON_DOUMA ? C(206, 222, 236) : C(44, 32, 64);
    Color hair = kind == MOON_DOUMA ? C(228, 234, 242) : C(24, 18, 32);
    Color skin = C(238, 226, 218);
    if (hitFlash > 0) { robe = C(255, 160, 150); robe2 = robe; }
    if (telegraphing && fmodf(gt * 12.0f, 1.0f) < 0.5f)
        robe = kind == MOON_DOUMA ? C(200, 240, 255) : C(150, 90, 190);

    if (vulnerable) {
        float pulse = 0.45f + 0.3f * sinf(gt * 10.0f);
        DrawRectangleLinesEx({ bx - w * 0.5f - 4, by - h * 0.5f - 4, w + 8, h + 8 },
                             3, Fade(C(255, 210, 80), pulse));
    } else if (guardBroken > 0) {
        float pulse = 0.35f + 0.25f * sinf(gt * 12.0f);
        DrawRectangleLinesEx({ bx - w * 0.5f - 4, by - h * 0.5f - 4, w + 8, h + 8 },
                             3, Fade(C(210, 200, 185), pulse));
    }

    // legs / robe skirt
    DrawRectangleRounded({ bx - 11, by + 6, 22, 30 }, 0.3f, 4, Fade(robe2, bodyA));
    // torso
    DrawRectangleRounded({ bx - 12, by - 22, 24, 32 }, 0.3f, 4, Fade(robe, bodyA));
    if (kind == MOON_DOUMA) {
        // rainbow-trimmed collar, serene
        DrawRectangle((int)(bx - 12), (int)(by - 22), 24, 4, Fade(C(240, 190, 120), bodyA));
        DrawRectangle((int)(bx - 2), (int)(by - 18), 4, 24, Fade(C(180, 220, 240), bodyA));
    } else {
        // kimono cross + upper moon patterning
        DrawLineEx({ bx - 10, by - 20 }, { bx + 4, by + 6 }, 3, Fade(C(90, 70, 130), bodyA));
        DrawLineEx({ bx + 10, by - 20 }, { bx - 4, by + 6 }, 3, Fade(C(90, 70, 130), bodyA));
    }
    // arms
    if (state == MState::AtkA || telegraphing) {
        DrawLineEx({ bx + facing * 8.0f, by - 14 }, { bx + facing * 20.0f, by - 24 }, 5, Fade(robe, bodyA));
    } else {
        DrawLineEx({ bx + facing * 8.0f, by - 14 }, { bx + facing * 16.0f, by + 2 }, 5, Fade(robe, bodyA));
    }
    // head
    Vector2 headC = { bx + facing * 2.0f, by - 31 };
    DrawCircleV(headC, 10, Fade(skin, bodyA));
    DrawCircleSector(headC, 11.5f, 160, 380, 14, Fade(hair, bodyA));
    if (kind == MOON_DOUMA) {
        // long pale side strands + lotus crown hints
        DrawRectangle((int)(headC.x - 13), (int)(headC.y - 2), 4, 22, Fade(hair, bodyA));
        DrawRectangle((int)(headC.x + 9),  (int)(headC.y - 2), 4, 22, Fade(hair, bodyA));
        DrawCircleV({ headC.x - 6, headC.y - 11 }, 2.5f, Fade(C(240, 190, 120), bodyA));
        DrawCircleV({ headC.x + 6, headC.y - 11 }, 2.5f, Fade(C(240, 190, 120), bodyA));
        // rainbow eyes, always smiling
        DrawCircleV({ headC.x + facing * 4.0f, headC.y + 1 }, 2.2f, Fade(C(120, 200, 240), bodyA));
        DrawCircleV({ headC.x - facing * 2.0f, headC.y + 1 }, 2.2f, Fade(C(240, 160, 120), bodyA));
    } else {
        // long dark mane
        DrawRectangle((int)(headC.x - facing * 13), (int)(headC.y - 2), 5, 26, Fade(hair, bodyA));
        // six eyes: three crimson pairs
        for (int i = 0; i < 3; i++) {
            float ey = headC.y - 3 + i * 4.0f;
            DrawCircleV({ headC.x + facing * 5.0f, ey }, 1.6f, Fade(C(235, 60, 60), bodyA));
            DrawCircleV({ headC.x + facing * 1.0f, ey }, 1.6f, Fade(C(235, 60, 60), bodyA));
        }
        // the flesh katana
        Vector2 hand = { bx + facing * 15.0f, by - 4 };
        float ang = state == MState::AtkB ? 0.0f : 35.0f;
        if (facing < 0) ang = 180.0f - ang;
        Rectangle blade = { hand.x, hand.y, 52, 5 };
        DrawRectanglePro(blade, { 0, 2.5f }, ang, Fade(C(180, 90, 160), bodyA));
        Rectangle hilt = { hand.x, hand.y, 8, 7 };
        DrawRectanglePro(hilt, { 4, 3.5f }, ang, Fade(C(60, 30, 60), bodyA));
    }

    // freezing breath cone
    if (kind == MOON_DOUMA && state == MState::AtkC) {
        Vector2 mouth = { bx + facing * 10.0f, by - 26 };
        Vector2 top = { mouth.x + facing * 250.0f, mouth.y - 46 };
        Vector2 bot = { mouth.x + facing * 250.0f, mouth.y + 62 };
        if (facing > 0) DrawTriangle(mouth, bot, top, Fade(C(225, 245, 255), 0.22f));
        else            DrawTriangle(mouth, top, bot, Fade(C(225, 245, 255), 0.22f));
    }

    // shards
    for (const auto& s : shards) {
        if (!s.alive) continue;
        if (kind == MOON_DOUMA) {
            DrawPoly(s.pos, 4, 9, s.spin, Fade(C(200, 240, 255), 0.9f));
            DrawPoly(s.pos, 4, 5, s.spin, C(240, 252, 255));
        } else {
            float angDeg = atan2f(s.vel.y, s.vel.x) * RAD2DEG;
            DrawRing(s.pos, 7, 13, angDeg - 100, angDeg + 100, 16, mc);
            DrawRing(s.pos, 9, 11, angDeg - 80, angDeg + 80, 12, C(230, 200, 250));
        }
    }
}

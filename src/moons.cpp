#include "moons.h"
#include "player.h"
#include "companion.h"
#include "shinobu.h"
#include "rengoku.h"
#include "gyomei.h"
#include "tengen.h"
#include "sanemi.h"
#include "effects.h"
#include "config.h"
#include "audio.h"
#include <cmath>
#include <algorithm>

// per-kind tuning: Douma < Kokushibo, both above Akaza, below Muzan.
// Kokushibo is the strongest Upper Moon: a wall of HP and relentless pressure.
static float MoonHp(int k)    { return k == MOON_DOUMA ? 1000.0f : 2050.0f; }
static float MoonSpd(int k)   { return k == MOON_DOUMA ? 240.0f : 300.0f; }
static float MoonGuard(int k) { return k == MOON_DOUMA ? 0.62f : 0.55f; }
static Color MoonCol(int k)   { return k == MOON_DOUMA ? Color{ 150, 220, 245, 255 }
                                                       : Color{ 190, 130, 235, 255 }; }

// Kokushibo never runs. He walks toward you — slow, certain, unbothered —
// and only *explodes* into motion for a strike. Speed climbs with each phase.
static float KokuWalk(int phase) {
    return 118.0f * (phase >= 3 ? 1.42f : phase >= 2 ? 1.20f : 1.0f);
}
// Per-crescent bite. Bounded so the screen-filling curtains pressure position
// rather than delete you outright (the player carries 200 HP + 0.9s i-frames).
static float KokuCrescentDmg(int phase) {
    return phase >= 3 ? 21.0f : phase >= 2 ? 18.0f : 16.0f;
}

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
    preyRengoku = false;
    preyGyomei = false;
    preyTengen = false;
    preySanemi = false;
    shards.clear();
    lotus.clear();
    slashArmed = false;
    slashArmed2 = false;
    slashBand2 = {};
    auraPulse = 0;
    walkBob = 0;
    transformed = false;
    comboHit = 0;
    subT = 0; subStep = 0;
    stareGlare = 0;
    stormCd = 0;
    stareCd = 6.0f;
    markPos = {};
}

void UpperMoon::Activate(Vector2 p) {
    Reset();
    active = true;
    pos = p;
    state = MState::Intro;
    if (kind == MOON_DOUMA) {
        stateTimer = 1.6f;
        PlaySfx(SFX_MIST, 1.0f, 0.7f);
    } else {
        // the strongest Upper Moon arrives: the night itself recoils
        stateTimer = 2.2f;
        ghostA = 0.0f;                 // materialises out of the dark
        SetBossDrone(1);
        PlaySfx(SFX_ROAR, 1.0f, 0.5f);
    }
}

Rectangle UpperMoon::Rect() const {
    return { pos.x - w * 0.5f, pos.y - h * 0.5f, w, h };
}

void UpperMoon::EnterRecover(float t) {
    state = MState::Recover;
    stateTimer = t;
}

void UpperMoon::ChooseAttack(const Player& player, const Giyu* ally, const Shinobu* shinobu,
                             const Rengoku* rengoku, const Gyomei* gyomei, const Tengen* tengen,
                             const Sanemi* sanemi) {
    preyAlly = false;
    preyShinobu = false;
    preyRengoku = false;
    preyGyomei = false;
    preyTengen = false;
    preySanemi = false;
    bool gActive = ally && ally->Active();
    bool sActive = shinobu && shinobu->Active();
    bool rActive = rengoku && rengoku->Active();
    bool yActive = gyomei && gyomei->Active();
    bool tActive = tengen && tengen->Active();
    bool nActive = sanemi && sanemi->Active();
    if ((gActive || sActive || rActive || yActive || tActive || nActive) && GetRandomValue(0, 99) < 40) {
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
            best = fabsf(tengen->pos.x - pos.x);
            preyAlly = false; preyShinobu = false; preyRengoku = false; preyGyomei = false; preyTengen = true;
        }
        if (nActive && fabsf(sanemi->pos.x - pos.x) < best) {
            preyAlly = false; preyShinobu = false; preyRengoku = false; preyGyomei = false; preyTengen = false; preySanemi = true;
        }
    }
    Vector2 prey = preyAlly ? ally->pos
                 : (preyShinobu ? shinobu->pos
                 : (preyRengoku ? rengoku->pos
                 : (preyGyomei ? gyomei->pos
                 : (preyTengen ? tengen->pos
                 : (preySanemi ? sanemi->pos : player.pos)))));
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
        // Kokushibo — the swordsman: relentless pressure, reach, and sudden
        // appearances. His silent stare baits panic; his phase-3 storms drown
        // the arena in moonlight.
        markPos = prey;                          // remember the spot to punish later
        if (stareCd <= 0 && GetRandomValue(0, 99) < (phase >= 2 ? 32 : 24)) {
            state = MState::Stare;               // a pause held a beat too long
            stateTimer = frnd(0.55f, 0.95f);
            stareCd = frnd(6.5f, 9.0f);
        } else if (phase >= 3 && stormCd <= 0 && GetRandomValue(0, 99) < 42) {
            state = MState::Storm;               // the sky fills with crescents
            stateTimer = 2.4f;
            subStep = 0; tickT = 0;
            stormCd = frnd(6.0f, 8.5f);
        } else if (dist < 100 && GetRandomValue(0, 99) < 60) {
            state = MState::Combo;               // point-blank: draw and cut
            stateTimer = 0.5f;
            comboHit = 0; subT = 0.18f;
        } else {
            if (dist > 300) state = roll < 46 ? MState::TeleA
                                  : roll < 74 ? MState::TeleB : MState::TeleC;
            else            state = roll < 42 ? MState::TeleA
                                  : roll < 66 ? MState::TeleB : MState::TeleC;
            switch (state) {
                case MState::TeleA: stateTimer = 0.34f; break;
                case MState::TeleB: stateTimer = 0.50f; break;   // long-slash telegraph
                default:            stateTimer = 0.30f; break;
            }
        }
    }
    // tempo tightens as they weaken; Kokushibo quickens far more, but the
    // suspense stare and the gathering storm keep their full deliberate weight.
    if (kind == MOON_DOUMA) {
        if (phase >= 2) stateTimer *= 0.75f;
    } else if (state != MState::Stare && state != MState::Storm) {
        stateTimer *= (phase >= 3 ? 0.60f : phase >= 2 ? 0.78f : 1.0f);
    }
}

void UpperMoon::Update(float dt, Player& player, Giyu* ally, Shinobu* shinobu,
                       Rengoku* rengoku, Gyomei* gyomei, Tengen* tengen, Sanemi* sanemi,
                       CombatSystem& cs, Effects& fx) {
    if (!active || state == MState::Dead) return;

    hitFlash  = fmaxf(hitFlash - dt, 0);
    slowTimer = fmaxf(slowTimer - dt, 0);
    guardBroken = fmaxf(guardBroken - dt, 0);
    openingCd = fmaxf(openingCd - dt, 0);
    ghostA += (1.0f - ghostA) * Clampf(6.0f * dt, 0, 1);
    // Kokushibo's ever-present menace + escalation timers
    auraPulse += dt;
    walkBob += dt;
    declareT = fmaxf(declareT - dt, 0);
    stareCd = fmaxf(stareCd - dt, 0);
    stormCd = fmaxf(stormCd - dt, 0);
    if (state != MState::Stare) stareGlare = fmaxf(stareGlare - 3.0f * dt, 0);

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

    int newPhase;
    if (kind == MOON_KOKU)
        newPhase = hp > maxHp * 0.66f ? 1 : hp > maxHp * 0.33f ? 2 : 3;
    else
        newPhase = hp > maxHp * 0.40f ? 1 : 2;

    if (newPhase != phase && state != MState::Dying && state != MState::Desperation) {
        int wasPhase = phase;
        phase = newPhase;
        if (kind == MOON_KOKU) {
            if (newPhase >= 3 && !transformed) {
                // the transformation: the strongest swordsman refuses the grave
                transformed = true;
                state = MState::Desperation;
                stateTimer = 3.6f;
                subStep = 0; subT = 1.1f;      // hold the declaration, then erupt
                tickT = 0;
                vel.x = 0;
                slashArmed = slashArmed2 = false;
                shards.clear();
                declareT = 2.8f;               // the cinematic banner
                fx.AddShake(1.0f);
                fx.AddHitstop(0.35f);
                fx.Flash(C(18, 3, 32), 0.92f); // the light drains from the arena
                fx.Ring(pos, 20, 520, 900, 14, MoonCol(kind));
                PlaySfx(SFX_ROAR, 1.0f, 0.4f);
            } else if (newPhase == 2 && wasPhase < 2) {
                // the blade quickens — a cold flourish, no pause in the assault
                fx.AddShake(0.5f);
                fx.Flash(C(30, 8, 46), 0.5f);
                fx.Ring(pos, 16, 360, 760, 10, MoonCol(kind));
                fx.Text({ pos.x, pos.y - h - 14 }, C(215, 165, 248), 1.6f,
                        "THE  BLADE  QUICKENS");
                PlaySfx(SFX_SLASH, 0.9f, 0.5f);
            }
        } else {
            fx.AddShake(0.55f);
            fx.Ring(pos, 18, 320, 720, 10, MoonCol(kind));
            fx.Text({ pos.x, pos.y - h - 10 }, MoonCol(kind), 1.4f, "DOUMA STOPS SMILING");
            PlaySfx(SFX_ROAR, 0.9f, 1.05f);
        }
    }

    float spd;
    if (kind == MOON_KOKU)
        spd = KokuWalk(phase) * (slowTimer > 0 ? 0.5f : 1.0f);
    else
        spd = (phase >= 2 ? MoonSpd(kind) * 1.15f : MoonSpd(kind))
            * (slowTimer > 0 ? 0.55f : 1.0f);
    vulnerable = (state == MState::Recover);

    bool huntingAlly = preyAlly && ally && ally->Active();
    bool huntingShinobu = preyShinobu && shinobu && shinobu->Active();
    bool huntingRengoku = preyRengoku && rengoku && rengoku->Active();
    bool huntingGyomei = preyGyomei && gyomei && gyomei->Active();
    bool huntingTengen = preyTengen && tengen && tengen->Active();
    bool huntingSanemi = preySanemi && sanemi && sanemi->Active();
    Vector2 prey = huntingAlly ? ally->pos
                 : (huntingShinobu ? shinobu->pos
                 : (huntingRengoku ? rengoku->pos
                 : (huntingGyomei ? gyomei->pos
                 : (huntingTengen ? tengen->pos
                 : (huntingSanemi ? sanemi->pos : player.pos)))));

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
                if (decideTimer <= 0) ChooseAttack(player, ally, shinobu, rengoku, gyomei, tengen, sanemi);
            }
            break;
        }
        case MState::TeleA: {
            vel.x = 0;
            stateTimer -= dt;
            facing = prey.x > pos.x ? 1 : -1;
            if (kind == MOON_KOKU) {
                fx.MoonWind({ pos.x, pos.y - 14 }, facing, MoonCol(kind), 0.4f);  // gather
            }
            if (stateTimer <= 0) {
                state = MState::AtkA;
                volley = 0;
                tickT = 0;
                stateTimer = kind == MOON_DOUMA ? 0.6f : 2.4f;   // koku: volley-driven
            }
            break;
        }
        case MState::AtkA: {
            stateTimer -= dt;
            tickT -= dt;
            vel.x = 0;
            if (kind == MOON_DOUMA) {
                int volleys = phase >= 2 ? 3 : 2;
                if (tickT <= 0 && volley < volleys) {
                    tickT = 0.22f;
                    volley++;
                    int n = phase >= 2 ? 8 : 6;
                    Vector2 origin = { pos.x + facing * 26.0f, pos.y - 14 };
                    float baseAng = atan2f(prey.y - origin.y, prey.x - origin.x);
                    for (int i = 0; i < n; i++) {
                        float a = baseAng + (i - (n - 1) * 0.5f) * 0.12f + frnd(-0.04f, 0.04f);
                        Shard s;
                        s.pos = origin;
                        s.vel = { cosf(a) * 500.0f, sinf(a) * 500.0f };
                        shards.push_back(s);
                    }
                    fx.Sparks(origin, facing > 0 ? 0.0f : 180.0f, 60, 8, MoonCol(kind), 380, 2.5f);
                    PlaySfx(SFX_SLASH, 0.6f, 1.3f);
                }
                if (stateTimer <= 0) EnterRecover(phase >= 2 ? 0.65f : 0.85f);
            } else {
                // Kokushibo — layered moon crescents that fill the field. Fans
                // arrive at shifting heights and cadence; phase 2+ adds a second
                // curtain from overhead whose gaps never line up with the first,
                // and a final delayed volley snaps to where you fled.
                int volleys = phase >= 3 ? 6 : phase >= 2 ? 5 : 4;
                if (tickT <= 0 && volley < volleys) {
                    volley++;
                    bool punish = (volley == volleys);
                    // unpredictable cadence: quick bursts, then a held beat
                    tickT = punish ? 0.44f : (volley % 3 == 0 ? 0.34f : 0.14f);
                    int n = phase >= 3 ? 9 : phase >= 2 ? 7 : 6;
                    float speed = 545.0f;
                    Vector2 origin = { pos.x + facing * 26.0f, pos.y - 14 - frnd(0, 34) };
                    float baseAng = atan2f(prey.y - origin.y, prey.x - origin.x);
                    float spread = punish ? 0.13f : 0.30f;   // punish = a focused spear
                    for (int i = 0; i < n; i++) {
                        float a = baseAng + (i - (n - 1) * 0.5f) * spread + frnd(-0.03f, 0.03f);
                        Shard s;
                        s.pos = origin; s.spin = frnd(0, 360);
                        s.vel = { cosf(a) * speed, sinf(a) * speed };
                        if ((int)shards.size() < 520) shards.push_back(s);
                    }
                    // phase 2+: an overhead curtain, offset so the seams stagger
                    if (phase >= 2 && !punish) {
                        Vector2 o2 = { Clampf(prey.x - facing * 170.0f, 40.0f,
                                             (float)cfg::SCREEN_W - 40.0f), -20.0f };
                        int m = phase >= 3 ? 7 : 5;
                        float base2 = atan2f(prey.y - o2.y, prey.x - o2.x);
                        for (int i = 0; i < m; i++) {
                            float a = base2 + (i - (m - 1) * 0.5f) * 0.22f + frnd(-0.03f, 0.03f);
                            Shard s;
                            s.pos = o2; s.spin = frnd(0, 360);
                            s.vel = { cosf(a) * (speed * 0.9f), sinf(a) * (speed * 0.9f) };
                            if ((int)shards.size() < 520) shards.push_back(s);
                        }
                    }
                    fx.Sparks(origin, facing > 0 ? 0.0f : 180.0f, 60, 10, MoonCol(kind), 420, 2.8f);
                    fx.MoonWind({ pos.x + facing * 20.0f, pos.y - 14 }, facing, MoonCol(kind), 0.8f);
                    if (punish) { fx.AddShake(0.25f); fx.Flash(C(24, 6, 40), 0.26f); }
                    PlaySfx(SFX_SLASH, 0.7f, punish ? 0.5f : 0.72f);
                }
                if ((volley >= volleys && tickT <= 0) || stateTimer <= 0)
                    EnterRecover(phase >= 3 ? 0.5f : phase >= 2 ? 0.6f : 0.72f);
            }
            break;
        }
        case MState::TeleB: {
            vel.x = 0;
            stateTimer -= dt;
            facing = prey.x > pos.x ? 1 : -1;
            if (kind == MOON_KOKU) {
                // telegraph the arena-wide chest cut — CROUCH beneath it
                slashBand = { facing > 0 ? pos.x + 20 : pos.x - 20 - 720,
                              cfg::GROUND_Y - 100, 720, 58 };
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
                    // begin the chained long-slash: beat 0 (chest) is armed
                    state = MState::AtkB;
                    stateTimer = 3.0f;         // safety cap; beats drive the exit
                    subStep = 0;
                    subT = 0.0f;               // TeleB already telegraphed beat 0
                    slashArmed = true;
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
                        if (rengoku && rengoku->Active() &&
                            Dist(rengoku->pos, { lz.pos.x, lz.pos.y - 20 }) < 90) {
                            float dir = rengoku->pos.x >= lz.pos.x ? 1.0f : -1.0f;
                            rengoku->TakeDamage(20, dir * 480.0f, HitKind::BossAoe, fx);
                        }
                        if (gyomei && gyomei->Active() &&
                            Dist(gyomei->pos, { lz.pos.x, lz.pos.y - 20 }) < 90) {
                            float dir = gyomei->pos.x >= lz.pos.x ? 1.0f : -1.0f;
                            gyomei->TakeDamage(20, dir * 480.0f, HitKind::BossAoe, fx);
                        }
                        if (tengen && tengen->Active() &&
                            Dist(tengen->pos, { lz.pos.x, lz.pos.y - 20 }) < 90) {
                            float dir = tengen->pos.x >= lz.pos.x ? 1.0f : -1.0f;
                            tengen->TakeDamage(20, dir * 480.0f, HitKind::BossAoe, fx);
                        }
                        if (sanemi && sanemi->Active() &&
                            Dist(sanemi->pos, { lz.pos.x, lz.pos.y - 20 }) < 90) {
                            float dir = sanemi->pos.x >= lz.pos.x ? 1.0f : -1.0f;
                            sanemi->TakeDamage(20, dir * 480.0f, HitKind::BossAoe, fx);
                        }
                    }
                }
                if (stateTimer <= 0) {
                    lotus.clear();
                    EnterRecover(phase >= 2 ? 0.7f : 0.95f);
                }
            } else {
                // Kokushibo — the chained moon-slash. Beats alternate height:
                // even = chest (CROUCH), odd = low sweep (JUMP). Phase 3 = 3 beats.
                int beats = phase >= 3 ? 3 : phase >= 2 ? 2 : 1;
                subT -= dt;
                if (subT <= 0) {
                    if (slashArmed) {                       // the armed band cuts
                        bool low = (subStep % 2 == 1);
                        cs.Add(slashBand, low ? 26.0f : 24.0f, facing * 640.0f,
                               low ? -560.0f : -260.0f, 0.09f,
                               Team::Enemy, HitKind::BossAoe, cs.NewId());
                        fx.SlashArc({ slashBand.x + slashBand.width * 0.5f,
                                      slashBand.y + slashBand.height * 0.5f },
                                    300, facing > 0 ? -10.0f : 190.0f,
                                    facing > 0 ? 10.0f : 170.0f, MoonCol(kind));
                        fx.MoonWind({ pos.x, slashBand.y + slashBand.height * 0.5f },
                                    facing, MoonCol(kind), 1.5f);
                        fx.AddShake(0.42f);
                        fx.Flash(C(22, 5, 38), 0.34f);
                        PlaySfx(SFX_SLASH, 1.0f, low ? 0.62f : 0.5f);
                        slashArmed = false;
                        subStep++;
                        subT = 0.16f;
                    } else if (subStep < beats) {           // arm the next height
                        bool low = (subStep % 2 == 1);
                        float bw = 720.0f;
                        slashBand = low
                            ? Rectangle{ facing > 0 ? pos.x - 40 : pos.x + 40 - bw,
                                         cfg::GROUND_Y - 40, bw, 40 }
                            : Rectangle{ facing > 0 ? pos.x + 20 : pos.x - 20 - bw,
                                         cfg::GROUND_Y - 100, bw, 58 };
                        slashArmed = true;
                        subT = 0.26f;                        // reaction window
                        PlaySfx(SFX_WHOOSH, 0.5f, 0.7f);
                    } else {
                        EnterRecover(phase >= 3 ? 0.55f : phase >= 2 ? 0.66f : 0.85f);
                    }
                }
                if (stateTimer <= 0) EnterRecover(0.6f);     // safety
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
                    if (rengoku && rengoku->Active() && CheckCollisionRecs(cone, rengoku->Rect()))
                        rengoku->TakeDamage(6, facing * 140.0f, HitKind::BossProjectile, fx);
                    if (gyomei && gyomei->Active() && CheckCollisionRecs(cone, gyomei->Rect()))
                        gyomei->TakeDamage(6, facing * 140.0f, HitKind::BossProjectile, fx);
                    if (tengen && tengen->Active() && CheckCollisionRecs(cone, tengen->Rect()))
                        tengen->TakeDamage(6, facing * 140.0f, HitKind::BossProjectile, fx);
                    if (sanemi && sanemi->Active() && CheckCollisionRecs(cone, sanemi->Rect()))
                        sanemi->TakeDamage(6, facing * 140.0f, HitKind::BossProjectile, fx);
                }
                if (stateTimer <= 0) EnterRecover(phase >= 2 ? 0.7f : 0.95f);
            } else {
                if (stateTimer <= 0) {
                    // flash cross: he is simply... there, and the cut lands heavy
                    Rectangle r = { facing > 0 ? pos.x : pos.x - 150, pos.y - 66, 150, 128 };
                    cs.Add(r, 28, facing * 700.0f, -420, 0.07f,
                           Team::Enemy, HitKind::BossDash, cs.NewId());
                    float a0 = facing > 0 ? -80.0f : 260.0f;
                    float a1 = facing > 0 ?  80.0f : 100.0f;
                    fx.SlashArc(pos, 130, a0, a1, MoonCol(kind));
                    fx.MoonWind({ pos.x + facing * 20.0f, pos.y - 8 }, facing, MoonCol(kind), 1.1f);
                    fx.AddShake(0.32f);
                    fx.Flash(C(20, 5, 36), 0.3f);
                    PlaySfx(SFX_SLASH, 1.0f, 0.52f);
                    EnterRecover(phase >= 3 ? 0.48f : phase >= 2 ? 0.58f : 0.8f);
                }
            }
            break;
        }
        case MState::Stare: {
            // the silent glare: motionless, six eyes fixed on you. Held a beat
            // too long to bait a panic dodge — the *vanish* is your only warning.
            vel.x = 0;
            stateTimer -= dt;
            facing = prey.x > pos.x ? 1 : -1;
            stareGlare = fminf(stareGlare + 4.0f * dt, 1.0f);
            if (GetRandomValue(0, 3) == 0)
                fx.Ember({ pos.x + frnd(-30, 30), pos.y + frnd(-44, 18) });
            if (stateTimer <= 0) {
                // vanish, and reappear at your side as a barely-there ghost
                fx.DeathBurst(pos, MoonCol(kind), 1.0f);
                fx.Ring(pos, 8, 220, 720, 8, MoonCol(kind));
                float side = (prey.x > cfg::SCREEN_W * 0.5f) ? -1.0f : 1.0f;
                if (GetRandomValue(0, 2) == 0) side = -side;
                pos.x = Clampf(prey.x + side * 96.0f, 54.0f, (float)cfg::SCREEN_W - 54.0f);
                pos.y = cfg::GROUND_Y - h * 0.5f;
                facing = prey.x > pos.x ? 1 : -1;
                ghostA = 0.08f;
                fx.AddHitstop(0.05f);
                fx.Flash(C(16, 3, 30), 0.5f);
                PlaySfx(SFX_WHOOSH, 1.0f, 0.5f);
                state = MState::AtkFlash;
                stateTimer = 0.15f;               // an incredibly fast draw
            }
            break;
        }
        case MState::AtkFlash: {
            vel.x = 0;
            stateTimer -= dt;
            ghostA = fminf(ghostA + 8.0f * dt, 1.0f);
            if (stateTimer <= 0) {
                Rectangle r = { facing > 0 ? pos.x - 10 : pos.x - 150, pos.y - 70, 160, 138 };
                cs.Add(r, 34, facing * 760.0f, -440, 0.08f,
                       Team::Enemy, HitKind::BossDash, cs.NewId());
                fx.SlashArc(pos, 150, facing > 0 ? -85.0f : 265.0f,
                            facing > 0 ? 85.0f : 95.0f, MoonCol(kind));
                fx.MoonWind({ pos.x, pos.y - 8 }, facing, MoonCol(kind), 1.8f);
                fx.AddShake(0.6f);
                fx.AddHitstop(0.06f);
                fx.Flash(C(26, 6, 44), 0.5f);
                fx.Ring(pos, 10, 180, 620, 8, C(230, 200, 250));
                PlaySfx(SFX_SLASH, 1.0f, 0.42f);
                EnterRecover(phase >= 3 ? 0.5f : 0.66f);
            }
            break;
        }
        case MState::Combo: {
            // Moon-Dragon: a precise, emotionless close-range sword chain.
            vel.x = 0;
            facing = prey.x > pos.x ? 1 : -1;
            subT -= dt;
            int hits = phase >= 3 ? 4 : 3;
            if (subT <= 0 && comboHit < hits) {
                bool last = (comboHit == hits - 1);
                Rectangle r = { facing > 0 ? pos.x : pos.x - 122, pos.y - 54, 122, 108 };
                cs.Add(r, last ? 20.0f : 12.0f, facing * (last ? 620.0f : 240.0f),
                       last ? -360.0f : -120.0f, 0.07f, Team::Enemy,
                       last ? HitKind::BossDash : HitKind::BossAoe, cs.NewId());
                float a0 = facing > 0 ? -60.0f : 240.0f;
                float a1 = facing > 0 ?  60.0f : 120.0f;
                fx.SlashArc({ pos.x + facing * 30.0f, pos.y - 6 }, 90.0f + comboHit * 8,
                            a0, a1, MoonCol(kind));
                fx.MoonWind({ pos.x + facing * 30.0f, pos.y - 6 }, facing, MoonCol(kind), 0.7f);
                if (last) { fx.AddShake(0.4f); fx.Flash(C(22, 5, 38), 0.3f); }
                PlaySfx(SFX_SLASH, 0.85f, 0.6f + comboHit * 0.05f);
                comboHit++;
                subT = last ? 0.2f : 0.14f;
            }
            if (comboHit >= hits && subT <= 0) {
                comboHit = 0;
                EnterRecover(phase >= 3 ? 0.5f : 0.68f);
            }
            break;
        }
        case MState::Storm: {
            // Phase 3 — Tenman Crescent Moons: the sky and earth answer him.
            // A repeatable, shorter cousin of the transformation.
            stateTimer -= dt;
            vel.x = 0;
            if (subStep == 0) {                    // one-shot gather flourish
                subStep = 1;
                fx.AddShake(0.5f);
                fx.Flash(C(24, 6, 42), 0.45f);
                fx.Ring(pos, 14, 460, 860, 12, MoonCol(kind));
                PlaySfx(SFX_ROAR, 0.8f, 0.5f);
            }
            tickT -= dt;
            if (tickT <= 0) {
                tickT = 0.08f;
                for (int i = 0; i < 4; i++) {       // rain from above
                    if ((int)shards.size() >= 520) break;
                    Shard s;
                    float sx = frnd(20.0f, cfg::SCREEN_W - 20.0f);
                    s.pos = { sx, -20.0f };
                    float a = frnd(60.0f, 120.0f) * DEG2RAD;
                    float sp = frnd(360.0f, 560.0f);
                    s.vel = { cosf(a) * sp, sinf(a) * sp };
                    s.spin = frnd(0, 360);
                    shards.push_back(s);
                }
                for (int i = 0; i < 3; i++) {       // erupt from the earth
                    if ((int)shards.size() >= 520) break;
                    Shard s;
                    float sx = frnd(20.0f, cfg::SCREEN_W - 20.0f);
                    s.pos = { sx, cfg::GROUND_Y - 8.0f };
                    float a = frnd(-150.0f, -30.0f) * DEG2RAD;
                    float sp = frnd(340.0f, 600.0f);
                    s.vel = { cosf(a) * sp, sinf(a) * sp };
                    s.spin = frnd(0, 360);
                    shards.push_back(s);
                    if (GetRandomValue(0, 2) == 0)
                        fx.Sparks({ sx, cfg::GROUND_Y - 4 }, -90, 50, 3, MoonCol(kind), 300, 2.4f);
                }
            }
            if (fmodf(stateTimer, 0.4f) < 0.02f) {
                fx.AddShake(0.28f);
                fx.Flash(C(20, 5, 36), 0.16f);
                PlaySfx(SFX_SLASH, 0.5f, 0.55f);
            }
            if (stateTimer <= 0) EnterRecover(0.85f);
            break;
        }
        case MState::Desperation: {
            // "I WILL NOT DIE" — the transformation. A held declaration, then an
            // enormous eruption of moon crescents, then the arena drowns in them.
            stateTimer -= dt;
            vel.x = 0;
            subT -= dt;
            if (subStep == 0) {
                if (GetRandomValue(0, 1) == 0)
                    fx.Ember({ pos.x + frnd(-42, 42), pos.y + frnd(-72, 10) });
                if (fmodf(stateTimer, 0.16f) < 0.02f) fx.AddShake(0.28f);
                if (subT <= 0) {                    // the eruption
                    subStep = 1;
                    for (int i = 0; i < 150; i++) {
                        if ((int)shards.size() >= 520) break;
                        float a = frnd(0.0f, 360.0f) * DEG2RAD;
                        float sp = frnd(280.0f, 720.0f);
                        Shard s;
                        s.pos = { pos.x, pos.y - 10 };
                        s.vel = { cosf(a) * sp, sinf(a) * sp };
                        s.spin = frnd(0, 360);
                        shards.push_back(s);
                    }
                    fx.AddShake(1.0f);
                    fx.AddHitstop(0.28f);
                    fx.Flash(C(30, 6, 50), 0.88f);
                    fx.Ring(pos, 16, 640, 1000, 18, MoonCol(kind));
                    fx.Ring(pos, 10, 440, 780, 10, C(235, 205, 255));
                    fx.DeathBurst(pos, MoonCol(kind), 2.4f);
                    PlaySfx(SFX_EXPLO, 1.0f, 0.6f);
                    PlaySfx(SFX_ROAR, 1.0f, 0.45f);
                }
            } else {
                tickT -= dt;
                if (tickT <= 0) {
                    tickT = 0.05f;
                    for (int i = 0; i < 5; i++) {
                        if ((int)shards.size() >= 520) break;
                        Shard s;
                        float sx = frnd(20.0f, cfg::SCREEN_W - 20.0f);
                        bool fromSky = GetRandomValue(0, 1) == 0;
                        s.pos = { sx, fromSky ? -18.0f : cfg::GROUND_Y - 10.0f };
                        float a = fromSky ? frnd(60.0f, 120.0f) * DEG2RAD
                                          : frnd(-150.0f, -30.0f) * DEG2RAD;
                        float sp = frnd(320.0f, 640.0f);
                        s.vel = { cosf(a) * sp, sinf(a) * sp };
                        s.spin = frnd(0, 360);
                        shards.push_back(s);
                    }
                }
                if (fmodf(stateTimer, 0.4f) < 0.02f) {
                    fx.AddShake(0.3f);
                    PlaySfx(SFX_SLASH, 0.5f, 0.5f);
                }
            }
            if (stateTimer <= 0) EnterRecover(1.0f);
            break;
        }
        case MState::Recover: {
            stateTimer -= dt;
            vel.x *= 1.0f - Clampf(5.0f * dt, 0, 1);
            if (stateTimer <= 0) {
                state = MState::Stalk;
                if (kind == MOON_KOKU)
                    decideTimer = frnd(0.32f, 0.66f)     // relentless: barely a breath
                                / (phase >= 3 ? 1.5f : phase >= 2 ? 1.25f : 1.0f);
                else
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
        float shardDmg;
        if (kind == MOON_DOUMA) shardDmg = 23.0f;
        else shardDmg = (state == MState::Desperation) ? 24.0f
                      : (state == MState::Storm)        ? 22.0f
                      : KokuCrescentDmg(phase);
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
        if (s.alive && rengoku && rengoku->Active() &&
            CheckCollisionCircleRec(s.pos, 12, rengoku->Rect())) {
            rengoku->TakeDamage(shardDmg, (s.vel.x > 0 ? 1 : -1) * 320.0f,
                                HitKind::BossProjectile, fx);
            s.alive = false;
        }
        if (s.alive && gyomei && gyomei->Active() &&
            CheckCollisionCircleRec(s.pos, 12, gyomei->Rect())) {
            gyomei->TakeDamage(shardDmg, (s.vel.x > 0 ? 1 : -1) * 320.0f,
                               HitKind::BossProjectile, fx);
            s.alive = false;
        }
        if (s.alive && tengen && tengen->Active() &&
            CheckCollisionCircleRec(s.pos, 12, tengen->Rect())) {
            tengen->TakeDamage(shardDmg, (s.vel.x > 0 ? 1 : -1) * 320.0f,
                               HitKind::BossProjectile, fx);
            s.alive = false;
        }
        if (s.alive && sanemi && sanemi->Active() &&
            CheckCollisionCircleRec(s.pos, 12, sanemi->Rect())) {
            sanemi->TakeDamage(shardDmg, (s.vel.x > 0 ? 1 : -1) * 320.0f,
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
    if (kindHit == HitKind::Rengoku && vulnerable) mult *= 1.2f;
    if (kindHit == HitKind::Water) slowTimer = 1.4f;
    if (kindHit == HitKind::Serpent) poisonT = 3.0f;
    if (kindHit == HitKind::Giyu) mult *= 0.3f;   // Hashira alone cannot fell an Upper Moon
    if (kindHit == HitKind::Shinobu) {
        mult *= 0.25f;
        poisonT = fmaxf(poisonT, 4.5f);
    }
    if (kindHit == HitKind::Rengoku) mult *= 0.36f;
    if (kindHit == HitKind::Gyomei) {
        mult *= 0.44f;
        if (guardBroken <= 0) {
            guardBroken = 3.4f;
            fx.Text({ pos.x, pos.y - h * 0.5f - 34 }, C(210, 200, 185), 1.2f,
                    "STONE HASHIRA BREAKS GUARD");
            fx.Ring(pos, 18, 145, 500, 8, C(210, 200, 185));
        }
    }
    if (kindHit == HitKind::Tengen) mult *= 0.34f;
    if (kindHit == HitKind::Sanemi) {
        mult *= 0.36f;
        slowTimer = fmaxf(slowTimer, 0.35f);
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
    if (kindHit == HitKind::Rengoku) tcol = C(255, 150, 55);
    if (kindHit == HitKind::Gyomei)  tcol = C(188, 178, 158);
    if (kindHit == HitKind::Tengen)  tcol = C(255, 212, 88);
    if (kindHit == HitKind::Sanemi)  tcol = C(205, 245, 226);
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
        declareT = 0;
        if (kind == MOON_KOKU) SetBossDrone(0);   // the dread lifts as he falls
        fx.AddShake(0.7f);
        fx.AddHitstop(0.4f);
    }
}

bool UpperMoon::ForceOpening(Effects& fx) {
    if (!active || openingCd > 0) return false;
    // an opening can be forced while he winds up or stares you down — but not
    // mid-combo, mid-flash, mid-storm, or through the transformation.
    bool interruptible =
        state == MState::Stalk || state == MState::TeleA ||
        state == MState::TeleB || state == MState::TeleC ||
        state == MState::Stare;
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

int UpperMoon::NullifyShardsInRect(Rectangle r) {
    int cut = 0;
    for (auto& s : shards) {
        if (s.alive && CheckCollisionPointRec(s.pos, r)) {
            s.alive = false;
            cut++;
        }
    }
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
    if (state == MState::AtkA || state == MState::AtkB || state == MState::AtkC ||
        state == MState::AtkFlash || state == MState::Combo ||
        state == MState::Storm || state == MState::Desperation)
        return true;
    return ShardsNear(a, 260) >= 2 || ShardsNear(b, 220) >= 2;
}

// ---------------------------------------------------------------- drawing

void UpperMoon::Draw() const {
    if (!active || state == MState::Dead) return;
    float gt = (float)GetTime();
    Color mc = MoonCol(kind);
    float bodyA = kind == MOON_KOKU ? ghostA : 1.0f;

    // Kokushibo carries his own gravity — an ever-present pall of moonlight and
    // dread, deepening with each phase. It hangs on him even as he merely walks.
    if (kind == MOON_KOKU) {
        float ph = phase >= 3 ? 1.0f : phase >= 2 ? 0.7f : 0.45f;
        float breathe = 0.5f + 0.5f * sinf(auraPulse * 2.2f);
        DrawEllipse((int)pos.x, (int)(cfg::GROUND_Y + 4), (int)(60 + 20 * ph), 14,
                    Fade(C(18, 4, 30), 0.5f * bodyA));
        float glowR = 72 + 34 * ph + 8 * breathe;
        DrawCircleGradient((int)pos.x, (int)(pos.y - 6), glowR,
                           Fade(mc, (0.09f + 0.11f * ph) * (0.6f + 0.4f * breathe) * bodyA), BLANK);
        // slow crescent motes orbiting him — moonlight that never settles
        int motes = 3 + (int)(ph * 4);
        for (int i = 0; i < motes; i++) {
            float a = auraPulse * (0.6f + 0.14f * i) + i * (6.2831853f / motes);
            float rr = 44 + 16 * sinf(auraPulse * 1.3f + i);
            Vector2 mp = { pos.x + cosf(a) * rr, pos.y - 10 + sinf(a) * rr * 0.5f };
            float ma = atan2f(-sinf(a), -cosf(a)) * RAD2DEG;
            DrawRing(mp, 4, 8, ma - 70, ma + 70, 10, Fade(mc, (0.35f + 0.3f * ph) * bodyA));
        }
    }

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

    // the long-slash warning band — the seam the moon-blade will pass through
    if (kind == MOON_KOKU && slashArmed) {
        float pulse = 0.16f + 0.14f * sinf(gt * 26.0f);
        DrawRectangleRec(slashBand, Fade(mc, pulse));
        DrawRectangle((int)slashBand.x, (int)(slashBand.y + slashBand.height * 0.5f - 2),
                      (int)slashBand.width, 4, Fade(C(255, 240, 255), pulse + 0.2f));
        DrawRectangleLinesEx(slashBand, 1.5f, Fade(mc, pulse + 0.1f));
    }
    // the flash-slash gathers for an instant before it lands
    if (kind == MOON_KOKU && state == MState::AtkFlash) {
        Rectangle r = { facing > 0 ? pos.x - 10 : pos.x - 150, pos.y - 70, 160, 138 };
        DrawRectangleRec(r, Fade(mc, 0.12f + 0.12f * sinf(gt * 40.0f)));
    }

    // shadow
    DrawEllipse((int)pos.x, (int)(cfg::GROUND_Y + 6), 24, 7, Fade(BLACK, 0.4f * bodyA));

    float lean = 0;
    float hover = kind == MOON_DOUMA ? sinf(gt * 3.0f) * 4.0f
                : (state == MState::Stalk ? sinf(walkBob * 7.0f) * 1.6f : 0.0f); // slow stride
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
        // six eyes: three crimson pairs, ever-watchful — they burn while he
        // stares you down, and smoulder even at rest
        float eyeGlow = 0.35f + 0.65f * stareGlare + (phase >= 3 ? 0.2f : 0.0f);
        float eyePulse = 0.7f + 0.3f * sinf(gt * 6.0f);
        for (int i = 0; i < 3; i++) {
            float ey = headC.y - 3 + i * 4.0f;
            Vector2 e1 = { headC.x + facing * 5.0f, ey };
            Vector2 e2 = { headC.x + facing * 1.0f, ey };
            DrawCircleV(e1, 3.6f, Fade(C(255, 40, 40), 0.4f * eyeGlow * eyePulse * bodyA));
            DrawCircleV(e2, 3.6f, Fade(C(255, 40, 40), 0.4f * eyeGlow * eyePulse * bodyA));
            DrawCircleV(e1, 1.7f, Fade(C(255, 95, 95), bodyA));
            DrawCircleV(e2, 1.7f, Fade(C(255, 95, 95), bodyA));
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
            // glowing purple moon crescent, with a soft halo
            float angDeg = atan2f(s.vel.y, s.vel.x) * RAD2DEG;
            DrawRing(s.pos, 6, 16, angDeg - 106, angDeg + 106, 18, Fade(mc, 0.32f));
            DrawRing(s.pos, 7, 13, angDeg - 100, angDeg + 100, 16, mc);
            DrawRing(s.pos, 9, 11, angDeg - 80, angDeg + 80, 12, C(235, 205, 255));
        }
    }
}

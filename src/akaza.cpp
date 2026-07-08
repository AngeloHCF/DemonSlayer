#include "akaza.h"
#include "player.h"
#include "companion.h"
#include "effects.h"
#include "config.h"
#include "audio.h"
#include <cmath>
#include <algorithm>

static const float AK_HP    = 900.0f;
static const float AK_SPEED = 300.0f;
static const float WAVE_SPD = 420.0f;

void Akaza::Reset() {
    active = false;
    state = AkState::Inactive;
    pos = {0, 0}; vel = {0, 0};
    facing = -1;
    hp = maxHp = AK_HP;
    phase = 1;
    vulnerable = false;
    guardBroken = 0;
    hitMem.Clear();
    stateTimer = 0; decideTimer = 0;
    comboHits = 0; comboTick = 0;
    dashAttackId = -1;
    slowTimer = 0; openingCd = 0;
    poisonT = 0; poisonTick = 0;
    hitFlash = 0;
    preyAlly = false;
    leapVx = 0;
    orbs.clear();
    waves.clear();
}

void Akaza::Activate(Vector2 p) {
    Reset();
    active = true;
    pos = { p.x, -90.0f };            // he falls out of the night sky
    state = AkState::Intro;
    stateTimer = 1.8f;
    vel = { 0, 500.0f };
    PlaySfx(SFX_WHOOSH, 0.9f, 0.5f);
}

Rectangle Akaza::Rect() const {
    return { pos.x - w * 0.5f, pos.y - h * 0.5f, w, h };
}

void Akaza::EnterRecover(float t) {
    state = AkState::Recover;
    stateTimer = t;
}

void Akaza::ChooseAttack(const Player& player, const Giyu* ally) {
    // he seeks the strong — and a Hashira is a feast
    preyAlly = ally && ally->Active() && GetRandomValue(0, 99) < 45;
    Vector2 prey = preyAlly ? ally->pos : player.pos;
    float dist = fabsf(prey.x - pos.x);
    int roll = GetRandomValue(0, 99);

    if (dist > 300) {
        if      (roll < 40) state = AkState::TeleDash;
        else if (roll < 70) state = AkState::TeleLeap;
        else                state = AkState::TeleShock;
    } else {
        if      (roll < 45) state = AkState::TeleCombo;
        else if (roll < 70) state = AkState::TeleShock;
        else                state = AkState::TeleDash;
    }

    switch (state) {
        case AkState::TeleCombo: stateTimer = 0.32f; break;
        case AkState::TeleDash:  stateTimer = 0.40f; break;
        case AkState::TeleShock: stateTimer = 0.50f; break;
        case AkState::TeleLeap:  stateTimer = 0.45f; break;
        default: break;
    }
    if (phase >= 2) stateTimer *= 0.72f;    // he rejoices in battle
}

void Akaza::Update(float dt, Player& player, Giyu* ally, CombatSystem& cs, Effects& fx) {
    if (!active || state == AkState::Dead) return;

    hitFlash  = fmaxf(hitFlash - dt, 0);
    slowTimer = fmaxf(slowTimer - dt, 0);
    guardBroken = fmaxf(guardBroken - dt, 0);
    openingCd = fmaxf(openingCd - dt, 0);

    // serpent venom (cannot finish an Upper Moon)
    if (poisonT > 0 && state != AkState::Dying) {
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
    if (newPhase != phase && state != AkState::Dying && state != AkState::Desperation) {
        phase = newPhase;
        // he cannot sense his opponent - so he will simply destroy EVERYTHING
        state = AkState::Desperation;
        stateTimer = 15.0f;
        comboTick = 0;
        despSpin = frnd(0, 360);
        vel.x = 0;
        fx.AddShake(0.85f);
        fx.AddHitstop(0.2f);
        fx.Ring(pos, 18, 380, 800, 10, C(90, 150, 255));
        fx.Text({ pos.x, pos.y - h - 26 }, C(140, 190, 255), 1.6f,
                "AKAZA CAN NO LONGER SENSE YOU");
        PlaySfx(SFX_ROAR, 1.0f, 0.95f);
    }

    float spd = (phase >= 2 ? AK_SPEED * 1.15f : AK_SPEED) * (slowTimer > 0 ? 0.55f : 1.0f);
    vulnerable = (state == AkState::Recover);

    bool huntingAlly = preyAlly && ally && ally->Active();
    Vector2 prey = huntingAlly ? ally->pos : player.pos;

    switch (state) {
        case AkState::Intro: {
            stateTimer -= dt;
            if (pos.y < cfg::GROUND_Y - h * 0.5f - 2) {
                vel.y += cfg::GRAVITY * 2.2f * dt;          // falling like a meteor
            } else if (vel.y > 0) {
                // impact: a crater greets Upper Moon Three
                vel.y = 0;
                fx.StoneSlam({ pos.x, cfg::GROUND_Y });
                fx.Ring({ pos.x, cfg::GROUND_Y - 10 }, 14, 240, 640, 10, C(120, 170, 255));
                fx.AddShake(0.8f);
                fx.AddHitstop(0.12f);
                PlaySfx(SFX_STONE, 1.0f, 0.8f);
                PlaySfx(SFX_ROAR, 0.9f, 1.1f);
            }
            if (stateTimer <= 0) { state = AkState::Stalk; decideTimer = 1.0f; }
            break;
        }
        case AkState::Stalk: {
            float dx = prey.x - pos.x;
            facing = dx > 0 ? 1 : -1;
            if (player.hiddenT > 0 && !huntingAlly) {
                vel.x *= 1.0f - Clampf(4.0f * dt, 0, 1);    // where did you go...
            } else {
                if (fabsf(dx) > 120) vel.x = facing * spd;
                else vel.x *= 1.0f - Clampf(7.0f * dt, 0, 1);
                decideTimer -= dt;
                if (decideTimer <= 0) ChooseAttack(player, ally);
            }
            break;
        }
        case AkState::TeleCombo: {
            vel.x = 0;
            stateTimer -= dt;
            facing = prey.x > pos.x ? 1 : -1;
            if (stateTimer <= 0) {
                state = AkState::Combo;
                comboHits = 0;
                comboTick = 0;
                stateTimer = (phase >= 2 ? 6 : 4) * 0.12f + 0.1f;
            }
            break;
        }
        case AkState::Combo: {
            stateTimer -= dt;
            comboTick -= dt;
            int maxHits = phase >= 2 ? 6 : 4;
            if (comboTick <= 0 && comboHits < maxHits) {
                comboTick = 0.12f;
                comboHits++;
                vel.x = facing * 480.0f;                    // stepping into each blow
                Rectangle r = {
                    facing > 0 ? pos.x + 2 : pos.x - 2 - 88,
                    pos.y - 40, 88, 80
                };
                cs.Add(r, 12, facing * 340.0f, -160, 0.05f,
                       Team::Enemy, HitKind::BossProjectile, cs.NewId());
                fx.Sparks({ pos.x + facing * 46.0f, pos.y - 10 + frnd(-18, 18) },
                          facing > 0 ? 0.0f : 180.0f, 40, 6, C(140, 190, 255), 380, 2.5f);
                PlaySfx(SFX_HIT, 0.5f, 1.3f + comboHits * 0.05f);
            }
            if (stateTimer <= 0) {
                vel.x = 0;
                EnterRecover(phase >= 2 ? 0.5f : 0.7f);
            }
            break;
        }
        case AkState::TeleDash: {
            vel.x = 0;
            stateTimer -= dt;
            facing = prey.x > pos.x ? 1 : -1;
            if (stateTimer <= 0) {
                state = AkState::DashBlow;
                stateTimer = 0.24f;
                dashAttackId = cs.NewId();
                fx.AddShake(0.12f);
                PlaySfx(SFX_WHOOSH, 0.8f, 1.05f);
            }
            break;
        }
        case AkState::DashBlow: {
            stateTimer -= dt;
            vel.x = facing * 1050.0f * (slowTimer > 0 ? 0.6f : 1.0f);
            fx.Sparks({ pos.x - facing * 18.0f, pos.y }, facing > 0 ? 180.0f : 0.0f,
                      25, 3, C(120, 170, 255), 300, 2.5f);
            Rectangle r = { pos.x - w * 0.6f, pos.y - h * 0.42f, w * 1.2f, h * 0.84f };
            cs.Add(r, 22, facing * 620.0f, -360, 0.03f,
                   Team::Enemy, HitKind::BossDash, dashAttackId);
            if (stateTimer <= 0) {
                vel.x = 0;
                EnterRecover(phase >= 2 ? 0.6f : 0.8f);
            }
            break;
        }
        case AkState::TeleShock: {
            vel.x = 0;
            stateTimer -= dt;
            facing = prey.x > pos.x ? 1 : -1;
            if (stateTimer <= 0) {
                state = AkState::Shockwave;
                stateTimer = 0.7f;
                waves.clear();
                waves.push_back({ pos, 24.0f, false, false });
                // air-splitting fist orbs, fanned toward his prey
                int n = phase >= 2 ? 8 : 6;
                Vector2 origin = { pos.x + facing * 26.0f, pos.y - 14 };
                float baseAng = atan2f(prey.y - origin.y, prey.x - origin.x);
                for (int i = 0; i < n; i++) {
                    float a = baseAng + (i - (n - 1) * 0.5f) * 0.13f;
                    FistOrb o;
                    o.pos = origin;
                    o.vel = { cosf(a) * 520.0f, sinf(a) * 520.0f };
                    orbs.push_back(o);
                }
                fx.Ring(pos, 16, 200, 620, 8, C(120, 170, 255));
                fx.Sparks(origin, facing > 0 ? 0.0f : 180.0f, 70, 14, C(150, 200, 255), 460, 3);
                fx.AddShake(0.3f);
                PlaySfx(SFX_EXPLO, 0.7f, 1.2f);
            }
            break;
        }
        case AkState::Shockwave: {
            stateTimer -= dt;
            for (auto& wv : waves) {
                wv.r += WAVE_SPD * dt;
                if (wv.r > 20) {
                    if (!wv.hitPlayer) {
                        float d = Dist(player.pos, wv.center);
                        if (fabsf(d - wv.r) < 28 && player.pos.y > cfg::GROUND_Y - 120) {
                            wv.hitPlayer = true;
                            float dir = player.pos.x >= wv.center.x ? 1.0f : -1.0f;
                            player.TakeDamage(18, dir * 520.0f, fx, true);
                        }
                    }
                    if (!wv.hitAlly && ally && ally->Active()) {
                        float d = Dist(ally->pos, wv.center);
                        if (fabsf(d - wv.r) < 28 && ally->pos.y > cfg::GROUND_Y - 120) {
                            wv.hitAlly = true;
                            float dir = ally->pos.x >= wv.center.x ? 1.0f : -1.0f;
                            ally->TakeDamage(18, dir * 520.0f, HitKind::BossAoe, fx);
                        }
                    }
                }
            }
            if (stateTimer <= 0) {
                waves.clear();
                EnterRecover(phase >= 2 ? 0.7f : 0.9f);
            }
            break;
        }
        case AkState::TeleLeap: {
            vel.x = 0;
            stateTimer -= dt;
            facing = prey.x > pos.x ? 1 : -1;
            if (stateTimer <= 0) {
                state = AkState::Leap;
                stateTimer = 1.4f;                          // safety timeout
                float dx = prey.x - pos.x;
                leapVx = Clampf(dx / 0.55f, -900.0f, 900.0f);
                vel.y = -760.0f;
                PlaySfx(SFX_WHOOSH, 0.8f, 0.9f);
            }
            break;
        }
        case AkState::Leap: {
            stateTimer -= dt;
            vel.x = leapVx;
            if ((vel.y >= 0 && pos.y + h * 0.5f >= cfg::GROUND_Y - 2) || stateTimer <= 0) {
                // crater
                vel.x = 0;
                Rectangle aoe = { pos.x - 95, cfg::GROUND_Y - 70, 190, 74 };
                cs.Add(aoe, 24, 0, -480, 0.06f,
                       Team::Enemy, HitKind::BossAoe, cs.NewId());
                fx.StoneSlam({ pos.x, cfg::GROUND_Y });
                fx.Ring({ pos.x, cfg::GROUND_Y - 8 }, 12, 190, 560, 9, C(120, 170, 255));
                fx.AddShake(0.55f);
                fx.AddHitstop(0.06f);
                PlaySfx(SFX_STONE, 0.9f, 0.9f);
                EnterRecover(phase >= 2 ? 0.7f : 0.9f);
            }
            break;
        }
        case AkState::Desperation: {
            // a spiral storm of air blasts in every direction, aimed at no one
            stateTimer -= dt;
            vel.x = 0;
            comboTick -= dt;
            if (comboTick <= 0) {
                comboTick = 0.055f;
                for (int i = 0; i < 3; i++) {
                    despSpin += 47.0f;
                    float a = despSpin * DEG2RAD;
                    float sp = frnd(380, 580);
                    FistOrb o;
                    o.pos = { pos.x, pos.y - 14 };
                    o.vel = { cosf(a) * sp, sinf(a) * sp };
                    orbs.push_back(o);
                }
                fx.Sparks({ pos.x, pos.y - 14 }, -90, 360, 2, C(150, 200, 255), 440, 2.5f);
            }
            if (fmodf(stateTimer, 0.6f) < 0.02f) {
                fx.AddShake(0.3f);
                fx.Ring(pos, 16, 220, 640, 7, C(120, 170, 255));
                PlaySfx(SFX_EXPLO, 0.45f, 1.4f);
            }
            if (stateTimer <= 0) EnterRecover(1.2f);   // arms trembling, spent
            break;
        }
        case AkState::Recover: {
            stateTimer -= dt;
            vel.x *= 1.0f - Clampf(5.0f * dt, 0, 1);
            if (stateTimer <= 0) {
                state = AkState::Stalk;
                decideTimer = frnd(0.5f, 1.0f) / (phase >= 2 ? 1.25f : 1.0f);
            }
            break;
        }
        case AkState::Dying: {
            stateTimer -= dt;
            vel.x = 0;
            if (fmodf(stateTimer, 0.2f) < 0.05f) {
                fx.DeathBurst({ pos.x + frnd(-w, w), pos.y + frnd(-h * 0.5f, h * 0.5f) },
                              C(120, 170, 255), 0.8f);
                fx.AddShake(0.18f);
            }
            if (stateTimer <= 0) {
                state = AkState::Dead;
                fx.DeathBurst(pos, C(150, 190, 255), 2.2f);
                fx.Ring(pos, 20, 340, 700, 10, C(150, 190, 255));
                fx.AddShake(0.9f);
                fx.AddHitstop(0.3f);
                PlaySfx(SFX_EXPLO, 0.9f, 0.9f);
                PlaySfx(SFX_ROAR, 0.9f, 1.3f);
            }
            break;
        }
        default: break;
    }

    // fist orbs fly on
    for (auto& o : orbs) {
        if (!o.alive) continue;
        o.pos.x += o.vel.x * dt;
        o.pos.y += o.vel.y * dt;
        float orbDmg = state == AkState::Desperation ? 20.0f : 10.0f;
        if (CheckCollisionCircleRec(o.pos, 11, player.Rect())) {
            if (player.TakeDamage(orbDmg, (o.vel.x > 0 ? 1 : -1) * 300.0f, fx))
                o.alive = false;
        }
        if (o.alive && ally && ally->Active() &&
            CheckCollisionCircleRec(o.pos, 11, ally->Rect())) {
            ally->TakeDamage(orbDmg, (o.vel.x > 0 ? 1 : -1) * 300.0f,
                             HitKind::BossProjectile, fx);
            o.alive = false;
        }
        if (o.pos.x < -60 || o.pos.x > cfg::SCREEN_W + 60 ||
            o.pos.y < -60 || o.pos.y > cfg::SCREEN_H + 60)
            o.alive = false;
        if (o.pos.y > cfg::GROUND_Y - 4) {
            o.alive = false;
            fx.Sparks({ o.pos.x, cfg::GROUND_Y - 4 }, -90, 110, 5, C(140, 190, 255), 240, 2.2f);
        }
    }
    orbs.erase(std::remove_if(orbs.begin(), orbs.end(),
               [](const FistOrb& o) { return !o.alive; }), orbs.end());

    // physics
    if (state != AkState::DashBlow) vel.y += cfg::GRAVITY * dt;
    else vel.y = 0;
    pos.x += vel.x * dt;
    pos.y += vel.y * dt;
    pos.x = Clampf(pos.x, 50.0f, (float)cfg::SCREEN_W - 50.0f);
    GroundClamp(pos, vel, h * 0.5f);
}

void Akaza::TakeDamage(float dmg, float kbx, HitKind kind, Effects& fx) {
    if (!active || state == AkState::Dying || state == AkState::Dead ||
        state == AkState::Intro) return;

    if (dmg <= 0) {                          // status-only field
        if (kind == HitKind::Water) slowTimer = fmaxf(slowTimer, 0.8f);
        return;
    }

    float mult = (vulnerable || guardBroken > 0) ? 1.0f : 0.65f;
    if (kind == HitKind::Fire && vulnerable) mult = 1.4f;
    if (kind == HitKind::Water) slowTimer = 1.4f;
    if (kind == HitKind::Serpent) poisonT = 3.0f;
    if (kind == HitKind::Giyu) mult *= 0.35f;   // he would relish breaking a Hashira
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
    fx.Text({ pos.x, pos.y - h * 0.5f - 16 }, tcol,
            (vulnerable || guardBroken > 0) ? 1.25f : 0.95f, "%.0f", dealt);
    fx.HitSparks({ pos.x, pos.y - 10 }, kbx >= 0 ? 1 : -1, tcol);

    if (hp <= 0) {
        hp = 0;
        state = AkState::Dying;
        stateTimer = 2.0f;
        vulnerable = false;
        orbs.clear();
        waves.clear();
        fx.AddShake(0.7f);
        fx.AddHitstop(0.4f);
    }
}

bool Akaza::ForceOpening(Effects& fx) {
    if (!active || openingCd > 0) return false;
    bool interruptible =
        state == AkState::Stalk || state == AkState::TeleCombo ||
        state == AkState::TeleDash || state == AkState::TeleShock ||
        state == AkState::TeleLeap;
    if (!interruptible) return false;
    openingCd = 8.0f;
    EnterRecover(1.0f);
    fx.Text({ pos.x, pos.y - h - 14 }, C(120, 190, 255), 1.25f, "GIYU CREATES AN OPENING");
    fx.Ring(pos, 16, 150, 520, 8, C(120, 190, 255));
    fx.AddShake(0.22f);
    return true;
}

int Akaza::NullifyOrbs(Vector2 c, float r) {
    int cut = 0;
    for (auto& o : orbs) {
        if (o.alive && Dist(o.pos, c) < r) { o.alive = false; cut++; }
    }
    return cut;
}

int Akaza::OrbsNear(Vector2 c, float r) const {
    int n = 0;
    for (const auto& o : orbs)
        if (o.alive && Dist(o.pos, c) < r) n++;
    return n;
}

void Akaza::Draw() const {
    if (!active || state == AkState::Dead) return;
    float gt = (float)GetTime();

    // shadow (not while falling in)
    if (pos.y > 0)
        DrawEllipse((int)pos.x, (int)(cfg::GROUND_Y + 6), 24, 7, Fade(BLACK, 0.4f));

    // ground shockwaves
    for (const auto& wv : waves) {
        if (wv.r < 14) continue;
        DrawRing(wv.center, wv.r - 6, wv.r + 6, 0, 360, 48, Fade(C(120, 170, 255), 0.55f));
        DrawRing(wv.center, wv.r + 4, wv.r + 8, 0, 360, 48, Fade(C(190, 220, 255), 0.35f));
    }

    float lean = 0;
    if (state == AkState::TeleDash) lean = -facing * 8.0f;
    if (state == AkState::DashBlow) lean = facing * 10.0f;

    Color skin = C(242, 226, 214);
    Color hakama = C(224, 214, 194);
    Color tattoo = C(55, 105, 215);
    Color hairC = C(232, 122, 158);
    if (hitFlash > 0) { skin = C(255, 170, 160); }

    float bx = pos.x + lean, by = pos.y;
    bool telegraphing = (state == AkState::TeleCombo || state == AkState::TeleDash ||
                         state == AkState::TeleShock || state == AkState::TeleLeap);
    if (telegraphing && fmodf(gt * 12.0f, 1.0f) < 0.5f) skin = C(255, 150, 150);

    if (vulnerable) {
        float pulse = 0.45f + 0.3f * sinf(gt * 10.0f);
        DrawRectangleLinesEx({ bx - w * 0.5f - 4, by - h * 0.5f - 4, w + 8, h + 8 },
                             3, Fade(C(255, 210, 80), pulse));
    } else if (guardBroken > 0) {
        float pulse = 0.35f + 0.25f * sinf(gt * 12.0f);
        DrawRectangleLinesEx({ bx - w * 0.5f - 4, by - h * 0.5f - 4, w + 8, h + 8 },
                             3, Fade(C(210, 200, 185), pulse));
    }

    // hakama legs
    DrawRectangle((int)(bx - 10), (int)(by + 8), 9, 26, hakama);
    DrawRectangle((int)(bx + 1),  (int)(by + 8), 9, 26, Fade(C(200, 190, 170), 1.0f));
    DrawRectangle((int)(bx - 10), (int)(by + 6), 20, 4, C(40, 44, 60));   // waistband
    // bare torso
    DrawRectangleRounded({ bx - 11, by - 22, 22, 32 }, 0.3f, 4, skin);
    // criminal tattoos: geometric blue lines
    float tp = 0.75f + 0.25f * sinf(gt * 5.0f);
    Color tat = Fade(tattoo, tp);
    DrawLineEx({ bx, by - 20 }, { bx, by + 4 }, 2, tat);                  // center line
    DrawLineEx({ bx - 10, by - 14 }, { bx + 10, by - 14 }, 2, tat);      // chest cross
    DrawLineEx({ bx - 10, by - 4 },  { bx + 10, by - 4 }, 2, tat);
    DrawLineEx({ bx - 11, by - 20 }, { bx - 6, by - 24 }, 2, tat);       // to the shoulders
    DrawLineEx({ bx + 11, by - 20 }, { bx + 6, by - 24 }, 2, tat);
    // arms — a boxer's guard
    bool punching = (state == AkState::Combo);
    if (punching) {
        float ext = (comboHits % 2 == 0) ? 20.0f : 26.0f;
        Vector2 fist = { bx + facing * ext, by - 12.0f + (comboHits % 2) * 8.0f };
        DrawLineEx({ bx + facing * 6.0f, by - 14 }, fist, 5, skin);
        DrawCircleV(fist, 5, skin);
        DrawCircleV(fist, 6.5f, Fade(C(150, 200, 255), 0.5f));
    } else if (state == AkState::TeleShock || state == AkState::Shockwave) {
        DrawLineEx({ bx - 8, by - 16 }, { bx - 14, by + 2 }, 5, skin);   // fists to the earth
        DrawLineEx({ bx + 8, by - 16 }, { bx + 14, by + 2 }, 5, skin);
        DrawCircleV({ bx - 14, by + 4 }, 4.5f, skin);
        DrawCircleV({ bx + 14, by + 4 }, 4.5f, skin);
    } else {
        DrawLineEx({ bx + facing * 6.0f, by - 15 }, { bx + facing * 13.0f, by - 24 }, 5, skin);
        DrawCircleV({ bx + facing * 13.0f, by - 26 }, 4.5f, skin);       // guard up
        DrawLineEx({ bx - facing * 4.0f, by - 13 }, { bx + facing * 8.0f, by - 6 }, 5, skin);
    }
    // head
    Vector2 headC = { bx + facing * 2.0f, by - 31 };
    DrawCircleV(headC, 9.5f, skin);
    // short spiked pink hair
    for (int i = 0; i < 4; i++) {
        float hxx = headC.x - 8 + i * 5.4f;
        DrawTriangle({ hxx + 2.5f, headC.y - 16 }, { hxx, headC.y - 6 },
                     { hxx + 5.4f, headC.y - 6 }, hairC);
    }
    // tattoo lines across the face
    DrawLineEx({ headC.x - 8, headC.y - 2 }, { headC.x + 8, headC.y - 2 }, 1.5f, tat);
    // fierce yellow eyes
    DrawCircleV({ headC.x + facing * 4.0f, headC.y + 1 }, 2.2f, C(245, 205, 70));

    // telegraph cues
    if (state == AkState::TeleLeap) {
        // crouched coil + target marker on the ground
        DrawRing({ bx, cfg::GROUND_Y }, 26, 30, 0, 360, 24,
                 Fade(C(120, 170, 255), 0.3f + 0.2f * sinf(gt * 16.0f)));
    }
    if (state == AkState::TeleShock) {
        DrawCircleV({ bx, by + 2 }, 6 + 3 * sinf(gt * 20.0f), Fade(C(150, 200, 255), 0.6f));
    }

    // fist orbs: compressed air
    for (const auto& o : orbs) {
        if (!o.alive) continue;
        DrawCircleV(o.pos, 9, Fade(C(120, 170, 255), 0.4f));
        DrawCircleV(o.pos, 5.5f, C(200, 225, 255));
    }
}

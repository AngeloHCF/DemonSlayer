#include "shinobu.h"
#include "player.h"
#include "enemy.h"
#include "boss.h"
#include "akaza.h"
#include "moons.h"
#include "combat.h"
#include "effects.h"
#include "config.h"
#include "audio.h"
#include <cmath>
#include <cstdio>

static const char* SAVE_FILE = "shinobu_mastery.txt";
static const int XP_TH[5] = { 18, 45, 80, 125, 180 };

static Color ShinobuCol() { return C(190, 150, 255); }

static void InsectTrail(Effects& fx, Vector2 p, int facing) {
    fx.SerpentTrail(p, facing);
    if (GetRandomValue(0, 1) == 0) fx.LoveSparkle(p, facing);
}

// ---------------------------------------------------------------- mastery

int ShinobuMastery::Level() const {
    int lv = 0;
    for (int i = 0; i < 5; i++) if (xp >= XP_TH[i]) lv = i + 1;
    return lv;
}

int ShinobuMastery::NextThreshold() const {
    int lv = Level();
    return lv >= 5 ? -1 : XP_TH[lv];
}

void ShinobuMastery::Load() {
    if (!FileExists(SAVE_FILE)) return;
    char* txt = LoadFileText(SAVE_FILE);
    if (!txt) return;
    sscanf(txt, "%d %d %d", &xp, &summons, &kills);
    UnloadFileText(txt);
    if (xp < 0) xp = 0;
    if (xp > 999) xp = 999;
}

void ShinobuMastery::Save() const {
    char buf[64];
    snprintf(buf, sizeof(buf), "%d %d %d", xp, summons, kills);
    SaveFileText(SAVE_FILE, buf);
}

// ---------------------------------------------------------------- lifecycle

void Shinobu::ResetRun() {
    state = ShinobuState::Inactive;
    fallen = false;
    summonedThisRun = false;
    summonCd = 8.0f;
    activeT = 0;
    hp = maxHp = mastery.MaxHp();
    pos = { -120, cfg::GROUND_Y - 38 };
    vel = { 0, 0 };
    hitMem.Clear();
    stateTimer = attackTimer = 0;
    hexCd = zigzagCd = wisteriaCd = healCd = 0;
    hitFlash = iframes = staggerT = 0;
}

bool Shinobu::CanSummon() const {
    return !fallen && state == ShinobuState::Inactive && summonCd <= 0;
}

void Shinobu::Summon(Vector2 playerPos, Effects& fx) {
    state = ShinobuState::Arrive;
    stateTimer = 0.46f;
    summonedThisRun = true;
    mastery.summons++;
    maxHp = mastery.MaxHp();
    hp = maxHp;
    activeT = mastery.Duration();
    hitMem.Clear();
    hexCd = zigzagCd = 0;
    wisteriaCd = 4.0f;
    healCd = 2.0f;
    attackTimer = 0.25f;
    bool fromLeft = playerPos.x > cfg::SCREEN_W * 0.5f;
    pos = { fromLeft ? -50.0f : cfg::SCREEN_W + 50.0f, cfg::GROUND_Y - h * 0.5f };
    facing = fromLeft ? 1 : -1;
    targetPos = { playerPos.x - facing * 82.0f, playerPos.y };
    iframes = 1.0f;
    fx.Text({ playerPos.x, playerPos.y - 112 }, ShinobuCol(), 1.5f, "SHINOBU KOCHO");
    PlaySfx(SFX_SERPENT, 0.9f, 1.35f);
    PlaySfx(SFX_WHOOSH, 0.75f, 1.35f);
}

Rectangle Shinobu::Rect() const {
    return { pos.x - w * 0.5f, pos.y - h * 0.5f, w, h };
}

void Shinobu::TakeDamage(float dmg, float kbx, HitKind kind, Effects& fx) {
    if (!Active() || state == ShinobuState::Arrive || state == ShinobuState::Withdraw) return;
    if (iframes > 0) return;

    bool bossHit = (kind == HitKind::BossDash || kind == HitKind::BossAoe ||
                    kind == HitKind::BossProjectile);
    float dodge = mastery.DodgeChance();
    if (bossHit) dodge *= 0.48f;
    if (state != ShinobuState::WisteriaBloom && frnd(0, 1) < dodge) {
        float dir = kbx >= 0 ? 1.0f : -1.0f;
        vel.x = dir * 520.0f;
        vel.y = -120.0f;
        iframes = 0.34f;
        InsectTrail(fx, pos, (int)-dir);
        PlaySfx(SFX_WHOOSH, 0.4f, 1.45f);
        if (state != ShinobuState::Follow && state != ShinobuState::Heal)
            state = ShinobuState::Follow;
        return;
    }

    hp -= dmg * mastery.DmgTaken();
    hitFlash = 0.15f;
    if (bossHit) {
        iframes = 0.75f;
        staggerT = 0.6f;
        state = ShinobuState::Follow;
        vel.x = kbx * 1.55f;
        vel.y = -390.0f;
        fx.AddShake(0.35f);
        fx.AddHitstop(0.05f);
        fx.Ring({ pos.x, pos.y - 6 }, 8, 75, 440, 5, C(255, 120, 100));
        PlaySfx(SFX_STONE, 0.5f, 1.45f);
    } else {
        iframes = 0.5f;
        vel.x = kbx * 0.75f;
        vel.y = -180.0f;
    }
    fx.BloodSpray({ pos.x, pos.y - 8 }, kbx > 0 ? 1 : -1, bossHit ? 1.65f : 0.95f);
    fx.Text({ pos.x, pos.y - h }, C(255, 110, 145), 0.9f, "-%.0f", dmg);
    PlaySfx(SFX_HURT, 0.55f, 1.05f);

    if (hp <= 0) {
        hp = 0;
        fallen = true;
        state = ShinobuState::Fallen;
        stateTimer = 2.2f;
        vel = { 0, 0 };
        fx.AddShake(0.5f);
        fx.AddHitstop(0.15f);
        fx.DeathBurst(pos, ShinobuCol(), 1.5f);
        fx.Text({ pos.x, pos.y - h - 20 }, C(220, 70, 120), 1.5f, "SHINOBU HAS FALLEN");
        PlaySfx(SFX_DEATH, 1.0f, 0.75f);
        mastery.Save();
    }
}

// ---------------------------------------------------------------- AI

void Shinobu::PickAction(Player& player, std::vector<Enemy>& enemies,
                         Boss& boss, Akaza& akaza, UpperMoon* moon,
                         CombatSystem& cs, Effects& fx) {
    int best = -1;
    float bestScore = -1e9f;
    int crowd = 0, windups = 0;
    for (int i = 0; i < (int)enemies.size(); i++) {
        const Enemy& e = enemies[i];
        if (!e.alive) continue;
        float dS = Dist(e.pos, pos);
        float dP = Dist(e.pos, player.pos);
        if (dS < 140) crowd++;
        if (e.Busy() && dP < 190) windups++;
        float score = 330.0f - dS * 0.45f;
        if (e.Busy() && dP < 155) score += 360;
        if (e.type == EType::Brute) score += 100;
        if (dP < 110) score += 130;
        if (score > bestScore) { bestScore = score; best = i; }
    }

    bool moonUp = moon && moon->Alive();
    targetIsBoss = (best < 0 && (boss.Alive() || akaza.Alive() || moonUp));
    if (best >= 0)          targetPos = enemies[best].pos;
    else if (akaza.Alive()) targetPos = akaza.pos;
    else if (moonUp)        targetPos = moon->pos;
    else if (targetIsBoss)  targetPos = boss.pos;
    else                    targetPos = { player.pos.x + 95.0f, player.pos.y };

    float distT = Dist(targetPos, pos);

    if (healCd <= 0 && player.hp < player.maxHp * 0.55f && Dist(player.pos, pos) < 280) {
        state = ShinobuState::Heal;
        stateTimer = 0.65f;
        formHits = 0;
        healCd = 15.0f - mastery.Level();
        vel.x = 0;
        fx.Text({ pos.x, pos.y - h - 12 }, C(235, 210, 255), 1.0f, "BUTTERFLY TRIAGE");
        PlaySfx(SFX_LOVE, 0.85f, 1.25f);
        return;
    }

    if (mastery.HasWisteriaBloom() && wisteriaCd <= 0 &&
        (crowd >= 2 || windups >= 2 || targetIsBoss) && distT < 260) {
        state = ShinobuState::WisteriaBloom;
        stateTimer = 1.35f;
        tickT = 0;
        formHits = 0;
        wisteriaCd = 18.0f;
        vel.x = 0;
        fx.Text({ pos.x, pos.y - h - 12 }, ShinobuCol(), 1.0f, "WISTERIA BLOOM");
        fx.Ring(pos, 18, 170, 340, 6, ShinobuCol());
        PlaySfx(SFX_MIST, 0.9f, 1.25f);
        return;
    }

    if (attackTimer > 0 || (best < 0 && !targetIsBoss)) return;
    if (distT > 560) return;

    facing = targetPos.x > pos.x ? 1 : -1;

    ShinobuState form;
    if (crowd >= 3 && hexCd <= 0) form = ShinobuState::FormHexagon;
    else if (zigzagCd <= 0 && distT > 130) form = ShinobuState::FormZigzag;
    else if (distT > 170) form = ShinobuState::FormFlutter;
    else form = ShinobuState::FormCaprice;

    if (form == lastForm) {
        if (form == ShinobuState::FormCaprice)
            form = (hexCd <= 0) ? ShinobuState::FormHexagon : ShinobuState::FormFlutter;
        else if (form == ShinobuState::FormFlutter)
            form = ShinobuState::FormCaprice;
        else if (form == ShinobuState::FormHexagon)
            form = ShinobuState::FormFlutter;
        else
            form = ShinobuState::FormCaprice;
    }
    lastForm = form;
    state = form;

    Color callC = ShinobuCol();
    switch (form) {
        case ShinobuState::FormFlutter:
            stateTimer = 0.34f;
            curId = cs.NewId();
            fx.Text({ pos.x, pos.y - h - 14 }, callC, 0.85f, "BEE STING");
            PlaySfx(SFX_SERPENT, 0.7f, 1.45f);
            break;
        case ShinobuState::FormHexagon:
            stateTimer = 0.72f;
            tickT = 0;
            formHits = 0;
            hexCd = 5.5f;
            fx.Text({ pos.x, pos.y - h - 14 }, callC, 0.85f, "DRAGONFLY: HEXAGON");
            PlaySfx(SFX_SLASH, 0.55f, 1.35f);
            break;
        case ShinobuState::FormZigzag:
            stateTimer = 0.72f;
            tickT = 0;
            formHits = 0;
            zigzagCd = 7.0f;
            fx.Text({ pos.x, pos.y - h - 14 }, callC, 0.85f, "CENTIPEDE: ZIGZAG");
            PlaySfx(SFX_WHOOSH, 0.7f, 1.55f);
            break;
        default:
            stateTimer = 0.45f;
            tickT = 0;
            formHits = 0;
            fx.Text({ pos.x, pos.y - h - 14 }, callC, 0.85f, "BUTTERFLY: CAPRICE");
            PlaySfx(SFX_SLASH, 0.55f, 1.25f);
            break;
    }
    attackTimer = mastery.Cadence();
}

void Shinobu::Update(float dt, Player& player, std::vector<Enemy>& enemies,
                     Boss& boss, Akaza& akaza, UpperMoon* moon,
                     CombatSystem& cs, Effects& fx) {
    if (state == ShinobuState::Inactive) {
        summonCd = fmaxf(summonCd - dt, 0);
        return;
    }
    if (state == ShinobuState::Fallen) {
        if (stateTimer > 0) stateTimer -= dt;
        return;
    }

    hitFlash = fmaxf(hitFlash - dt, 0);
    iframes = fmaxf(iframes - dt, 0);
    staggerT = fmaxf(staggerT - dt, 0);
    attackTimer = fmaxf(attackTimer - dt, 0);
    hexCd = fmaxf(hexCd - dt, 0);
    zigzagCd = fmaxf(zigzagCd - dt, 0);
    wisteriaCd = fmaxf(wisteriaCd - dt, 0);
    healCd = fmaxf(healCd - dt, 0);

    if (state != ShinobuState::Withdraw && state != ShinobuState::Arrive) {
        activeT -= dt;
        if (activeT <= 0) {
            state = ShinobuState::Withdraw;
            facing = pos.x < cfg::SCREEN_W * 0.5f ? -1 : 1;
            fx.Text({ pos.x, pos.y - h - 10 }, ShinobuCol(), 1.0f, "shinobu withdraws");
            PlaySfx(SFX_WHOOSH, 0.6f, 1.25f);
        }
    }

    float dmgMul = mastery.DmgMult();

    switch (state) {
        case ShinobuState::Arrive: {
            stateTimer -= dt;
            float spd = 1650.0f;
            float dx = targetPos.x - pos.x;
            if (fabsf(dx) > 18) vel.x = (dx > 0 ? 1.0f : -1.0f) * spd;
            else vel.x = 0;
            InsectTrail(fx, pos, facing);
            if (stateTimer <= 0) {
                state = ShinobuState::Follow;
                vel.x = 0;
                fx.Ring(pos, 10, 90, 460, 5, ShinobuCol());
                fx.AddShake(0.14f);
            }
            break;
        }
        case ShinobuState::Follow: {
            if (staggerT > 0) {
                vel.x *= 1.0f - Clampf(4.5f * dt, 0, 1);
                break;
            }
            PickAction(player, enemies, boss, akaza, moon, cs, fx);
            if (state != ShinobuState::Follow) break;
            float want = targetIsBoss ? 135.0f : 72.0f;
            float dx = targetPos.x - pos.x;
            facing = dx > 0 ? 1 : -1;
            if (fabsf(dx) > want) vel.x = facing * mastery.MoveSpeed();
            else vel.x *= 1.0f - Clampf(10.0f * dt, 0, 1);
            break;
        }
        case ShinobuState::FormFlutter: {
            stateTimer -= dt;
            if (stateTimer > 0.20f) {
                vel.x *= 1.0f - Clampf(12.0f * dt, 0, 1);
            } else if (stateTimer > 0.05f) {
                vel.x = facing * 1480.0f;
                InsectTrail(fx, pos, facing);
                Rectangle r = {
                    facing > 0 ? pos.x - 4 : pos.x - 112,
                    pos.y - 36, 116, 72
                };
                cs.Add(r, 19.0f * dmgMul, facing * 230.0f, -130, 0.035f,
                       Team::Player, HitKind::Shinobu, curId);
            } else {
                vel.x *= 1.0f - Clampf(14.0f * dt, 0, 1);
            }
            if (stateTimer <= 0) state = ShinobuState::Follow;
            break;
        }
        case ShinobuState::FormCaprice: {
            stateTimer -= dt;
            tickT -= dt;
            vel.x *= 1.0f - Clampf(7.0f * dt, 0, 1);
            if (tickT <= 0 && formHits < 4) {
                tickT = 0.10f;
                formHits++;
                vel.x = facing * 240.0f;
                Rectangle r = {
                    facing > 0 ? pos.x : pos.x - 92,
                    pos.y - 34, 92, 68
                };
                cs.Add(r, 6.4f * dmgMul, facing * 135.0f, -90, 0.045f,
                       Team::Player, HitKind::Shinobu, cs.NewId());
                float a0 = formHits % 2 ? -50.0f : 45.0f;
                float a1 = formHits % 2 ? 45.0f : -50.0f;
                if (facing < 0) { a0 = 180 - a0; a1 = 180 - a1; }
                fx.SlashArc(pos, 64, a0, a1, ShinobuCol());
                PlaySfx(SFX_SLASH, 0.34f, 1.25f + formHits * 0.08f);
            }
            if (stateTimer <= 0) state = ShinobuState::Follow;
            break;
        }
        case ShinobuState::FormHexagon: {
            stateTimer -= dt;
            tickT -= dt;
            vel.x *= 1.0f - Clampf(8.0f * dt, 0, 1);
            if (tickT <= 0 && formHits < 6) {
                tickT = 0.095f;
                formHits++;
                float yOff = (formHits % 3 - 1) * 18.0f;
                Rectangle r = {
                    facing > 0 ? pos.x - 12 : pos.x - 96,
                    pos.y - 38 + yOff, 108, 34
                };
                cs.Add(r, 5.3f * dmgMul, facing * 120.0f, -80, 0.045f,
                       Team::Player, HitKind::Shinobu, cs.NewId());
                Vector2 arcC = { pos.x + facing * 28.0f, pos.y + yOff * 0.2f };
                fx.SlashArc(arcC, 54 + formHits * 2.0f, -40, 40, ShinobuCol());
                InsectTrail(fx, arcC, facing);
                PlaySfx(SFX_SLASH, 0.28f, 1.35f + formHits * 0.04f);
            }
            if (stateTimer <= 0) {
                state = ShinobuState::Follow;
                fx.Ring(pos, 16, 105, 420, 4, ShinobuCol());
            }
            break;
        }
        case ShinobuState::FormZigzag: {
            stateTimer -= dt;
            tickT -= dt;
            if (tickT <= 0 && formHits < 4) {
                formHits++;
                tickT = 0.15f;
                int dir = (formHits % 2 == 1) ? facing : -facing;
                vel.x = dir * (formHits % 2 == 1 ? 1120.0f : 820.0f);
                vel.y = (formHits % 2 == 1) ? -70.0f : 40.0f;
                Rectangle r = {
                    dir > 0 ? pos.x - 8 : pos.x - 102,
                    pos.y - 36, 110, 72
                };
                cs.Add(r, 10.5f * dmgMul, dir * 210.0f, -140, 0.055f,
                       Team::Player, HitKind::Shinobu, cs.NewId());
                InsectTrail(fx, pos, dir);
                fx.Ring({ pos.x + dir * 25.0f, pos.y }, 4, 70, 420, 4, ShinobuCol());
                PlaySfx(SFX_WHOOSH, 0.35f, 1.45f + formHits * 0.05f);
            }
            if (stateTimer <= 0) {
                state = ShinobuState::Follow;
                vel.x *= 0.25f;
            }
            break;
        }
        case ShinobuState::WisteriaBloom: {
            stateTimer -= dt;
            tickT -= dt;
            vel.x *= 1.0f - Clampf(7.0f * dt, 0, 1);
            InsectTrail(fx, { pos.x + frnd(-45, 45), pos.y + frnd(-36, 22) }, facing);
            if (tickT <= 0) {
                tickT = 0.22f;
                formHits++;
                Rectangle r = { pos.x - 155, pos.y - 82, 310, 164 };
                cs.Add(r, 3.0f * dmgMul, 0, -40, 0.05f,
                       Team::Player, HitKind::Shinobu, cs.NewId());
                fx.Ring(pos, 24 + formHits * 5.0f, 160, 320, 4, ShinobuCol());
                if (formHits % 2 == 0 && Dist(player.pos, pos) < 170)
                    player.Heal(1.5f + mastery.Level() * 0.35f, fx);
            }
            if (stateTimer <= 0) state = ShinobuState::Follow;
            break;
        }
        case ShinobuState::Heal: {
            stateTimer -= dt;
            vel.x *= 1.0f - Clampf(8.0f * dt, 0, 1);
            if (formHits == 0 && stateTimer <= 0.36f) {
                formHits = 1;
                player.Heal(mastery.HealAmount(), fx);
                fx.Ring(player.pos, 8, 90, 360, 5, C(235, 210, 255));
                fx.Text({ player.pos.x, player.pos.y - 92 }, C(235, 210, 255), 0.9f, "mended");
            }
            if (stateTimer <= 0) state = ShinobuState::Follow;
            break;
        }
        case ShinobuState::Withdraw: {
            vel.x = facing * 1250.0f;
            InsectTrail(fx, pos, facing);
            if (pos.x < -70 || pos.x > cfg::SCREEN_W + 70) {
                state = ShinobuState::Inactive;
                summonCd = mastery.SummonCd();
                mastery.xp += 5;
                mastery.Save();
            }
            break;
        }
        default: break;
    }

    if (state != ShinobuState::Arrive) vel.y += cfg::GRAVITY * dt;
    else vel.y = 0;
    pos.x += vel.x * dt;
    pos.y += vel.y * dt;
    if (state != ShinobuState::Withdraw && state != ShinobuState::Arrive)
        pos.x = Clampf(pos.x, 28.0f, (float)cfg::SCREEN_W - 28.0f);
    onGround = GroundClamp(pos, vel, h * 0.5f);
}

// ---------------------------------------------------------------- drawing

void Shinobu::Draw() const {
    if (state == ShinobuState::Inactive) return;
    float gt = (float)GetTime();

    float alpha = 1.0f;
    if (state == ShinobuState::Fallen) {
        alpha = Clampf(stateTimer / 2.2f, 0, 1);
        if (alpha <= 0.01f) return;
    }
    if (iframes > 0)
        alpha *= fmodf(gt * 16.0f, 2.0f) < 1.0f ? 0.55f : 1.0f;

    DrawEllipse((int)pos.x, (int)(cfg::GROUND_Y + 6), 17, 5, Fade(BLACK, 0.32f * alpha));

    float bx = pos.x, by = pos.y;
    bool kneel = (state == ShinobuState::Fallen);
    if (kneel) by += 9;

    Color uniform = C(22, 22, 34);
    Color haori = C(235, 232, 244);
    Color wingA = C(185, 150, 245);
    Color wingB = C(105, 210, 190);
    Color skin = C(238, 211, 190);
    Color hair = C(28, 22, 42);
    Color bladeC = C(210, 200, 255);
    if (hitFlash > 0) { uniform = C(255, 150, 170); haori = uniform; wingA = uniform; }

    DrawRectangle((int)(bx - 8), (int)(by + 7), 7, kneel ? 11 : 21, Fade(uniform, alpha));
    DrawRectangle((int)(bx + 1), (int)(by + 7), 7, kneel ? 11 : 21, Fade(C(16, 16, 28), alpha));
    DrawRectangleRounded({ bx - 10, by - 19, 20, 29 }, 0.3f, 4, Fade(uniform, alpha));

    // butterfly haori wings
    DrawTriangle({ bx - 10, by - 19 }, { bx - 30, by - 4 }, { bx - 9, by + 12 }, Fade(haori, alpha));
    DrawTriangle({ bx + 10, by - 19 }, { bx + 30, by - 4 }, { bx + 9, by + 12 }, Fade(haori, alpha));
    DrawTriangle({ bx - 24, by - 3 }, { bx - 12, by + 8 }, { bx - 16, by - 10 }, Fade(wingA, 0.72f * alpha));
    DrawTriangle({ bx + 24, by - 3 }, { bx + 12, by + 8 }, { bx + 16, by - 10 }, Fade(wingA, 0.72f * alpha));
    DrawLineEx({ bx - 26, by + 4 }, { bx - 12, by + 8 }, 2, Fade(wingB, alpha));
    DrawLineEx({ bx + 26, by + 4 }, { bx + 12, by + 8 }, 2, Fade(wingB, alpha));
    DrawRectangle((int)(bx - 10), (int)(by + 3), 20, 4, Fade(C(225, 222, 215), alpha));

    Vector2 headC = { bx + facing * 1.5f, by - 28 };
    DrawCircleV(headC, 8.5f, Fade(skin, alpha));
    DrawCircleSector(headC, 10.0f, 170, 372, 12, Fade(hair, alpha));
    DrawCircleV({ headC.x - facing * 7.0f, headC.y - 9.0f }, 5.0f, Fade(hair, alpha));
    DrawCircleV({ headC.x - facing * 12.0f, headC.y - 11.0f }, 2.4f, Fade(wingA, alpha));
    DrawCircleV({ headC.x - facing * 4.0f, headC.y - 12.0f }, 2.4f, Fade(wingB, alpha));
    DrawCircleV({ headC.x + facing * 4.0f, headC.y + 1.0f }, 1.3f, Fade(C(75, 55, 115), alpha));

    Vector2 hand = { bx + facing * 10.0f, by - 6 };
    float ang = 32.0f;
    switch (state) {
        case ShinobuState::FormFlutter: ang = 0; break;
        case ShinobuState::FormCaprice: ang = sinf(gt * 54.0f) * 34.0f; break;
        case ShinobuState::FormHexagon: ang = sinf(gt * 70.0f) * 50.0f; break;
        case ShinobuState::FormZigzag:  ang = fmodf(gt * 980.0f, 360.0f); break;
        case ShinobuState::WisteriaBloom: ang = -18.0f; break;
        case ShinobuState::Heal: ang = 78.0f; break;
        case ShinobuState::Withdraw: ang = 20.0f; break;
        case ShinobuState::Fallen: ang = 85.0f; break;
        default: break;
    }
    bool spin = state == ShinobuState::FormZigzag;
    if (facing < 0 && !spin) ang = 180.0f - ang;
    DrawLineEx({ bx + facing * 5.0f, by - 13 }, hand, 4, Fade(uniform, alpha));
    Rectangle blade = { hand.x, hand.y, 52, 3.0f };
    DrawRectanglePro(blade, { 0, 1.5f }, ang, Fade(bladeC, alpha));
    DrawCircleV({ hand.x + cosf(ang * DEG2RAD) * 52.0f,
                  hand.y + sinf(ang * DEG2RAD) * 52.0f }, 3.5f,
                Fade(ShinobuCol(), alpha));
    Rectangle hilt = { hand.x, hand.y, 8, 6 };
    DrawRectanglePro(hilt, { 4, 3 }, ang, Fade(C(55, 42, 85), alpha));

    if (state == ShinobuState::WisteriaBloom) {
        float p = fmodf(gt * 1.3f, 1.0f);
        DrawRing(pos, 150 * p, 150 * p + 4, 0, 360, 42, Fade(ShinobuCol(), 0.5f * (1 - p)));
        for (int i = 0; i < 4; i++) {
            float a = gt * 1.8f + i * PI * 0.5f;
            Vector2 q = { pos.x + cosf(a) * 58.0f, pos.y - 18 + sinf(a * 1.7f) * 22.0f };
            DrawCircleV(q, 2.0f, Fade(C(220, 190, 255), alpha));
        }
    }

    if (hp < maxHp && Active()) {
        float bw = w + 16;
        float f = Clampf(hp / maxHp, 0, 1);
        DrawRectangle((int)(pos.x - bw * 0.5f), (int)(pos.y - h * 0.5f - 16), (int)bw, 5, C(16, 18, 26));
        DrawRectangle((int)(pos.x - bw * 0.5f + 1), (int)(pos.y - h * 0.5f - 15),
                      (int)((bw - 2) * f), 3, ShinobuCol());
    }
    if (Active() && state != ShinobuState::Withdraw) {
        float f = Clampf(activeT / mastery.Duration(), 0, 1);
        DrawRing({ pos.x, pos.y - h * 0.5f - 25 }, 5, 8, -90, -90 + 360 * f, 24,
                 Fade(ShinobuCol(), 0.8f));
    }
}

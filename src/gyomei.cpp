#include "gyomei.h"
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

static const char* SAVE_FILE = "gyomei_mastery.txt";
static const int XP_TH[5] = { 26, 60, 105, 165, 240 };

static Color GyomeiCol() { return C(188, 178, 158); }
static Color StoneDark() { return C(84, 78, 70); }
static Color ChainCol() { return C(204, 198, 184); }

static bool UltimateDanger(const Boss& boss, const Akaza& akaza, const UpperMoon* moon) {
    return (boss.Alive() && boss.state == BState::Desperation) ||
           (akaza.Alive() && akaza.state == AkState::Desperation) ||
           (moon && moon->Alive() && moon->kind == MOON_KOKU &&
            moon->state == MState::Desperation);
}

static float UltimateSourceX(const Boss& boss, const Akaza& akaza, const UpperMoon* moon, float fallback) {
    if (boss.Alive() && boss.state == BState::Desperation) return boss.pos.x;
    if (akaza.Alive() && akaza.state == AkState::Desperation) return akaza.pos.x;
    if (moon && moon->Alive() && moon->kind == MOON_KOKU && moon->state == MState::Desperation)
        return moon->pos.x;
    return fallback;
}

static void StoneBurst(Effects& fx, Vector2 p, float power = 1.0f) {
    fx.StoneSlam({ p.x, cfg::GROUND_Y });
    fx.Sparks(p, -90, 130, (int)(10 * power), C(235, 230, 210), 420 * power, 3.0f);
    fx.Ring({ p.x, cfg::GROUND_Y - 8 }, 12, 120 * power, 520, 8, GyomeiCol());
}

static void ChainTrail(Effects& fx, Vector2 p, int facing) {
    fx.Sparks(p, facing > 0 ? 180.0f : 0.0f, 44, 3, ChainCol(), 280, 2.2f);
    if (GetRandomValue(0, 2) == 0) fx.Dust({ p.x + frnd(-12, 12), cfg::GROUND_Y });
}

static void DrawChain(Vector2 a, Vector2 b, Color col, float alpha) {
    DrawLineEx(a, b, 3.0f, Fade(C(58, 56, 54), 0.8f * alpha));
    float d = Dist(a, b);
    int links = (int)Clampf(d / 12.0f, 3.0f, 24.0f);
    for (int i = 1; i < links; i++) {
        float t = i / (float)links;
        Vector2 p = { Lerpf(a.x, b.x, t), Lerpf(a.y, b.y, t) };
        DrawCircleLines((int)p.x, (int)p.y, 3.5f, Fade(col, alpha));
    }
}

static void DrawAxe(Vector2 p, float ang, int facing, float alpha) {
    Rectangle haft = { p.x, p.y, 48.0f, 5.5f };
    DrawRectanglePro(haft, { 4.0f, 2.75f }, ang, Fade(C(72, 50, 34), alpha));
    float r = ang * DEG2RAD;
    Vector2 tip = { p.x + cosf(r) * 48.0f, p.y + sinf(r) * 48.0f };
    Vector2 n = { -sinf(r), cosf(r) };
    Vector2 t = { cosf(r), sinf(r) };
    DrawTriangle({ tip.x + n.x * 20, tip.y + n.y * 20 },
                 { tip.x - n.x * 20, tip.y - n.y * 20 },
                 { tip.x + t.x * 20, tip.y + t.y * 20 },
                 Fade(C(206, 202, 190), alpha));
    DrawTriangle({ tip.x + n.x * 14 - t.x * 4, tip.y + n.y * 14 - t.y * 4 },
                 { tip.x - n.x * 14 - t.x * 4, tip.y - n.y * 14 - t.y * 4 },
                 { tip.x - t.x * 24, tip.y - t.y * 24 },
                 Fade(C(152, 146, 134), alpha));
}

static void DrawFlail(Vector2 p, float alpha) {
    DrawCircleV(p, 14.0f, Fade(C(122, 116, 106), alpha));
    DrawCircleLines((int)p.x, (int)p.y, 14.0f, Fade(C(225, 220, 205), alpha));
    for (int i = 0; i < 8; i++) {
        float a = (i * 45.0f) * DEG2RAD;
        Vector2 inner = { p.x + cosf(a) * 11.0f, p.y + sinf(a) * 11.0f };
        Vector2 outer = { p.x + cosf(a) * 21.0f, p.y + sinf(a) * 21.0f };
        Vector2 side = { -sinf(a) * 4.0f, cosf(a) * 4.0f };
        DrawTriangle({ inner.x + side.x, inner.y + side.y },
                     { inner.x - side.x, inner.y - side.y },
                     outer, Fade(C(215, 210, 194), alpha));
    }
}

// ---------------------------------------------------------------- mastery

int GyomeiMastery::Level() const {
    int lv = 0;
    for (int i = 0; i < 5; i++) if (xp >= XP_TH[i]) lv = i + 1;
    return lv;
}

int GyomeiMastery::NextThreshold() const {
    int lv = Level();
    return lv >= 5 ? -1 : XP_TH[lv];
}

void GyomeiMastery::Load() {
    if (!FileExists(SAVE_FILE)) return;
    char* txt = LoadFileText(SAVE_FILE);
    if (!txt) return;
    sscanf(txt, "%d %d %d", &xp, &summons, &kills);
    UnloadFileText(txt);
    if (xp < 0) xp = 0;
    if (xp > 999) xp = 999;
}

void GyomeiMastery::Save() const {
    char buf[64];
    snprintf(buf, sizeof(buf), "%d %d %d", xp, summons, kills);
    SaveFileText(SAVE_FILE, buf);
}

// ---------------------------------------------------------------- lifecycle

void Gyomei::ResetRun() {
    state = GyomeiState::Inactive;
    fallen = false;
    summonedThisRun = false;
    summonCd = 0;
    activeT = 0;
    hp = maxHp = mastery.MaxHp();
    pos = { -180, cfg::GROUND_Y - 46 };
    vel = { 0, 0 };
    hitMem.Clear();
    stateTimer = attackTimer = 0;
    serpentCd = upperCd = conquestCd = justiceCd = stoneGuardCd = 0;
    tickT = 0;
    formHits = 0;
    curId = -1;
    hitFlash = iframes = dodgeT = staggerT = 0;
    stoneGuardShield = stoneGuardShieldMax = 0;
    ultDangerLast = false;
    exitDir = -1;
}

bool Gyomei::CanSummon() const {
    return !fallen && state == GyomeiState::Inactive && summonCd <= 0;
}

void Gyomei::Summon(Vector2 playerPos, Effects& fx) {
    state = GyomeiState::Arrive;
    stateTimer = 0.82f;
    summonedThisRun = true;
    mastery.summons++;
    maxHp = mastery.MaxHp();
    if (hp <= 0) hp = maxHp;
    hp = fminf(hp, maxHp);
    activeT = mastery.Duration();
    hitMem.Clear();
    serpentCd = upperCd = conquestCd = 0;
    justiceCd = 9.0f;
    stoneGuardCd = 3.0f;
    stoneGuardShield = stoneGuardShieldMax = 0;
    ultDangerLast = false;
    attackTimer = 0.62f;
    bool fromLeft = playerPos.x > cfg::SCREEN_W * 0.5f;
    pos = { fromLeft ? -92.0f : cfg::SCREEN_W + 92.0f, cfg::GROUND_Y - h * 0.5f };
    facing = fromLeft ? 1 : -1;
    exitDir = fromLeft ? -1 : 1;
    targetPos = { playerPos.x - facing * 118.0f, playerPos.y };
    iframes = 1.2f;
    fx.Text({ playerPos.x, playerPos.y - 124 }, GyomeiCol(), 1.55f, "GYOMEI HIMEJIMA");
    fx.Text({ playerPos.x, playerPos.y - 98 }, C(230, 220, 190), 1.0f, "STONE HASHIRA");
    fx.Ring(playerPos, 16, 190, 480, 9, GyomeiCol());
    fx.AddShake(0.42f);
    fx.AddHitstop(0.08f);
    PlaySfx(SFX_STONE, 1.0f, 0.62f);
    PlaySfx(SFX_CHAIN, 0.85f, 0.82f);
}

void Gyomei::BeginWithdraw(Effects& fx) {
    if (state == GyomeiState::Inactive || state == GyomeiState::Fallen ||
        state == GyomeiState::Withdraw) return;

    state = GyomeiState::Withdraw;
    stateTimer = 0;
    attackTimer = 0;
    tickT = 0;
    curId = -1;
    formHits = 0;
    stoneGuardShield = stoneGuardShieldMax = 0;
    ultDangerLast = false;
    hitMem.Clear();
    facing = exitDir;
    vel = { facing * 820.0f, 0 };
    iframes = 0.9f;
    fx.Text({ pos.x, pos.y - h - 10 }, GyomeiCol(), 0.9f, "GYOMEI WITHDRAWS");
}

Rectangle Gyomei::Rect() const {
    return { pos.x - w * 0.5f, pos.y - h * 0.5f, w, h };
}

Rectangle Gyomei::GuardRect(const Player& player) const {
    float left = fminf(pos.x, player.pos.x) - 125.0f;
    float right = fmaxf(pos.x, player.pos.x) + 125.0f;
    if (facing > 0) right += 90.0f;
    else left -= 90.0f;
    left = Clampf(left, -80.0f, (float)cfg::SCREEN_W + 80.0f);
    right = Clampf(right, -80.0f, (float)cfg::SCREEN_W + 80.0f);
    return { left, cfg::GROUND_Y - 250.0f, fmaxf(160.0f, right - left), 258.0f };
}

void Gyomei::TakeDamage(float dmg, float kbx, HitKind kind, Effects& fx) {
    if (!Active() || state == GyomeiState::Arrive || state == GyomeiState::Withdraw) return;
    if (iframes > 0) return;

    bool bossHit = (kind == HitKind::BossDash || kind == HitKind::BossAoe ||
                    kind == HitKind::BossProjectile);
    if (state == GyomeiState::StoneGuard && stoneGuardShield > 0) {
        stoneGuardShield -= bossHit ? dmg * 0.65f : dmg * 0.35f;
        fx.Sparks({ pos.x + facing * 42.0f, pos.y - 18.0f }, facing > 0 ? 180.0f : 0.0f,
                  90, 14, C(245, 240, 210), 420, 3.2f);
        fx.Ring({ pos.x + facing * 42.0f, pos.y - 18.0f }, 10, 84, 360, 5, GyomeiCol());
        fx.AddShake(0.10f);
        PlaySfx(SFX_CHAIN, 0.45f, 1.12f);
        if (stoneGuardShield <= 0)
            fx.Text({ pos.x, pos.y - h - 12 }, GyomeiCol(), 0.95f, "stone guard breaks");
        return;
    }

    if (!bossHit && state != GyomeiState::ArcsJustice && frnd(0, 1) < mastery.DodgeChance()) {
        float dir = kbx >= 0 ? 1.0f : -1.0f;
        vel.x = dir * 245.0f;
        iframes = 0.30f;
        dodgeT = 0.28f;
        fx.Dust({ pos.x, cfg::GROUND_Y });
        PlaySfx(SFX_CHAIN, 0.35f, 1.18f);
        if (state != GyomeiState::Follow && state != GyomeiState::StoneGuard)
            state = GyomeiState::Follow;
        return;
    }

    hp -= dmg * mastery.DmgTaken() * (bossHit ? 0.86f : 1.0f);
    hitFlash = 0.18f;
    if (bossHit) {
        iframes = 0.62f;
        staggerT = 0.42f;
        state = GyomeiState::Follow;
        vel.x = kbx * 0.78f;
        vel.y = -245.0f;
        fx.AddShake(0.34f);
        fx.AddHitstop(0.05f);
        fx.Ring({ pos.x, pos.y - 6 }, 10, 98, 420, 6, GyomeiCol());
        PlaySfx(SFX_STONE, 0.62f, 0.85f);
    } else {
        iframes = 0.42f;
        vel.x = kbx * 0.38f;
        vel.y = -110.0f;
    }
    fx.BloodSpray({ pos.x, pos.y - 8 }, kbx > 0 ? 1 : -1, bossHit ? 1.15f : 0.8f);
    fx.Text({ pos.x, pos.y - h }, C(255, 125, 105), 0.9f, "-%.0f", dmg);
    PlaySfx(SFX_HURT, 0.45f, 0.72f);

    if (hp <= 0) {
        hp = 0;
        fallen = true;
        state = GyomeiState::Fallen;
        stateTimer = 2.4f;
        vel = { 0, 0 };
        fx.AddShake(0.72f);
        fx.AddHitstop(0.20f);
        fx.StoneSlam({ pos.x, cfg::GROUND_Y });
        fx.DeathBurst(pos, GyomeiCol(), 1.9f);
        fx.Text({ pos.x, pos.y - h - 20 }, C(215, 70, 65), 1.5f, "GYOMEI HAS FALLEN");
        PlaySfx(SFX_DEATH, 1.0f, 0.48f);
        mastery.Save();
    }
}

// ---------------------------------------------------------------- AI

void Gyomei::PickAction(Player& player, std::vector<Enemy>& enemies,
                        Boss& boss, Akaza& akaza, UpperMoon* moon,
                        CombatSystem& cs, Effects& fx) {
    int best = -1;
    float bestScore = -1e9f;
    int crowd = 0, windups = 0, brutes = 0;
    for (int i = 0; i < (int)enemies.size(); i++) {
        const Enemy& e = enemies[i];
        if (!e.alive) continue;
        float dG = Dist(e.pos, pos);
        float dP = Dist(e.pos, player.pos);
        if (dG < 210) crowd++;
        if (e.type == EType::Brute && dG < 260) brutes++;
        if (e.Busy() && dP < 230) windups++;
        float score = 360.0f - dG * 0.30f;
        if (e.Busy() && dP < 185) score += 430;
        if (e.type == EType::Brute) score += 180;
        if (dP < 125) score += 180;
        if (score > bestScore) { bestScore = score; best = i; }
    }

    bool moonUp = moon && moon->Alive();
    targetIsBoss = (best < 0 && (boss.Alive() || akaza.Alive() || moonUp));
    if (best >= 0)          targetPos = enemies[best].pos;
    else if (akaza.Alive()) targetPos = akaza.pos;
    else if (moonUp)        targetPos = moon->pos;
    else if (targetIsBoss)  targetPos = boss.pos;
    else                    targetPos = { player.pos.x - 135.0f, player.pos.y };

    float distT = Dist(targetPos, pos);

    bool bossThreat = boss.Alive() &&
        (boss.state == BState::Desperation || boss.state == BState::WhipStorm ||
         boss.state == BState::Arena || boss.CrescentsNear(player.pos, 280) >= 2 ||
         boss.CrescentsNear(pos, 240) >= 2);
    bool akazaThreat = akaza.Alive() &&
        (akaza.state == AkState::Desperation || akaza.state == AkState::Shockwave ||
         akaza.OrbsNear(player.pos, 260) >= 2 || akaza.OrbsNear(pos, 230) >= 2);
    bool moonThreat = moon && moon->Menacing(pos, player.pos);
    if (stoneGuardCd <= 0 && (bossThreat || akazaThreat || moonThreat || windups >= 3)) {
        state = GyomeiState::StoneGuard;
        stateTimer = 2.35f + 0.10f * mastery.Level();
        tickT = 0;
        formHits = 0;
        stoneGuardShieldMax = mastery.StoneGuardShield();
        stoneGuardShield = stoneGuardShieldMax;
        stoneGuardCd = mastery.StoneGuardCd();
        facing = UltimateSourceX(boss, akaza, moon, targetPos.x) > pos.x ? 1 : -1;
        activeT = fmaxf(activeT, stateTimer + 0.5f);
        vel.x = 0;
        iframes = fmaxf(iframes, 0.28f);
        fx.Text({ pos.x, pos.y - h - 16 }, GyomeiCol(), 1.18f, "STONE GUARD");
        fx.Text({ pos.x, pos.y - h + 4 }, C(230, 220, 190), 0.85f, "AXE AND FLAIL INTERCEPT");
        fx.Ring({ pos.x + facing * 64.0f, pos.y - 28.0f }, 18, 165, 430, 8, GyomeiCol());
        PlaySfx(SFX_CHAIN, 1.0f, 0.78f);
        PlaySfx(SFX_STONE, 0.72f, 0.68f);
        return;
    }

    if (justiceCd <= 0 && (targetIsBoss || crowd >= 4 || windups >= 3) && distT < 480) {
        state = GyomeiState::ArcsJustice;
        stateTimer = 1.55f;
        formHits = 0;
        tickT = 0;
        curId = cs.NewId();
        justiceCd = 26.0f - mastery.Level() * 1.2f;
        facing = targetPos.x > pos.x ? 1 : -1;
        vel.x = 0;
        fx.Text({ pos.x, pos.y - h - 16 }, GyomeiCol(), 1.15f,
                "FIFTH FORM: ARCS OF JUSTICE");
        fx.Ring(pos, 26, 310, 560, 12, GyomeiCol());
        fx.AddShake(0.38f);
        PlaySfx(SFX_CHAIN, 1.0f, 0.64f);
        PlaySfx(SFX_STONE, 0.92f, 0.58f);
        return;
    }

    if (attackTimer > 0 || (best < 0 && !targetIsBoss)) return;
    if (distT > 610) return;

    facing = targetPos.x > pos.x ? 1 : -1;

    GyomeiState form;
    if ((targetIsBoss || brutes > 0 || crowd >= 3) && upperCd <= 0)
        form = GyomeiState::UpperSmash;
    else if ((crowd >= 3 || distT > 220) && conquestCd <= 0)
        form = GyomeiState::RapidConquest;
    else if (distT > 150 && serpentCd <= 0)
        form = GyomeiState::Serpentinite;
    else
        form = GyomeiState::Combo;

    if (form == lastForm) {
        if (form == GyomeiState::Combo)
            form = (serpentCd <= 0) ? GyomeiState::Serpentinite : GyomeiState::UpperSmash;
        else if (form == GyomeiState::Serpentinite)
            form = GyomeiState::Combo;
        else if (form == GyomeiState::UpperSmash)
            form = (conquestCd <= 0) ? GyomeiState::RapidConquest : GyomeiState::Combo;
        else
            form = GyomeiState::UpperSmash;
    }
    lastForm = form;
    state = form;
    formHits = 0;
    tickT = 0;

    switch (form) {
        case GyomeiState::Serpentinite:
            stateTimer = 0.82f;
            serpentCd = 4.8f;
            curId = cs.NewId();
            fx.Text({ pos.x, pos.y - h - 14 }, GyomeiCol(), 0.88f,
                    "FIRST FORM: SERPENTINITE BIPOLAR");
            PlaySfx(SFX_CHAIN, 0.85f, 0.92f);
            break;
        case GyomeiState::UpperSmash:
            stateTimer = 0.96f;
            upperCd = 6.2f;
            fx.Text({ pos.x, pos.y - h - 14 }, GyomeiCol(), 0.9f,
                    "SECOND FORM: UPPER SMASH");
            PlaySfx(SFX_STONE, 0.6f, 0.72f);
            break;
        case GyomeiState::RapidConquest:
            stateTimer = 1.04f;
            conquestCd = 7.6f;
            fx.Text({ pos.x, pos.y - h - 14 }, GyomeiCol(), 0.86f,
                    "VOLCANIC ROCK: RAPID CONQUEST");
            PlaySfx(SFX_CHAIN, 0.9f, 0.78f);
            break;
        default:
            stateTimer = 1.10f;
            fx.Text({ pos.x, pos.y - h - 14 }, GyomeiCol(), 0.82f, "STONE COMBO");
            PlaySfx(SFX_CHAIN, 0.62f, 1.0f);
            break;
    }
    attackTimer = mastery.Cadence();
}

void Gyomei::Update(float dt, Player& player, std::vector<Enemy>& enemies,
                    Boss& boss, Akaza& akaza, UpperMoon* moon,
                    CombatSystem& cs, Effects& fx) {
    if (state == GyomeiState::Inactive) {
        summonCd = fmaxf(summonCd - dt, 0);
        return;
    }
    if (state == GyomeiState::Fallen) {
        if (stateTimer > 0) stateTimer -= dt;
        return;
    }

    hitFlash = fmaxf(hitFlash - dt, 0);
    iframes = fmaxf(iframes - dt, 0);
    dodgeT = fmaxf(dodgeT - dt, 0);
    staggerT = fmaxf(staggerT - dt, 0);
    attackTimer = fmaxf(attackTimer - dt, 0);
    serpentCd = fmaxf(serpentCd - dt, 0);
    upperCd = fmaxf(upperCd - dt, 0);
    conquestCd = fmaxf(conquestCd - dt, 0);
    justiceCd = fmaxf(justiceCd - dt, 0);
    stoneGuardCd = fmaxf(stoneGuardCd - dt, 0);

    bool ultDanger = UltimateDanger(boss, akaza, moon);
    if (ultDanger && !ultDangerLast && stoneGuardCd <= 0 &&
        state != GyomeiState::Arrive && state != GyomeiState::Withdraw) {
        state = GyomeiState::StoneGuard;
        stateTimer = 2.55f + 0.12f * mastery.Level();
        tickT = 0;
        formHits = 0;
        stoneGuardShieldMax = mastery.StoneGuardShield() * 1.18f;
        stoneGuardShield = stoneGuardShieldMax;
        stoneGuardCd = mastery.StoneGuardCd();
        facing = UltimateSourceX(boss, akaza, moon, player.pos.x) > pos.x ? 1 : -1;
        activeT = fmaxf(activeT, stateTimer + 0.5f);
        vel.x = 0;
        iframes = fmaxf(iframes, 0.35f);
        fx.Text({ pos.x, pos.y - h - 16 }, GyomeiCol(), 1.22f,
                "STONE GUARD: ULTIMATE INTERCEPT");
        fx.Ring({ pos.x + facing * 72.0f, pos.y - 26.0f }, 20, 210, 500, 10, GyomeiCol());
        fx.AddShake(0.35f);
        PlaySfx(SFX_CHAIN, 1.0f, 0.68f);
    }
    ultDangerLast = ultDanger;

    float dmgMul = mastery.DmgMult();

    switch (state) {
        case GyomeiState::Arrive: {
            stateTimer -= dt;
            float spd = 1180.0f;
            float dx = targetPos.x - pos.x;
            if (fabsf(dx) > 24) vel.x = (dx > 0 ? 1.0f : -1.0f) * spd;
            else vel.x = 0;
            ChainTrail(fx, pos, facing);
            if (stateTimer <= 0) {
                state = GyomeiState::Follow;
                vel.x = 0;
                StoneBurst(fx, { pos.x, cfg::GROUND_Y - 6 }, 1.35f);
                fx.AddShake(0.42f);
                PlaySfx(SFX_STONE, 0.9f, 0.7f);
            }
            break;
        }
        case GyomeiState::Follow: {
            if (staggerT > 0) {
                vel.x *= 1.0f - Clampf(5.0f * dt, 0, 1);
                break;
            }
            PickAction(player, enemies, boss, akaza, moon, cs, fx);
            if (state != GyomeiState::Follow) break;
            float guardSpot = player.pos.x - (targetPos.x > player.pos.x ? 1.0f : -1.0f) * 95.0f;
            float wantX = targetIsBoss ? targetPos.x - facing * 165.0f : targetPos.x - facing * 105.0f;
            if (!targetIsBoss && Dist(targetPos, player.pos) > 240.0f)
                wantX = guardSpot;
            float dx = wantX - pos.x;
            facing = (targetIsBoss ? targetPos.x : player.pos.x) > pos.x ? 1 : -1;
            if (fabsf(dx) > 28) vel.x = (dx > 0 ? 1.0f : -1.0f) * mastery.MoveSpeed();
            else vel.x *= 1.0f - Clampf(8.0f * dt, 0, 1);
            break;
        }
        case GyomeiState::Combo: {
            stateTimer -= dt;
            float elapsed = 1.10f - stateTimer;
            vel.x *= 1.0f - Clampf(6.0f * dt, 0, 1);
            if (formHits == 0 && elapsed >= 0.24f) {
                formHits = 1;
                Rectangle r = { facing > 0 ? pos.x - 16 : pos.x - 124, pos.y - 50, 140, 96 };
                cs.Add(r, 25.0f * dmgMul, facing * 310.0f, -190, 0.08f,
                       Team::Player, HitKind::Gyomei, cs.NewId());
                fx.SlashArc({ pos.x + facing * 46.0f, pos.y - 12.0f }, 78,
                            facing > 0 ? -70 : 250, facing > 0 ? 46 : 134, GyomeiCol());
                ChainTrail(fx, { pos.x + facing * 56.0f, pos.y - 10.0f }, facing);
                PlaySfx(SFX_CHAIN, 0.62f, 1.05f);
            }
            if (formHits == 1 && elapsed >= 0.58f) {
                formHits = 2;
                Rectangle r = { facing > 0 ? pos.x + 30 : pos.x - 250, pos.y - 64, 220, 112 };
                cs.Add(r, 32.0f * dmgMul, facing * 430.0f, -235, 0.08f,
                       Team::Player, HitKind::Gyomei, cs.NewId());
                fx.Ring({ pos.x + facing * 126.0f, pos.y - 12.0f }, 8, 92, 360, 5, GyomeiCol());
                ChainTrail(fx, { pos.x + facing * 135.0f, pos.y - 12.0f }, facing);
                PlaySfx(SFX_CHAIN, 0.78f, 0.82f);
            }
            if (formHits == 2 && elapsed >= 0.88f) {
                formHits = 3;
                Rectangle r = { pos.x - 140, pos.y - 56, 280, 120 };
                cs.Add(r, 40.0f * dmgMul, facing * 280.0f, -390, 0.10f,
                       Team::Player, HitKind::Gyomei, cs.NewId());
                StoneBurst(fx, { pos.x + facing * 42.0f, cfg::GROUND_Y - 4 }, 1.15f);
                fx.AddShake(0.36f);
                fx.AddHitstop(0.055f);
                PlaySfx(SFX_STONE, 0.88f, 0.78f);
            }
            if (stateTimer <= 0) state = GyomeiState::Follow;
            break;
        }
        case GyomeiState::Serpentinite: {
            stateTimer -= dt;
            float elapsed = 0.82f - stateTimer;
            vel.x *= 1.0f - Clampf(7.0f * dt, 0, 1);
            if (formHits == 0 && elapsed >= 0.34f) {
                formHits = 1;
                Rectangle front = { facing > 0 ? pos.x + 8 : pos.x - 250, pos.y - 58, 242, 116 };
                Rectangle rear  = { facing > 0 ? pos.x - 128 : pos.x - 8, pos.y - 48, 128, 94 };
                cs.Add(front, 36.0f * dmgMul, facing * 520.0f, -250, 0.10f,
                       Team::Player, HitKind::Gyomei, curId);
                cs.Add(rear, 22.0f * dmgMul, -facing * 250.0f, -180, 0.08f,
                       Team::Player, HitKind::Gyomei, curId);
                fx.Ring({ pos.x + facing * 118.0f, pos.y - 12.0f }, 12, 116, 440, 7, GyomeiCol());
                fx.Sparks({ pos.x + facing * 130.0f, pos.y - 10.0f }, facing > 0 ? 180 : 0,
                          80, 16, ChainCol(), 520, 3);
                fx.AddShake(0.22f);
                PlaySfx(SFX_CHAIN, 0.85f, 0.74f);
            }
            if (stateTimer <= 0) state = GyomeiState::Follow;
            break;
        }
        case GyomeiState::UpperSmash: {
            stateTimer -= dt;
            vel.x *= 1.0f - Clampf(8.0f * dt, 0, 1);
            if (stateTimer > 0.42f) {
                if (GetRandomValue(0, 3) == 0)
                    fx.QuakeTrail({ pos.x + frnd(-34, 34), cfg::GROUND_Y });
            } else if (formHits == 0) {
                formHits = 1;
                Rectangle r = { pos.x - 168, pos.y - 84, 336, 158 };
                cs.Add(r, 62.0f * dmgMul, facing * 470.0f, -560, 0.14f,
                       Team::Player, HitKind::Gyomei, cs.NewId());
                StoneBurst(fx, { pos.x + facing * 46.0f, cfg::GROUND_Y - 3 }, 1.55f);
                fx.Flash(C(210, 200, 185), 0.13f);
                fx.AddShake(0.60f);
                fx.AddHitstop(0.09f);
                PlaySfx(SFX_STONE, 1.0f, 0.56f);
            }
            if (stateTimer <= 0) state = GyomeiState::Follow;
            break;
        }
        case GyomeiState::RapidConquest: {
            stateTimer -= dt;
            tickT -= dt;
            vel.x *= 1.0f - Clampf(5.0f * dt, 0, 1);
            if (tickT <= 0 && formHits < 4) {
                tickT = 0.18f;
                formHits++;
                Rectangle r = { facing > 0 ? pos.x - 22 : pos.x - 292, pos.y - 68, 314, 132 };
                cs.Add(r, 22.0f * dmgMul, facing * 330.0f, -260, 0.07f,
                       Team::Player, HitKind::Gyomei, cs.NewId());
                Vector2 c = { pos.x + facing * (74.0f + formHits * 18.0f), pos.y - 16.0f };
                fx.SlashArc(c, 88 + formHits * 6.0f, -55, 55, GyomeiCol());
                fx.Ring(c, 6, 76, 420, 4, GyomeiCol());
                ChainTrail(fx, c, facing);
                PlaySfx(SFX_CHAIN, 0.58f, 0.74f + formHits * 0.06f);
            }
            if (stateTimer <= 0) {
                state = GyomeiState::Follow;
                StoneBurst(fx, { pos.x + facing * 70.0f, cfg::GROUND_Y - 4 }, 0.9f);
            }
            break;
        }
        case GyomeiState::ArcsJustice: {
            stateTimer -= dt;
            float elapsed = 1.55f - stateTimer;
            vel.x *= 1.0f - Clampf(9.0f * dt, 0, 1);
            if (GetRandomValue(0, 1) == 0)
                ChainTrail(fx, { pos.x + frnd(-160, 160), pos.y + frnd(-80, 30) }, facing);
            const float marks[5] = { 0.34f, 0.55f, 0.78f, 1.02f, 1.24f };
            if (formHits < 5 && elapsed >= marks[formHits]) {
                int hit = formHits++;
                int dir = (hit % 2 == 0) ? facing : -facing;
                float width = hit == 4 ? 760.0f : 420.0f;
                float x = hit == 4 ? pos.x - width * 0.5f
                                   : (dir > 0 ? pos.x - 40.0f : pos.x - width + 40.0f);
                Rectangle r = { x, pos.y - 110.0f, width, 198.0f };
                float dmg = (hit == 4 ? 82.0f : 38.0f) * dmgMul;
                cs.Add(r, dmg, dir * (hit == 4 ? 620.0f : 430.0f),
                       hit == 4 ? -620.0f : -330.0f, 0.12f,
                       Team::Player, HitKind::Gyomei, cs.NewId());
                Vector2 c = { pos.x + dir * (hit == 4 ? 0.0f : 170.0f), cfg::GROUND_Y - 22.0f };
                StoneBurst(fx, c, hit == 4 ? 1.8f : 1.05f);
                fx.Ring(pos, 50 + hit * 32.0f, hit == 4 ? 430.0f : 270.0f,
                        620, hit == 4 ? 14 : 9, GyomeiCol());
                fx.AddShake(hit == 4 ? 0.82f : 0.34f);
                fx.AddHitstop(hit == 4 ? 0.12f : 0.05f);
                PlaySfx(hit == 4 ? SFX_STONE : SFX_CHAIN, hit == 4 ? 1.0f : 0.82f,
                        hit == 4 ? 0.48f : 0.72f);
            }
            if (stateTimer <= 0) state = GyomeiState::Follow;
            break;
        }
        case GyomeiState::StoneGuard: {
            stateTimer -= dt;
            tickT -= dt;
            vel.x *= 1.0f - Clampf(10.0f * dt, 0, 1);
            Rectangle ward = GuardRect(player);

            int stopped = 0;
            for (auto& hb : cs.Boxes()) {
                if (hb.team != Team::Enemy || hb.life <= 0) continue;
                if (CheckCollisionRecs(hb.rect, ward)) {
                    hb.life = 0;
                    stopped += (hb.kind == HitKind::BossProjectile || hb.kind == HitKind::BossAoe) ? 7 : 3;
                    Vector2 c = { hb.rect.x + hb.rect.width * 0.5f,
                                  hb.rect.y + hb.rect.height * 0.5f };
                    fx.Sparks(c, -90, 360, 16, C(245, 240, 210), 560, 3.0f);
                    fx.QuakeTrail({ c.x, cfg::GROUND_Y });
                }
            }
            stopped += boss.NullifyCrescentsInRect(ward) * 5;
            stopped += boss.NullifyRingsInRect(ward) * 13;
            stopped += akaza.NullifyOrbsInRect(ward) * 5;
            if (moon) stopped += moon->NullifyShardsInRect(ward) * 5;

            if (stopped > 0) {
                stoneGuardShield -= (float)stopped;
                int bursts = (int)Clampf((float)stopped / 4.0f, 1.0f, 5.0f);
                for (int i = 0; i < bursts; i++) {
                    Vector2 p = { frnd(ward.x + 20.0f, ward.x + ward.width - 20.0f),
                                  frnd(ward.y + 35.0f, ward.y + ward.height - 35.0f) };
                    fx.Sparks(p, -90, 360, 14, C(245, 240, 210), 580, 3.1f);
                    fx.QuakeTrail({ p.x, cfg::GROUND_Y });
                }
                fx.AddShake(0.12f + 0.015f * fminf((float)stopped, 20.0f));
                fx.AddHitstop(0.025f);
                PlaySfx(SFX_CHAIN, 0.48f, 1.22f);
            }
            if (tickT <= 0) {
                tickT = 0.10f;
                fx.Ring({ pos.x + facing * 56.0f, pos.y - 34.0f }, 10, 92, 410, 5, GyomeiCol());
                fx.Sparks({ pos.x + facing * 66.0f, pos.y - 22.0f },
                          facing > 0 ? 180.0f : 0.0f, 80, 5, ChainCol(), 380, 2.5f);
                fx.Dust({ pos.x + frnd(-24, 24), cfg::GROUND_Y });
            }
            if (stoneGuardShield <= 0) {
                fx.Text({ pos.x, pos.y - h - 12 }, GyomeiCol(), 0.95f, "stone guard breaks");
                fx.AddShake(0.42f);
                stateTimer = 0;
            }
            if (stateTimer <= 0) state = GyomeiState::Follow;
            break;
        }
        case GyomeiState::Withdraw: {
            vel.x = facing * 820.0f;
            ChainTrail(fx, pos, facing);
            if (pos.x < -110 || pos.x > cfg::SCREEN_W + 110) {
                state = GyomeiState::Inactive;
                summonCd = 0;
                vel = { 0, 0 };
                hitMem.Clear();
            }
            break;
        }
        default: break;
    }

    if (state != GyomeiState::Arrive) vel.y += cfg::GRAVITY * dt;
    else vel.y = 0;
    pos.x += vel.x * dt;
    pos.y += vel.y * dt;
    if (state != GyomeiState::Withdraw && state != GyomeiState::Arrive)
        pos.x = Clampf(pos.x, 34.0f, (float)cfg::SCREEN_W - 34.0f);
    onGround = GroundClamp(pos, vel, h * 0.5f);
}

// ---------------------------------------------------------------- drawing

void Gyomei::Draw() const {
    if (state == GyomeiState::Inactive) return;
    float gt = (float)GetTime();

    float alpha = 1.0f;
    if (state == GyomeiState::Fallen) {
        alpha = Clampf(stateTimer / 2.4f, 0, 1);
        if (alpha <= 0.01f) return;
    }
    if (iframes > 0 && state != GyomeiState::StoneGuard)
        alpha *= fmodf(gt * 12.0f, 2.0f) < 1.0f ? 0.62f : 1.0f;

    DrawEllipse((int)pos.x, (int)(cfg::GROUND_Y + 7), 28, 7, Fade(BLACK, 0.38f * alpha));

    float walkBob = Active() && state == GyomeiState::Follow ? sinf(gt * 7.0f) * 2.0f : 0.0f;
    float bx = pos.x, by = pos.y + walkBob;
    bool kneel = (state == GyomeiState::Fallen);
    if (kneel) by += 12;
    if (dodgeT > 0) bx -= facing * 8.0f * Clampf(dodgeT / 0.28f, 0, 1);

    Color uniform = C(22, 24, 28);
    Color robe = C(72, 88, 76);
    Color sash = C(210, 196, 144);
    Color skin = C(236, 204, 176);
    Color hair = C(22, 18, 16);
    Color beads = C(115, 78, 48);
    if (hitFlash > 0) { uniform = C(255, 145, 120); robe = uniform; beads = C(255, 210, 180); }

    DrawRectangle((int)(bx - 14), (int)(by + 12), 10, kneel ? 14 : 26, Fade(uniform, alpha));
    DrawRectangle((int)(bx + 4), (int)(by + 12), 10, kneel ? 14 : 26, Fade(C(18, 19, 23), alpha));
    DrawRectangleRounded({ bx - 17, by - 28, 34, 43 }, 0.18f, 4, Fade(uniform, alpha));
    DrawTriangle({ bx - 19, by - 28 }, { bx - 46, by + 22 }, { bx - 10, by + 16 }, Fade(robe, alpha));
    DrawTriangle({ bx + 19, by - 28 }, { bx + 46, by + 22 }, { bx + 10, by + 16 }, Fade(robe, alpha));
    DrawRectangle((int)(bx - 17), (int)(by + 5), 34, 5, Fade(sash, alpha));

    Vector2 headC = { bx + facing * 2.0f, by - 41.0f };
    DrawCircleV(headC, 12.0f, Fade(skin, alpha));
    DrawCircleSector({ headC.x, headC.y - 2.0f }, 14.0f, 185, 355, 12, Fade(hair, alpha));
    DrawRectangle((int)(headC.x - 9), (int)(headC.y - 17), 18, 5, Fade(hair, alpha));
    DrawCircleV({ headC.x + facing * 4.0f, headC.y + 1.0f }, 1.6f, Fade(C(225, 225, 210), alpha));
    DrawLineEx({ headC.x + facing * 2.0f, headC.y + 4.0f },
               { headC.x + facing * 8.0f, headC.y + 12.0f }, 1.4f, Fade(C(110, 150, 210), 0.75f * alpha));

    for (int i = 0; i < 9; i++) {
        float a = (205.0f + i * 16.0f) * DEG2RAD;
        DrawCircleV({ bx + cosf(a) * 17.0f, by - 17.0f + sinf(a) * 13.0f },
                    3.1f, Fade(beads, alpha));
    }

    Vector2 hand = { bx + facing * 13.0f, by - 13.0f };
    Vector2 axe = { bx + facing * 78.0f, by - 22.0f };
    Vector2 flail = { bx - facing * 72.0f, by - 12.0f };
    float axeAng = facing > 0 ? -18.0f : 198.0f;
    switch (state) {
        case GyomeiState::Arrive:
            axe = { bx + facing * 118.0f, by - 8.0f };
            flail = { bx - facing * 94.0f, by - 36.0f };
            axeAng = facing > 0 ? 4.0f : 176.0f;
            break;
        case GyomeiState::Combo:
            axe = { bx + facing * (70.0f + 20.0f * sinf(gt * 18.0f)), by - 28.0f + 28.0f * sinf(gt * 12.0f) };
            flail = { bx + facing * (112.0f + 18.0f * sinf(gt * 20.0f)), by - 18.0f + 32.0f * cosf(gt * 18.0f) };
            axeAng = facing > 0 ? 24.0f + sinf(gt * 18.0f) * 40.0f
                                : 156.0f - sinf(gt * 18.0f) * 40.0f;
            break;
        case GyomeiState::Serpentinite:
            axe = { bx - facing * 82.0f, by - 18.0f };
            flail = { bx + facing * 154.0f, by - 20.0f + sinf(gt * 34.0f) * 24.0f };
            axeAng = facing > 0 ? 154.0f : 26.0f;
            break;
        case GyomeiState::UpperSmash:
            axe = stateTimer > 0.42f ? Vector2{ bx + facing * 24.0f, by - 98.0f }
                                     : Vector2{ bx + facing * 46.0f, cfg::GROUND_Y - 22.0f };
            flail = stateTimer > 0.42f ? Vector2{ bx - facing * 18.0f, by - 104.0f }
                                       : Vector2{ bx - facing * 35.0f, cfg::GROUND_Y - 18.0f };
            axeAng = stateTimer > 0.42f ? (facing > 0 ? -82.0f : 262.0f)
                                        : (facing > 0 ? 62.0f : 118.0f);
            break;
        case GyomeiState::RapidConquest: {
            float a = gt * 12.0f;
            axe = { bx + facing * (92.0f + cosf(a) * 55.0f), by - 20.0f + sinf(a) * 56.0f };
            flail = { bx + facing * (122.0f + cosf(a + PI) * 66.0f), by - 22.0f + sinf(a + PI) * 62.0f };
            axeAng = fmodf(gt * 720.0f, 360.0f);
            break;
        }
        case GyomeiState::ArcsJustice: {
            float a = gt * 17.0f;
            axe = { bx + cosf(a) * 170.0f, by - 18.0f + sinf(a) * 104.0f };
            flail = { bx + cosf(a + PI) * 190.0f, by - 18.0f + sinf(a + PI) * 112.0f };
            axeAng = fmodf(gt * 1080.0f, 360.0f);
            break;
        }
        case GyomeiState::StoneGuard:
            axe = { bx + facing * 78.0f, by - 88.0f + sinf(gt * 28.0f) * 14.0f };
            flail = { bx + facing * 86.0f, by + 34.0f + cosf(gt * 28.0f) * 16.0f };
            axeAng = facing > 0 ? -70.0f : 250.0f;
            break;
        case GyomeiState::Withdraw:
            axe = { bx + facing * 120.0f, by - 18.0f };
            flail = { bx - facing * 86.0f, by - 26.0f };
            axeAng = facing > 0 ? 0.0f : 180.0f;
            break;
        case GyomeiState::Fallen:
            axe = { bx + 42.0f, cfg::GROUND_Y - 14.0f };
            flail = { bx - 42.0f, cfg::GROUND_Y - 14.0f };
            axeAng = 18.0f;
            break;
        default:
            break;
    }

    DrawLineEx({ bx - facing * 9.0f, by - 21.0f }, hand, 7.0f, Fade(uniform, alpha));
    DrawLineEx({ bx + facing * 8.0f, by - 20.0f }, { bx + facing * 22.0f, by - 2.0f },
               6.0f, Fade(uniform, alpha));
    DrawChain(hand, axe, ChainCol(), alpha);
    DrawChain(hand, flail, ChainCol(), alpha);
    DrawAxe(axe, axeAng, facing, alpha);
    DrawFlail(flail, alpha);

    if (state == GyomeiState::StoneGuard) {
        float p = fmodf(gt * 1.7f, 1.0f);
        Rectangle ward = { facing > 0 ? bx + 36.0f : bx - 128.0f, cfg::GROUND_Y - 238.0f, 92, 238 };
        DrawRectangleGradientV((int)ward.x, (int)ward.y, (int)ward.width, (int)ward.height,
                               Fade(C(225, 220, 198), 0.18f),
                               Fade(C(105, 100, 92), 0.28f));
        DrawRectangleLinesEx(ward, 3, Fade(GyomeiCol(), 0.82f));
        DrawRing({ bx + facing * 72.0f, by - 24.0f }, 80 * p, 80 * p + 5,
                 0, 360, 36, Fade(GyomeiCol(), 0.55f * (1 - p)));
        if (stoneGuardShieldMax > 0) {
            float f = Clampf(stoneGuardShield / stoneGuardShieldMax, 0, 1);
            DrawRectangle((int)(pos.x - 36), (int)(pos.y - h * 0.5f - 42), 72, 5, C(24, 22, 20));
            DrawRectangle((int)(pos.x - 35), (int)(pos.y - h * 0.5f - 41),
                          (int)(70 * f), 3, GyomeiCol());
        }
    }
    if (state == GyomeiState::ArcsJustice) {
        float p = fmodf(gt * 1.4f, 1.0f);
        DrawRing(pos, 210 * p, 210 * p + 7, 0, 360, 54, Fade(GyomeiCol(), 0.55f * (1 - p)));
        DrawRing(pos, 72, 76, 0, 360, 42, Fade(C(240, 232, 205), 0.25f));
    }

    if (hp < maxHp && Active()) {
        float bw = w + 20;
        float f = Clampf(hp / maxHp, 0, 1);
        DrawRectangle((int)(pos.x - bw * 0.5f), (int)(pos.y - h * 0.5f - 17), (int)bw, 5, C(16, 18, 22));
        DrawRectangle((int)(pos.x - bw * 0.5f + 1), (int)(pos.y - h * 0.5f - 16),
                      (int)((bw - 2) * f), 3, GyomeiCol());
    }
    if (Active() && state != GyomeiState::Withdraw) {
        float f = Clampf(hp / maxHp, 0, 1);
        DrawRing({ pos.x, pos.y - h * 0.5f - 30 }, 5, 8, -90, -90 + 360 * f, 24,
                 Fade(GyomeiCol(), 0.85f));
    }
}

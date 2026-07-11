#include "rengoku.h"
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

static const char* SAVE_FILE = "rengoku_mastery.txt";
static const int XP_TH[5] = { 22, 55, 95, 150, 220 };

static Color RengokuCol() { return C(255, 150, 55); }
static Color RengokuFlameCol(int lv) {
    return lv >= 5 ? C(255, 236, 145) : (lv >= 3 ? C(255, 170, 55) : C(255, 115, 45));
}
static Color RengokuCoreCol(int lv) {
    return lv >= 4 ? C(255, 250, 205) : C(255, 215, 120);
}

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

static void FlameTrail(Effects& fx, Vector2 p, int facing) {
    fx.FireCharge(p);
    fx.Sparks({ p.x - facing * 8.0f, p.y }, facing > 0 ? 180.0f : 0.0f,
              45, 3, C(255, 210, 80), 300, 2.5f);
    if (GetRandomValue(0, 2) == 0)
        fx.Ember({ p.x + frnd(-18, 18), p.y + frnd(-30, 14) });
}

static void FlameWake(Effects& fx, Vector2 p, int facing, float scale, int lv) {
    Color flame = RengokuFlameCol(lv);
    Color core = RengokuCoreCol(lv);
    int n = (int)(4 * scale + lv);
    for (int i = 0; i < n; i++)
        fx.Ember({ p.x - facing * frnd(2, 46) * scale, p.y + frnd(-34, 18) * scale });
    fx.Sparks(p, facing > 0 ? 180.0f : 0.0f, 52, 2 + lv / 2, core,
              360.0f * scale, 2.5f * scale);
    if (GetRandomValue(0, 2) == 0)
        fx.SlashArc({ p.x + facing * 8.0f, p.y - 4.0f }, 42.0f * scale,
                    facing > 0 ? -42.0f : 222.0f,
                    facing > 0 ? 42.0f : 138.0f, flame);
}

static void FlameBurstLine(Effects& fx, Vector2 p, int facing, float len, float scale, int lv) {
    Color flame = RengokuFlameCol(lv);
    Color core = RengokuCoreCol(lv);
    int n = (int)(10 * scale + lv * 2);
    for (int i = 0; i < n; i++) {
        float t = (i + frnd(-0.2f, 0.2f)) / fmaxf((float)(n - 1), 1.0f);
        Vector2 q = { p.x + facing * len * Clampf(t, 0, 1),
                      p.y + frnd(-34, 20) * scale - sinf(t * PI) * 28.0f * scale };
        fx.Sparks(q, facing > 0 ? 0.0f : 180.0f, 70, 1,
                  (i % 3 == 0) ? core : flame, 310.0f * scale, 2.8f * scale);
        if (i % 2 == 0) fx.Ember(q);
    }
}

// ---------------------------------------------------------------- mastery

int RengokuMastery::Level() const {
    int lv = 0;
    for (int i = 0; i < 5; i++) if (xp >= XP_TH[i]) lv = i + 1;
    return lv;
}

int RengokuMastery::NextThreshold() const {
    int lv = Level();
    return lv >= 5 ? -1 : XP_TH[lv];
}

void RengokuMastery::Load() {
    if (!FileExists(SAVE_FILE)) return;
    char* txt = LoadFileText(SAVE_FILE);
    if (!txt) return;
    sscanf(txt, "%d %d %d", &xp, &summons, &kills);
    UnloadFileText(txt);
    if (xp < 0) xp = 0;
    if (xp > 999) xp = 999;
}

void RengokuMastery::Save() const {
    char buf[64];
    snprintf(buf, sizeof(buf), "%d %d %d", xp, summons, kills);
    SaveFileText(SAVE_FILE, buf);
}

// ---------------------------------------------------------------- lifecycle

void Rengoku::ResetRun() {
    state = RengokuState::Inactive;
    fallen = false;
    summonedThisRun = false;
    summonCd = 0;
    activeT = 0;
    hp = maxHp = mastery.MaxHp();
    pos = { -140, cfg::GROUND_Y - 42 };
    vel = { 0, 0 };
    hitMem.Clear();
    stateTimer = attackTimer = 0;
    blazingCd = bloomingCd = tigerCd = hazeCd = wheelCd = lotusCd = ninthCd = 0;
    hitFlash = iframes = staggerT = 0;
    flameWallShield = flameWallShieldMax = 0;
    ultDangerLast = false;
    exitDir = -1;
}

bool Rengoku::CanSummon() const {
    return !fallen && state == RengokuState::Inactive && summonCd <= 0;
}

void Rengoku::Summon(Vector2 playerPos, Effects& fx) {
    state = RengokuState::Arrive;
    stateTimer = 0.6f;
    summonedThisRun = true;
    mastery.summons++;
    maxHp = mastery.MaxHp();
    if (hp <= 0) hp = maxHp;
    hp = fminf(hp, maxHp);
    activeT = mastery.Duration();
    hitMem.Clear();
    blazingCd = bloomingCd = tigerCd = hazeCd = wheelCd = lotusCd = 0;
    ninthCd = 6.0f;
    flameWallShield = flameWallShieldMax = 0;
    ultDangerLast = false;
    attackTimer = 0.35f;
    bool fromLeft = playerPos.x > cfg::SCREEN_W * 0.5f;
    pos = { fromLeft ? -70.0f : cfg::SCREEN_W + 70.0f, cfg::GROUND_Y - h * 0.5f };
    facing = fromLeft ? 1 : -1;
    exitDir = fromLeft ? -1 : 1;
    targetPos = { playerPos.x - facing * 105.0f, playerPos.y };
    iframes = 1.0f;
    fx.Text({ playerPos.x, playerPos.y - 116 }, RengokuCol(), 1.5f, "KYOJURO RENGOKU");
    fx.Ring(playerPos, 8, 120, 460, 6, RengokuCol());
    PlaySfx(SFX_FIRE, 0.95f, 0.9f);
    PlaySfx(SFX_WHOOSH, 0.75f, 0.8f);
}

void Rengoku::BeginWithdraw(Effects& fx) {
    if (state == RengokuState::Inactive || state == RengokuState::Fallen ||
        state == RengokuState::Withdraw) return;

    state = RengokuState::Withdraw;
    stateTimer = 0;
    attackTimer = 0;
    tickT = 0;
    formHits = 0;
    curId = -1;
    flameWallShield = flameWallShieldMax = 0;
    ultDangerLast = false;
    hitMem.Clear();
    facing = exitDir;
    vel = { facing * 1200.0f, 0 };
    iframes = 0.8f;
    fx.Text({ pos.x, pos.y - h - 10 }, RengokuCol(), 0.9f, "RENGOKU WITHDRAWS");
}

Rectangle Rengoku::Rect() const {
    return { pos.x - w * 0.5f, pos.y - h * 0.5f, w, h };
}

void Rengoku::TakeDamage(float dmg, float kbx, HitKind kind, Effects& fx) {
    if (!Active() || state == RengokuState::Arrive || state == RengokuState::Withdraw) return;
    if (iframes > 0) return;

    bool bossHit = (kind == HitKind::BossDash || kind == HitKind::BossAoe ||
                    kind == HitKind::BossProjectile);
    float dodge = mastery.DodgeChance();
    if (bossHit) dodge *= 0.45f;
    if (state == RengokuState::FormBlooming && flameWallShield > 0) {
        flameWallShield -= bossHit ? 8.0f : 3.0f;
        fx.Ring(pos, 18, 92, 420, 5, RengokuCol());
        if (flameWallShield <= 0)
            fx.Text({ pos.x, pos.y - h - 12 }, RengokuCol(), 0.9f, "flame guard breaks");
        return;
    }

    if (state != RengokuState::NinthForm && frnd(0, 1) < dodge) {
        float dir = kbx >= 0 ? 1.0f : -1.0f;
        vel.x = dir * 380.0f;
        iframes = 0.32f;
        FlameTrail(fx, pos, (int)-dir);
        PlaySfx(SFX_WHOOSH, 0.35f, 1.0f);
        if (state == RengokuState::FormUnknowing || state == RengokuState::FormTiger)
            state = RengokuState::Follow;
        return;
    }

    hp -= dmg * mastery.DmgTaken();
    hitFlash = 0.15f;
    if (bossHit) {
        iframes = 0.7f;
        staggerT = 0.55f;
        state = RengokuState::Follow;
        vel.x = kbx * 1.35f;
        vel.y = -370.0f;
        fx.AddShake(0.38f);
        fx.AddHitstop(0.05f);
        fx.Ring({ pos.x, pos.y - 6 }, 8, 90, 460, 5, C(255, 120, 80));
        PlaySfx(SFX_STONE, 0.5f, 1.25f);
    } else {
        iframes = 0.48f;
        vel.x = kbx * 0.55f;
        vel.y = -160.0f;
    }
    fx.BloodSpray({ pos.x, pos.y - 8 }, kbx > 0 ? 1 : -1, bossHit ? 1.8f : 1.0f);
    fx.Text({ pos.x, pos.y - h }, C(255, 125, 90), 0.9f, "-%.0f", dmg);
    PlaySfx(SFX_HURT, 0.55f, 0.85f);

    if (hp <= 0) {
        hp = 0;
        fallen = true;
        state = RengokuState::Fallen;
        stateTimer = 2.2f;
        vel = { 0, 0 };
        fx.AddShake(0.65f);
        fx.AddHitstop(0.18f);
        fx.FireExplosion(pos);
        fx.DeathBurst(pos, RengokuCol(), 1.7f);
        fx.Text({ pos.x, pos.y - h - 20 }, C(240, 90, 70), 1.5f, "RENGOKU HAS FALLEN");
        PlaySfx(SFX_DEATH, 1.0f, 0.55f);
        mastery.Save();
    }
}

// ---------------------------------------------------------------- AI

void Rengoku::PickAction(Player& player, std::vector<Enemy>& enemies,
                         Boss& boss, Akaza& akaza, UpperMoon* moon,
                         CombatSystem& cs, Effects& fx) {
    int best = -1;
    float bestScore = -1e9f;
    int crowd = 0, windups = 0;
    for (int i = 0; i < (int)enemies.size(); i++) {
        const Enemy& e = enemies[i];
        if (!e.alive) continue;
        float dR = Dist(e.pos, pos);
        float dP = Dist(e.pos, player.pos);
        if (dR < 165) crowd++;
        if (e.Busy() && dP < 210) windups++;
        float score = 340.0f - dR * 0.35f;
        if (e.Busy() && dP < 160) score += 330;
        if (e.type == EType::Brute) score += 130;
        if (dP < 120) score += 120;
        if (score > bestScore) { bestScore = score; best = i; }
    }

    bool moonUp = moon && moon->Alive();
    targetIsBoss = (best < 0 && (boss.Alive() || akaza.Alive() || moonUp));
    if (best >= 0)          targetPos = enemies[best].pos;
    else if (akaza.Alive()) targetPos = akaza.pos;
    else if (moonUp)        targetPos = moon->pos;
    else if (targetIsBoss)  targetPos = boss.pos;
    else                    targetPos = { player.pos.x - 125.0f, player.pos.y };

    float distT = Dist(targetPos, pos);

    if (mastery.HasNinthForm() && ninthCd <= 0 &&
        (targetIsBoss || crowd >= 3 || windups >= 3) && distT < 380) {
        state = RengokuState::NinthForm;
        stateTimer = 1.05f;
        formHits = 0;
        curId = cs.NewId();
        ninthCd = 23.0f;
        facing = targetPos.x > pos.x ? 1 : -1;
        vel.x = 0;
        fx.Text({ pos.x, pos.y - h - 14 }, RengokuCol(), 1.15f, "NINTH FORM: RENGOKU");
        fx.Ring(pos, 18, 190, 560, 8, RengokuCol());
        PlaySfx(SFX_FIRE, 1.0f, 0.65f);
        return;
    }

    if (attackTimer > 0 || (best < 0 && !targetIsBoss)) return;
    if (distT > 580) return;

    facing = targetPos.x > pos.x ? 1 : -1;

    RengokuState form;
    if ((windups >= 2 || (targetIsBoss && distT < 185)) && bloomingCd <= 0)
        form = RengokuState::FormBlooming;
    else if (crowd >= 4 && wheelCd <= 0)
        form = RengokuState::FormInfernoWheel;
    else if (distT > 320 && lotusCd <= 0)
        form = RengokuState::FormCrimsonLotus;
    else if (distT > 220 && hazeCd <= 0)
        form = RengokuState::FormSolarHeat;
    else if (crowd >= 3 && tigerCd <= 0)
        form = RengokuState::FormTiger;
    else if ((targetIsBoss || crowd >= 2) && blazingCd <= 0)
        form = RengokuState::FormBlazing;
    else if (distT > 170)
        form = RengokuState::FormUnknowing;
    else
        form = RengokuState::FormRisingSun;

    if (form == lastForm) {
        if (form == RengokuState::FormRisingSun)
            form = (blazingCd <= 0) ? RengokuState::FormBlazing : RengokuState::FormUnknowing;
        else if (form == RengokuState::FormUnknowing)
            form = RengokuState::FormRisingSun;
        else if (form == RengokuState::FormBlazing)
            form = (tigerCd <= 0) ? RengokuState::FormTiger : RengokuState::FormRisingSun;
        else if (form == RengokuState::FormTiger)
            form = (wheelCd <= 0) ? RengokuState::FormInfernoWheel : RengokuState::FormBlazing;
        else if (form == RengokuState::FormSolarHeat)
            form = RengokuState::FormUnknowing;
        else if (form == RengokuState::FormInfernoWheel)
            form = (lotusCd <= 0) ? RengokuState::FormCrimsonLotus : RengokuState::FormRisingSun;
        else if (form == RengokuState::FormCrimsonLotus)
            form = RengokuState::FormSolarHeat;
        else
            form = RengokuState::FormBlazing;
    }
    lastForm = form;
    state = form;

    switch (form) {
        case RengokuState::FormUnknowing:
            stateTimer = 0.36f;
            formHits = 0;
            curId = cs.NewId();
            fx.Text({ pos.x, pos.y - h - 14 }, RengokuCol(), 0.9f, "UNKNOWING FIRE");
            PlaySfx(SFX_FIRE, 0.75f, 1.05f);
            break;
        case RengokuState::FormBlazing:
            stateTimer = 0.62f;
            formHits = 0;
            blazingCd = 5.0f;
            fx.Text({ pos.x, pos.y - h - 14 }, RengokuCol(), 0.9f, "BLAZING UNIVERSE");
            PlaySfx(SFX_FIRE, 0.85f, 0.85f);
            break;
        case RengokuState::FormBlooming:
            stateTimer = 0.92f + 0.05f * mastery.Level();
            tickT = 0;
            formHits = 0;
            bloomingCd = 8.0f;
            flameWallShieldMax = mastery.BloomingGuardShield();
            flameWallShield = flameWallShieldMax;
            iframes = fmaxf(iframes, stateTimer);
            fx.Text({ pos.x, pos.y - h - 14 }, RengokuCol(), 0.95f, "BLOOMING FLAME UNDULATION");
            fx.Ring(pos, 18, 132, 520, 7, RengokuCol());
            PlaySfx(SFX_FIRE, 0.9f, 0.95f);
            break;
        case RengokuState::FormTiger:
            stateTimer = 0.80f;
            tickT = 0;
            formHits = 0;
            tigerCd = 7.0f;
            fx.Text({ pos.x, pos.y - h - 14 }, RengokuCol(), 0.9f, "FLAME TIGER");
            PlaySfx(SFX_FIRE, 0.9f, 0.75f);
            break;
        case RengokuState::FormSolarHeat:
            stateTimer = 0.48f;
            tickT = 0;
            formHits = 0;
            curId = cs.NewId();
            hazeCd = 4.2f;
            iframes = fmaxf(iframes, 0.38f);
            fx.Text({ pos.x, pos.y - h - 14 }, RengokuCol(), 0.9f, "SOLAR HEAT HAZE");
            PlaySfx(SFX_WHOOSH, 0.8f, 1.35f);
            break;
        case RengokuState::FormInfernoWheel:
            stateTimer = 0.76f + 0.03f * mastery.Level();
            tickT = 0;
            formHits = 0;
            wheelCd = 6.5f;
            fx.Text({ pos.x, pos.y - h - 14 }, RengokuCol(), 0.9f, "INFERNO WHEEL");
            PlaySfx(SFX_FIRE, 0.85f, 1.05f);
            break;
        case RengokuState::FormCrimsonLotus:
            stateTimer = 0.68f;
            tickT = 0;
            formHits = 0;
            curId = cs.NewId();
            lotusCd = 7.5f;
            fx.Text({ pos.x, pos.y - h - 14 }, RengokuCol(), 0.9f, "CRIMSON LOTUS CREST");
            PlaySfx(SFX_FIRE, 0.9f, 0.82f);
            break;
        default:
            stateTimer = 0.50f;
            formHits = 0;
            fx.Text({ pos.x, pos.y - h - 14 }, RengokuCol(), 0.9f, "RISING SUN");
            PlaySfx(SFX_SLASH, 0.55f, 0.85f);
            break;
    }
    attackTimer = mastery.Cadence();
}

void Rengoku::Update(float dt, Player& player, std::vector<Enemy>& enemies,
                     Boss& boss, Akaza& akaza, UpperMoon* moon,
                     CombatSystem& cs, Effects& fx) {
    if (state == RengokuState::Inactive) {
        summonCd = fmaxf(summonCd - dt, 0);
        return;
    }
    if (state == RengokuState::Fallen) {
        if (stateTimer > 0) stateTimer -= dt;
        return;
    }

    hitFlash = fmaxf(hitFlash - dt, 0);
    iframes = fmaxf(iframes - dt, 0);
    staggerT = fmaxf(staggerT - dt, 0);
    attackTimer = fmaxf(attackTimer - dt, 0);
    blazingCd = fmaxf(blazingCd - dt, 0);
    bloomingCd = fmaxf(bloomingCd - dt, 0);
    tigerCd = fmaxf(tigerCd - dt, 0);
    hazeCd = fmaxf(hazeCd - dt, 0);
    wheelCd = fmaxf(wheelCd - dt, 0);
    lotusCd = fmaxf(lotusCd - dt, 0);
    ninthCd = fmaxf(ninthCd - dt, 0);

    bool ultDanger = UltimateDanger(boss, akaza, moon);
    if (ultDanger && !ultDangerLast &&
        state != RengokuState::Arrive && state != RengokuState::Withdraw) {
        state = RengokuState::FormBlooming;
        stateTimer = 2.1f + 0.12f * mastery.Level();
        tickT = 0;
        formHits = 0;
        flameWallShieldMax = mastery.BloomingGuardShield();
        flameWallShield = flameWallShieldMax;
        facing = UltimateSourceX(boss, akaza, moon, player.pos.x) > pos.x ? 1 : -1;
        activeT = fmaxf(activeT, stateTimer + 0.4f);
        vel.x = 0;
        iframes = fmaxf(iframes, 0.35f);
        fx.Text({ pos.x, pos.y - h - 16 }, RengokuCol(), 1.15f, "BLOOMING FLAME UNDULATION");
        fx.Ring(pos, 22, 170, 520, 9, RengokuCol());
        PlaySfx(SFX_FIRE, 1.0f, 0.75f);
    }
    ultDangerLast = ultDanger;

    float dmgMul = mastery.DmgMult();
    int lv = mastery.Level();
    float flair = 0.95f + 0.13f * lv;

    switch (state) {
        case RengokuState::Arrive: {
            stateTimer -= dt;
            float spd = 1500.0f;
            float dx = targetPos.x - pos.x;
            if (fabsf(dx) > 22) vel.x = (dx > 0 ? 1.0f : -1.0f) * spd;
            else vel.x = 0;
            FlameTrail(fx, pos, facing);
            if (stateTimer <= 0) {
                state = RengokuState::Follow;
                vel.x = 0;
                fx.FireExplosion({ pos.x, pos.y + h * 0.25f });
                fx.AddShake(0.22f);
            }
            break;
        }
        case RengokuState::Follow: {
            if (staggerT > 0) {
                vel.x *= 1.0f - Clampf(4.0f * dt, 0, 1);
                break;
            }
            PickAction(player, enemies, boss, akaza, moon, cs, fx);
            if (state != RengokuState::Follow) break;
            float want = targetIsBoss ? 160.0f : 90.0f;
            float dx = targetPos.x - pos.x;
            facing = dx > 0 ? 1 : -1;
            if (fabsf(dx) > want) vel.x = facing * mastery.MoveSpeed();
            else vel.x *= 1.0f - Clampf(8.0f * dt, 0, 1);
            break;
        }
        case RengokuState::FormUnknowing: {
            stateTimer -= dt;
            if (stateTimer > 0.20f) {
                vel.x *= 1.0f - Clampf(10.0f * dt, 0, 1);
            } else if (stateTimer > 0.04f) {
                vel.x = facing * 1300.0f;
                FlameWake(fx, pos, facing, 0.85f * flair, lv);
                Rectangle r = { pos.x - 72 + facing * 48, pos.y - 42, 150, 84 };
                cs.Add(r, 30.0f * dmgMul, facing * 420.0f, -210, 0.035f,
                       Team::Player, HitKind::Rengoku, curId);
                if (formHits == 0 && stateTimer < 0.15f) {
                    formHits = 1;
                    fx.SlashArc(pos, 104.0f * flair, facing > 0 ? -65.0f : 245.0f,
                                facing > 0 ? 65.0f : 115.0f, RengokuCoreCol(lv));
                    FlameBurstLine(fx, { pos.x - facing * 40.0f, pos.y + 4 }, facing,
                                   175.0f * flair, flair, lv);
                }
            } else {
                vel.x *= 1.0f - Clampf(12.0f * dt, 0, 1);
            }
            if (stateTimer <= 0) state = RengokuState::Follow;
            break;
        }
        case RengokuState::FormRisingSun: {
            stateTimer -= dt;
            vel.x *= 1.0f - Clampf(7.0f * dt, 0, 1);
            if (formHits == 0 && stateTimer <= 0.32f) {
                formHits = 1;
                Rectangle r = { pos.x - 80, pos.y - 78, 160, 132 };
                cs.Add(r, 24.0f * dmgMul, facing * 220.0f, -470, 0.08f,
                       Team::Player, HitKind::Rengoku, cs.NewId());
                fx.SlashArc({ pos.x, pos.y - 10 }, 92, facing > 0 ? 130 : 50,
                            facing > 0 ? -30 : 210, RengokuCol());
                FlameBurstLine(fx, { pos.x + facing * 8.0f, pos.y + 6.0f }, facing,
                               116.0f * flair, 0.7f * flair, lv);
                fx.Ring(pos, 16, 120, 500, 7, RengokuCol());
                PlaySfx(SFX_FIRE, 0.75f, 1.15f);
            }
            if (stateTimer <= 0) state = RengokuState::Follow;
            break;
        }
        case RengokuState::FormBlazing: {
            stateTimer -= dt;
            vel.x *= 1.0f - Clampf(6.0f * dt, 0, 1);
            FlameWake(fx, { pos.x + facing * 16.0f, pos.y - 8.0f }, facing, 0.85f * flair, lv);
            if (formHits == 0 && stateTimer <= 0.30f) {
                formHits = 1;
                Rectangle r = {
                    facing > 0 ? pos.x - 14 : pos.x - 142,
                    pos.y - 54, 156, 108
                };
                cs.Add(r, 43.0f * dmgMul, facing * 520.0f, -300, 0.09f,
                       Team::Player, HitKind::Rengoku, cs.NewId());
                Vector2 c = { pos.x + facing * 58.0f, pos.y - 8.0f };
                fx.SlashArc(c, 130.0f * flair, facing > 0 ? -140.0f : 320.0f,
                            facing > 0 ? 78.0f : 102.0f, RengokuFlameCol(lv));
                fx.FireExplosion(c);
                fx.AddShake(0.35f);
                fx.AddHitstop(0.05f);
                PlaySfx(SFX_EXPLO, 0.65f, 1.1f);
            }
            if (stateTimer <= 0) state = RengokuState::Follow;
            break;
        }
        case RengokuState::FormTiger: {
            stateTimer -= dt;
            tickT -= dt;
            if (tickT <= 0 && formHits < 3) {
                tickT = 0.18f;
                formHits++;
                vel.x = facing * 760.0f;
                FlameWake(fx, pos, facing, 0.85f * flair, lv);
                Rectangle r = {
                    facing > 0 ? pos.x - 18 : pos.x - 132,
                    pos.y - 48, 150, 96
                };
                cs.Add(r, 18.5f * dmgMul, facing * 340.0f, -210, 0.07f,
                       Team::Player, HitKind::Rengoku, cs.NewId());
                fx.SlashArc({ pos.x + facing * 38.0f, pos.y - 4.0f },
                            76 + formHits * 8.0f, -50, 55, RengokuCol());
                FlameBurstLine(fx, { pos.x, pos.y }, facing,
                               132.0f + formHits * 12.0f, 0.75f * flair, lv);
                PlaySfx(SFX_SLASH, 0.45f, 0.8f + formHits * 0.08f);
            }
            if (stateTimer <= 0) {
                state = RengokuState::Follow;
                fx.FireExplosion({ pos.x + facing * 40.0f, pos.y - 6.0f });
            }
            break;
        }
        case RengokuState::FormSolarHeat: {
            stateTimer -= dt;
            vel.x = facing * 980.0f;
            FlameWake(fx, pos, facing, 0.65f * flair, lv);
            tickT -= dt;
            if (tickT <= 0) {
                tickT = 0.09f;
                curId = cs.NewId();
                Rectangle r = { pos.x - 54.0f + facing * 34.0f, pos.y - 42.0f,
                                108.0f * flair, 84.0f };
                cs.Add(r, 13.0f * dmgMul, facing * 100.0f, -90, 0.04f,
                       Team::Player, HitKind::Rengoku, curId);
                fx.SlashArc(pos, 74.0f * flair, facing > 0 ? -32.0f : 212.0f,
                            facing > 0 ? 32.0f : 148.0f, RengokuFlameCol(lv));
            }
            if (formHits == 0 && stateTimer <= 0.18f) {
                formHits = 1;
                vel.x = facing * 620.0f;
                Rectangle r = {
                    facing > 0 ? pos.x : pos.x - 142.0f * flair,
                    pos.y - 48.0f, 142.0f * flair, 96.0f
                };
                cs.Add(r, 36.0f * dmgMul, facing * 430.0f, -200, 0.06f,
                       Team::Player, HitKind::Rengoku, cs.NewId());
                fx.FireExplosion({ pos.x + facing * 58.0f, pos.y - 4.0f });
                fx.AddHitstop(0.04f);
                PlaySfx(SFX_WHOOSH, 0.8f, 1.45f);
            }
            if (stateTimer <= 0) state = RengokuState::Follow;
            break;
        }
        case RengokuState::FormInfernoWheel: {
            stateTimer -= dt;
            tickT -= dt;
            vel.x = facing * 560.0f;
            float R = 90.0f * flair;
            if (tickT <= 0) { tickT = 0.085f; curId = cs.NewId(); }
            cs.Add({ pos.x - R, pos.y - R, R * 2.0f, R * 2.0f },
                   14.0f * dmgMul, facing * 180.0f, -280, 0.045f,
                   Team::Player, HitKind::Rengoku, curId);
            fx.SlashArc(pos, R, stateTimer * 1100.0f,
                        stateTimer * 1100.0f + 230.0f, RengokuFlameCol(lv));
            fx.Ring(pos, R * 0.58f, R * 0.98f, 390, 6, RengokuCoreCol(lv));
            FlameWake(fx, pos, facing, 0.75f * flair, lv);
            if (stateTimer <= 0) {
                Rectangle fin = { pos.x - 118.0f * flair, pos.y - 70.0f,
                                  236.0f * flair, 132.0f };
                cs.Add(fin, 30.0f * dmgMul, facing * 380.0f, -340, 0.06f,
                       Team::Player, HitKind::Rengoku, cs.NewId());
                fx.FireExplosion({ pos.x + facing * 36.0f, pos.y - 6.0f });
                fx.AddShake(0.30f);
                state = RengokuState::Follow;
            }
            break;
        }
        case RengokuState::FormCrimsonLotus: {
            stateTimer -= dt;
            vel.x *= 1.0f - Clampf(8.0f * dt, 0, 1);
            if (stateTimer > 0.48f) {
                fx.FireCharge({ pos.x + facing * 34.0f, pos.y - 12.0f });
            } else {
                float len = 270.0f * flair;
                Rectangle r = {
                    facing > 0 ? pos.x + 12.0f : pos.x - len - 12.0f,
                    pos.y - 62.0f, len, 124.0f
                };
                cs.Add(r, 11.0f * dmgMul, facing * 190.0f, -120, 0.04f,
                       Team::Player, HitKind::Rengoku, curId);
                FlameBurstLine(fx, { pos.x + facing * 14.0f, pos.y + 8.0f },
                               facing, len, 1.05f * flair, lv);
                if (formHits == 0 && stateTimer <= 0.18f) {
                    formHits = 1;
                    Vector2 c = { Clampf(pos.x + facing * len * 0.75f, 60.0f,
                                         (float)cfg::SCREEN_W - 60.0f),
                                  cfg::GROUND_Y - 36.0f };
                    cs.Add({ c.x - 90.0f * flair, c.y - 76.0f,
                             180.0f * flair, 108.0f }, 34.0f * dmgMul,
                           facing * 340.0f, -270, 0.07f, Team::Player,
                           HitKind::Rengoku, cs.NewId());
                    fx.FireExplosion(c);
                    fx.AddShake(0.34f);
                    fx.AddHitstop(0.05f);
                }
            }
            if (stateTimer <= 0) state = RengokuState::Follow;
            break;
        }
        case RengokuState::FormBlooming: {
            stateTimer -= dt;
            tickT -= dt;
            vel.x *= 1.0f - Clampf(9.0f * dt, 0, 1);

            float R = (112.0f + 7.0f * lv) * flair;
            Rectangle guard = { pos.x - R, pos.y - R, R * 2.0f, R * 2.0f };

            int stopped = 0;
            for (auto& hb : cs.Boxes()) {
                if (hb.team != Team::Enemy || hb.life <= 0) continue;
                if (CheckCollisionRecs(hb.rect, guard)) {
                    hb.life = 0;
                    stopped += 2;
                    Vector2 c = { hb.rect.x + hb.rect.width * 0.5f,
                                  hb.rect.y + hb.rect.height * 0.5f };
                    fx.FireExplosion(c);
                }
            }
            stopped += boss.NullifyCrescentsInRect(guard);
            stopped += boss.NullifyRingsInRect(guard) * 8;
            stopped += akaza.NullifyOrbsInRect(guard);
            if (moon) stopped += moon->NullifyShardsInRect(guard);

            if (stopped > 0) {
                flameWallShield -= (float)stopped;
                fx.AddShake(0.08f);
                PlaySfx(SFX_FIRE, 0.35f, 1.35f);
            }
            if (tickT <= 0) {
                tickT = 0.08f;
                curId = cs.NewId();
                cs.Add(guard, 8.5f * dmgMul, 0, -120, 0.04f,
                       Team::Player, HitKind::Rengoku, curId);
                for (int i = 0; i < 4 + lv; i++) {
                    float a = frnd(0, 360) * DEG2RAD;
                    float rr = frnd(R * 0.35f, R);
                    fx.Ember({ pos.x + cosf(a) * rr, pos.y + sinf(a) * rr * 0.75f });
                }
                fx.SlashArc(pos, R * 0.82f, stateTimer * 720.0f,
                            stateTimer * 720.0f + 170.0f, RengokuFlameCol(lv));
                fx.Ring(pos, R * 0.55f, R * 0.95f, 360, 6, RengokuCoreCol(lv));
            }
            if (flameWallShield <= 0) {
                fx.Text({ pos.x, pos.y - h - 12 }, RengokuCol(), 0.9f,
                        "flame guard breaks");
                stateTimer = 0;
            }
            if (stateTimer <= 0) state = RengokuState::Follow;
            break;
        }
        case RengokuState::NinthForm: {
            stateTimer -= dt;
            if (stateTimer > 0.72f) {
                vel.x *= 1.0f - Clampf(8.0f * dt, 0, 1);
                FlameWake(fx, pos, facing, 1.2f * flair, lv);
            } else if (stateTimer > 0.22f) {
                vel.x = facing * 1420.0f;
                FlameWake(fx, pos, facing, 1.35f * flair, lv);
                Rectangle r = { pos.x - 110 + facing * 80, pos.y - 72, 220, 144 };
                cs.Add(r, 66.0f * dmgMul, facing * 640.0f, -420, 0.04f,
                       Team::Player, HitKind::Rengoku, curId);
                if (formHits == 0 && stateTimer < 0.50f) {
                    formHits = 1;
                    Vector2 c = { pos.x + facing * 80.0f, pos.y - 8.0f };
                    fx.FireExplosion(c);
                    fx.FireExplosion({ c.x + facing * 62.0f, c.y + 8.0f });
                    fx.Ring(c, 24, 220.0f * flair, 620, 14, RengokuFlameCol(lv));
                    fx.Flash(C(255, 170, 55), 0.25f);
                    fx.AddShake(0.55f);
                    fx.AddHitstop(0.08f);
                    PlaySfx(SFX_EXPLO, 0.9f, 0.85f);
                }
            } else {
                vel.x *= 1.0f - Clampf(14.0f * dt, 0, 1);
            }
            if (stateTimer <= 0) state = RengokuState::Follow;
            break;
        }
        case RengokuState::Withdraw: {
            vel.x = facing * 1200.0f;
            FlameTrail(fx, pos, facing);
            if (pos.x < -80 || pos.x > cfg::SCREEN_W + 80) {
                state = RengokuState::Inactive;
                summonCd = 0;
                vel = { 0, 0 };
                hitMem.Clear();
            }
            break;
        }
        default: break;
    }

    if (state != RengokuState::Arrive) vel.y += cfg::GRAVITY * dt;
    else vel.y = 0;
    pos.x += vel.x * dt;
    pos.y += vel.y * dt;
    if (state != RengokuState::Withdraw && state != RengokuState::Arrive)
        pos.x = Clampf(pos.x, 32.0f, (float)cfg::SCREEN_W - 32.0f);
    onGround = GroundClamp(pos, vel, h * 0.5f);
}

// ---------------------------------------------------------------- drawing

void Rengoku::Draw() const {
    if (state == RengokuState::Inactive) return;
    float gt = (float)GetTime();
    int lv = mastery.Level();

    float alpha = 1.0f;
    if (state == RengokuState::Fallen) {
        alpha = Clampf(stateTimer / 2.2f, 0, 1);
        if (alpha <= 0.01f) return;
    }
    if (iframes > 0)
        alpha *= fmodf(gt * 13.0f, 2.0f) < 1.0f ? 0.58f : 1.0f;

    DrawEllipse((int)pos.x, (int)(cfg::GROUND_Y + 6), 22, 6, Fade(BLACK, 0.35f * alpha));

    float bx = pos.x, by = pos.y;
    bool kneel = (state == RengokuState::Fallen);
    if (kneel) by += 10;

    Color uniform = C(24, 24, 32);
    Color haori = C(245, 235, 205);
    Color flame = RengokuCol();
    Color red = C(210, 44, 36);
    Color skin = C(238, 206, 180);
    Color hair = C(245, 200, 70);
    if (hitFlash > 0) { uniform = C(255, 150, 120); haori = uniform; flame = uniform; }

    DrawRectangle((int)(bx - 10), (int)(by + 8), 8, kneel ? 12 : 23, Fade(uniform, alpha));
    DrawRectangle((int)(bx + 2), (int)(by + 8), 8, kneel ? 12 : 23, Fade(C(18, 18, 28), alpha));
    DrawRectangleRounded({ bx - 12, by - 22, 24, 32 }, 0.25f, 4, Fade(uniform, alpha));
    DrawTriangle({ bx - 14, by - 22 }, { bx - 34, by + 14 }, { bx - 8, by + 12 }, Fade(haori, alpha));
    DrawTriangle({ bx + 14, by - 22 }, { bx + 34, by + 14 }, { bx + 8, by + 12 }, Fade(haori, alpha));
    DrawTriangle({ bx - 30, by + 14 }, { bx - 18, by + 4 }, { bx - 8, by + 14 }, Fade(flame, alpha));
    DrawTriangle({ bx + 30, by + 14 }, { bx + 18, by + 4 }, { bx + 8, by + 14 }, Fade(flame, alpha));
    DrawTriangle({ bx - 25, by + 15 }, { bx - 16, by + 8 }, { bx - 10, by + 15 }, Fade(red, alpha));
    DrawTriangle({ bx + 25, by + 15 }, { bx + 16, by + 8 }, { bx + 10, by + 15 }, Fade(red, alpha));
    DrawRectangle((int)(bx - 12), (int)(by + 4), 24, 4, Fade(C(225, 222, 215), alpha));

    Vector2 headC = { bx + facing * 2.0f, by - 31 };
    DrawCircleV(headC, 9.5f, Fade(skin, alpha));
    DrawCircleSector(headC, 12.0f, 155, 390, 12, Fade(hair, alpha));
    for (int i = -2; i <= 2; i++) {
        Vector2 a = { headC.x + i * 4.0f, headC.y - 9.0f };
        Vector2 b = { a.x + facing * (10.0f + fabsf((float)i) * 2.0f), a.y - 5.0f };
        DrawLineEx(a, b, 4, Fade(i % 2 == 0 ? red : hair, alpha));
    }
    DrawCircleV({ headC.x + facing * 4.2f, headC.y + 1.0f }, 1.5f, Fade(C(95, 35, 22), alpha));

    Vector2 hand = { bx + facing * 12.0f, by - 7 };
    float ang = 36.0f;
    bool spin = false;
    switch (state) {
        case RengokuState::FormUnknowing: ang = 0; break;
        case RengokuState::FormRisingSun: ang = -65.0f; break;
        case RengokuState::FormBlazing: ang = 70.0f; break;
        case RengokuState::FormBlooming: ang = fmodf(gt * 1550.0f, 360.0f); spin = true; break;
        case RengokuState::FormTiger: ang = sinf(gt * 38.0f) * 50.0f; break;
        case RengokuState::FormSolarHeat: ang = 0; break;
        case RengokuState::FormInfernoWheel: ang = fmodf(gt * 1500.0f, 360.0f); spin = true; break;
        case RengokuState::FormCrimsonLotus: ang = stateTimer > 0.48f ? -92.0f : 14.0f; break;
        case RengokuState::NinthForm: ang = fmodf(gt * 900.0f, 360.0f); spin = true; break;
        case RengokuState::Withdraw: ang = 24.0f; break;
        case RengokuState::Fallen: ang = 80.0f; break;
        default: break;
    }
    if (facing < 0 && !spin) ang = 180.0f - ang;
    DrawLineEx({ bx + facing * 6.0f, by - 14 }, hand, 5, Fade(uniform, alpha));
    Rectangle blade = { hand.x, hand.y, 50, 4.8f };
    DrawRectanglePro(blade, { 0, 2.4f }, ang, Fade(C(255, 225, 170), alpha));
    Rectangle edge = { hand.x + 8, hand.y - 1, 40, 2.0f };
    DrawRectanglePro(edge, { 0, 1.0f }, ang, Fade(flame, alpha));
    Rectangle hilt = { hand.x, hand.y, 9, 8 };
    DrawRectanglePro(hilt, { 4.5f, 4 }, ang, Fade(C(70, 42, 32), alpha));

    float auraScale = 1.0f + 0.12f * lv;
    float pulse = 0.5f + 0.5f * sinf(gt * 16.0f);
    Color aura = RengokuFlameCol(lv);
    if (state == RengokuState::FormRisingSun) {
        DrawCircleSector({ pos.x + facing * 10.0f, pos.y - 22.0f },
                         108.0f * auraScale, facing > 0 ? 130.0f : 50.0f,
                         facing > 0 ? 275.0f : -95.0f, 28, Fade(aura, 0.20f * alpha));
    }
    if (state == RengokuState::FormBlazing || state == RengokuState::FormCrimsonLotus) {
        DrawCircleV({ pos.x + facing * 54.0f, pos.y - 20.0f },
                    30.0f + 10.0f * pulse * auraScale, Fade(aura, 0.18f * alpha));
        DrawCircleV({ pos.x + facing * 54.0f, pos.y - 20.0f },
                    14.0f + 6.0f * pulse * auraScale, Fade(C(255, 238, 170), 0.26f * alpha));
    }
    if (state == RengokuState::FormTiger) {
        DrawCircleSector({ pos.x + facing * 46.0f, pos.y - 2.0f },
                         92.0f * auraScale, facing > 0 ? -52.0f : 232.0f,
                         facing > 0 ? 58.0f : 122.0f, 26, Fade(aura, 0.22f * alpha));
    }
    if (state == RengokuState::FormInfernoWheel) {
        DrawRing(pos, 48.0f + pulse * 18.0f, 56.0f + pulse * 18.0f,
                 0, 360, 48, Fade(aura, 0.32f * alpha));
        DrawRing(pos, 88.0f * auraScale, 94.0f * auraScale, gt * 300.0f,
                 gt * 300.0f + 250.0f, 48, Fade(C(255, 230, 150), 0.24f * alpha));
    }
    if (state == RengokuState::FormUnknowing || state == RengokuState::FormSolarHeat) {
        DrawLineEx({ pos.x - facing * 64.0f, pos.y + 10.0f },
                   { pos.x + facing * 110.0f, pos.y - 6.0f },
                   8.0f * auraScale, Fade(aura, 0.18f * alpha));
    }
    if (state == RengokuState::NinthForm) {
        float p = fmodf(gt * 1.6f, 1.0f);
        DrawRing(pos, 155 * p, 155 * p + 5, 0, 360, 48,
                 Fade(RengokuFlameCol(lv), 0.55f * (1 - p)));
    }
    if (state == RengokuState::FormBlooming) {
        float pulse = 0.5f + 0.5f * sinf(gt * 16.0f);
        float scale = 1.0f + 0.12f * lv;
        DrawRing(pos, 58.0f * scale, 64.0f * scale, 0, 360, 56,
                 Fade(C(255, 235, 160), 0.34f * alpha));
        DrawRing(pos, 104.0f * scale, 111.0f * scale, gt * 260.0f,
                 gt * 260.0f + 290.0f, 64, Fade(RengokuFlameCol(lv), 0.30f * alpha));
        DrawRing(pos, 132.0f * scale + pulse * 10.0f,
                 138.0f * scale + pulse * 10.0f, 0, 360, 72,
                 Fade(RengokuCoreCol(lv), 0.16f * alpha));
        if (flameWallShieldMax > 0) {
            float f = Clampf(flameWallShield / flameWallShieldMax, 0, 1);
            DrawRectangle((int)(pos.x - 31), (int)(pos.y - h * 0.5f - 39), 62, 4, C(32, 20, 16));
            DrawRectangle((int)(pos.x - 30), (int)(pos.y - h * 0.5f - 38),
                          (int)(60 * f), 2, RengokuCol());
        }
    }

    if (hp < maxHp && Active()) {
        float bw = w + 14;
        float f = Clampf(hp / maxHp, 0, 1);
        DrawRectangle((int)(pos.x - bw * 0.5f), (int)(pos.y - h * 0.5f - 16), (int)bw, 5, C(16, 18, 26));
        DrawRectangle((int)(pos.x - bw * 0.5f + 1), (int)(pos.y - h * 0.5f - 15),
                      (int)((bw - 2) * f), 3, RengokuCol());
    }
    if (Active() && state != RengokuState::Withdraw) {
        float f = Clampf(hp / maxHp, 0, 1);
        DrawRing({ pos.x, pos.y - h * 0.5f - 27 }, 5, 8, -90, -90 + 360 * f, 24,
                 Fade(RengokuCol(), 0.85f));
    }
}

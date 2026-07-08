#include "sanemi.h"
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

static const char* SAVE_FILE = "sanemi_mastery.txt";
static const int XP_TH[5] = { 24, 58, 100, 155, 230 };

static Color SanemiCol() { return C(205, 245, 226); }
static Color WindDark()  { return C(95, 168, 138); }
static Color WindEdge()  { return C(242, 255, 248); }

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

static void WindTrail(Effects& fx, Vector2 p, int facing, float scale = 1.0f) {
    fx.WindSpiral(p);
    fx.MoonWind({ p.x - facing * 12.0f, p.y }, -facing, SanemiCol(), 0.65f * scale);
    fx.Sparks(p, facing > 0 ? 180.0f : 0.0f, 95, (int)(5 * scale), WindEdge(), 540, 2.5f);
    if (GetRandomValue(0, 1) == 0)
        fx.Dust({ p.x + frnd(-22, 22), cfg::GROUND_Y - 3 });
}

static void WindBurst(Effects& fx, Vector2 p, float scale = 1.0f) {
    fx.Ring(p, 10, 120 * scale, 560, 8, SanemiCol());
    fx.Ring(p, 4, 70 * scale, 420, 4, WindEdge());
    for (int i = 0; i < (int)(7 * scale); i++)
        fx.WindSpiral({ p.x + frnd(-45, 45) * scale, p.y + frnd(-48, 30) * scale });
    fx.Sparks(p, -90, 360, (int)(12 * scale), WindEdge(), 620, 2.8f);
}

// ---------------------------------------------------------------- mastery

int SanemiMastery::Level() const {
    int lv = 0;
    for (int i = 0; i < 5; i++) if (xp >= XP_TH[i]) lv = i + 1;
    return lv;
}

int SanemiMastery::NextThreshold() const {
    int lv = Level();
    return lv >= 5 ? -1 : XP_TH[lv];
}

void SanemiMastery::Load() {
    if (!FileExists(SAVE_FILE)) return;
    char* txt = LoadFileText(SAVE_FILE);
    if (!txt) return;
    sscanf(txt, "%d %d %d", &xp, &summons, &kills);
    UnloadFileText(txt);
    if (xp < 0) xp = 0;
    if (xp > 999) xp = 999;
}

void SanemiMastery::Save() const {
    char buf[64];
    snprintf(buf, sizeof(buf), "%d %d %d", xp, summons, kills);
    SaveFileText(SAVE_FILE, buf);
}

// ---------------------------------------------------------------- lifecycle

void Sanemi::ResetRun() {
    state = SanemiState::Inactive;
    fallen = false;
    summonedThisRun = false;
    summonCd = 0;
    activeT = 0;
    hp = maxHp = mastery.MaxHp();
    pos = { -130, cfg::GROUND_Y - 41 };
    vel = { 0, 0 };
    hitMem.Clear();
    stateTimer = attackTimer = 0;
    gustCd = stormCd = airCd = typhoonCd = barrierCd = 0;
    tickT = 0;
    curId = -1;
    formHits = 0;
    hitFlash = iframes = afterimageT = staggerT = 0;
    barrierShield = barrierShieldMax = 0;
    ultDangerLast = false;
    exitDir = -1;
}

bool Sanemi::CanSummon() const {
    return !fallen && state == SanemiState::Inactive && summonCd <= 0;
}

void Sanemi::Summon(Vector2 playerPos, Effects& fx) {
    state = SanemiState::Arrive;
    stateTimer = 0.42f;
    summonedThisRun = true;
    mastery.summons++;
    maxHp = mastery.MaxHp();
    if (hp <= 0) hp = maxHp;
    hp = fminf(hp, maxHp);
    activeT = mastery.Duration();
    hitMem.Clear();
    gustCd = stormCd = airCd = 0;
    typhoonCd = 6.5f;
    barrierCd = 3.5f;
    barrierShield = barrierShieldMax = 0;
    ultDangerLast = false;
    attackTimer = 0.08f;
    bool fromLeft = playerPos.x > cfg::SCREEN_W * 0.5f;
    pos = { fromLeft ? -72.0f : cfg::SCREEN_W + 72.0f, cfg::GROUND_Y - h * 0.5f };
    facing = fromLeft ? 1 : -1;
    exitDir = fromLeft ? -1 : 1;
    targetPos = { playerPos.x - facing * 92.0f, playerPos.y };
    iframes = 1.0f;
    afterimageT = 0.55f;
    fx.Text({ playerPos.x, playerPos.y - 126 }, SanemiCol(), 1.55f, "SANEMI SHINAZUGAWA");
    fx.Text({ playerPos.x, playerPos.y - 100 }, WindDark(), 0.95f, "WIND HASHIRA");
    fx.Ring(playerPos, 14, 180, 680, 7, SanemiCol());
    fx.Flash(SanemiCol(), 0.10f);
    PlaySfx(SFX_WIND, 1.0f, 0.82f);
    PlaySfx(SFX_WHOOSH, 0.95f, 1.42f);
}

void Sanemi::BeginWithdraw(Effects& fx) {
    if (state == SanemiState::Inactive || state == SanemiState::Fallen ||
        state == SanemiState::Withdraw) return;

    state = SanemiState::Withdraw;
    stateTimer = 0;
    attackTimer = 0;
    tickT = 0;
    curId = -1;
    formHits = 0;
    barrierShield = barrierShieldMax = 0;
    ultDangerLast = false;
    hitMem.Clear();
    facing = exitDir;
    vel = { facing * 1580.0f, 0 };
    iframes = 0.8f;
    afterimageT = 0.45f;
    fx.Text({ pos.x, pos.y - h - 10 }, SanemiCol(), 0.9f, "SANEMI WITHDRAWS");
}

Rectangle Sanemi::Rect() const {
    return { pos.x - w * 0.5f, pos.y - h * 0.5f, w, h };
}

void Sanemi::TakeDamage(float dmg, float kbx, HitKind kind, Effects& fx) {
    if (!Active() || state == SanemiState::Arrive || state == SanemiState::Withdraw) return;
    if (iframes > 0) return;

    bool bossHit = (kind == HitKind::BossDash || kind == HitKind::BossAoe ||
                    kind == HitKind::BossProjectile);
    if (state == SanemiState::WindBarrier && barrierShield > 0) {
        barrierShield -= bossHit ? dmg * 0.45f : dmg * 0.22f;
        WindBurst(fx, pos, 0.65f);
        fx.AddShake(0.14f);
        PlaySfx(SFX_WIND, 0.55f, 1.25f);
        if (barrierShield <= 0)
            fx.Text({ pos.x, pos.y - h - 12 }, SanemiCol(), 0.95f, "wind barrier breaks");
        return;
    }

    float dodge = mastery.DodgeChance();
    if (bossHit) dodge *= 0.62f;
    if (state != SanemiState::IdatenTyphoon && frnd(0, 1) < dodge) {
        float dir = kbx >= 0 ? 1.0f : -1.0f;
        vel.x = -dir * 610.0f;
        vel.y = fminf(vel.y, -140.0f);
        iframes = 0.30f;
        afterimageT = 0.28f;
        WindTrail(fx, pos, (int)dir, 0.9f);
        fx.Ring(pos, 8, 76, 480, 4, SanemiCol());
        PlaySfx(SFX_WHOOSH, 0.58f, 1.55f);
        if (state != SanemiState::Follow && state != SanemiState::WindBarrier)
            state = SanemiState::Follow;
        return;
    }

    hp -= dmg * mastery.DmgTaken();
    hitFlash = 0.13f;
    if (bossHit) {
        iframes = 0.58f;
        staggerT = 0.34f;
        state = SanemiState::Follow;
        vel.x = kbx * 0.95f;
        vel.y = -320.0f;
        fx.AddShake(0.32f);
        fx.AddHitstop(0.045f);
        fx.Ring({ pos.x, pos.y - 8 }, 8, 88, 480, 5, SanemiCol());
        PlaySfx(SFX_WIND, 0.52f, 1.05f);
    } else {
        iframes = 0.42f;
        vel.x = kbx * 0.50f;
        vel.y = -150.0f;
    }
    fx.BloodSpray({ pos.x, pos.y - 8 }, kbx > 0 ? 1 : -1, bossHit ? 1.45f : 0.95f);
    fx.Text({ pos.x, pos.y - h }, C(255, 115, 95), 0.9f, "-%.0f", dmg);
    PlaySfx(SFX_HURT, 0.48f, 1.1f);

    if (hp <= 0) {
        hp = 0;
        fallen = true;
        state = SanemiState::Fallen;
        stateTimer = 2.2f;
        vel = { 0, 0 };
        fx.AddShake(0.70f);
        fx.AddHitstop(0.16f);
        WindBurst(fx, pos, 1.45f);
        fx.DeathBurst(pos, SanemiCol(), 1.55f);
        fx.Text({ pos.x, pos.y - h - 20 }, C(230, 75, 70), 1.5f, "SANEMI HAS FALLEN");
        PlaySfx(SFX_DEATH, 1.0f, 0.62f);
        mastery.Save();
    }
}

// ---------------------------------------------------------------- AI

void Sanemi::PickAction(Player& player, std::vector<Enemy>& enemies,
                        Boss& boss, Akaza& akaza, UpperMoon* moon,
                        CombatSystem& cs, Effects& fx) {
    int best = -1;
    float bestScore = -1e9f;
    int crowd = 0, wideCrowd = 0, windups = 0;
    for (int i = 0; i < (int)enemies.size(); i++) {
        const Enemy& e = enemies[i];
        if (!e.alive) continue;
        float dS = Dist(e.pos, pos);
        float dP = Dist(e.pos, player.pos);
        if (dS < 170) crowd++;
        if (dS < 330) wideCrowd++;
        if (e.Busy()) windups++;
        float score = 360.0f - dS * 0.24f;
        if (dP < 150) score += 130;
        if (e.Busy()) score += 190;
        if (e.type == EType::Fast) score += 95;
        if (wideCrowd >= 3) score += 70;
        if (score > bestScore) { bestScore = score; best = i; }
    }

    bool moonUp = moon && moon->Alive();
    Vector2 bossPos = {};
    bool bossUp = false;
    float bossScore = -1e9f;
    if (akaza.Alive()) { bossPos = akaza.pos; bossUp = true; bossScore = 490.0f - Dist(akaza.pos, pos) * 0.18f; }
    if (moonUp) {
        float s = 520.0f - Dist(moon->pos, pos) * 0.16f;
        if (!bossUp || s > bossScore) { bossPos = moon->pos; bossUp = true; bossScore = s; }
    }
    if (boss.Alive()) {
        float s = 540.0f - Dist(boss.pos, pos) * 0.14f;
        if (!bossUp || s > bossScore) { bossPos = boss.pos; bossUp = true; bossScore = s; }
    }

    if (bossUp && (best < 0 || bossScore > bestScore - (wideCrowd >= 3 ? 160.0f : 40.0f))) {
        targetPos = bossPos;
        targetIsBoss = true;
    } else if (best >= 0) {
        targetPos = enemies[best].pos;
        targetIsBoss = false;
    } else {
        targetPos = { player.pos.x + (pos.x < player.pos.x ? -82.0f : 82.0f), player.pos.y };
        targetIsBoss = false;
    }

    float distT = Dist(targetPos, pos);

    bool bossThreat = boss.Alive() &&
        (boss.state == BState::Desperation || boss.state == BState::WhipStorm ||
         boss.CrescentsNear(player.pos, 275) >= 2 || boss.CrescentsNear(pos, 260) >= 2);
    bool akazaThreat = akaza.Alive() &&
        (akaza.state == AkState::Desperation || akaza.OrbsNear(player.pos, 270) >= 2 ||
         akaza.OrbsNear(pos, 250) >= 2);
    bool moonThreat = moon && moon->Menacing(pos, player.pos);
    if (barrierCd <= 0 && (bossThreat || akazaThreat || moonThreat || windups >= 4)) {
        state = SanemiState::WindBarrier;
        stateTimer = 1.70f + 0.09f * mastery.Level();
        tickT = 0;
        formHits = 0;
        barrierShieldMax = mastery.BarrierShield();
        barrierShield = barrierShieldMax;
        barrierCd = mastery.BarrierCd();
        facing = UltimateSourceX(boss, akaza, moon, targetPos.x) > pos.x ? 1 : -1;
        activeT = fmaxf(activeT, stateTimer + 0.35f);
        vel.x = 0;
        iframes = fmaxf(iframes, 0.22f);
        fx.Text({ pos.x, pos.y - h - 16 }, SanemiCol(), 1.12f, "WIND BARRIER");
        fx.Ring(pos, 18, 190, 720, 8, SanemiCol());
        PlaySfx(SFX_WIND, 1.0f, 0.72f);
        return;
    }

    if (mastery.HasIdatenTyphoon() && typhoonCd <= 0 &&
        (targetIsBoss || wideCrowd >= 4 || windups >= 3) && distT < 560.0f) {
        state = SanemiState::IdatenTyphoon;
        stateTimer = 1.34f;
        tickT = 0;
        formHits = 0;
        curId = cs.NewId();
        typhoonCd = 21.0f - mastery.Level();
        facing = targetPos.x > pos.x ? 1 : -1;
        vel.x = 0;
        fx.Text({ pos.x, pos.y - h - 16 }, SanemiCol(), 1.18f, "NINTH FORM: IDATEN TYPHOON");
        fx.Ring(pos, 26, 300, 800, 11, SanemiCol());
        fx.Flash(SanemiCol(), 0.18f);
        PlaySfx(SFX_WIND, 1.0f, 0.56f);
        return;
    }

    if (attackTimer > 0 || (best < 0 && !targetIsBoss)) return;
    if (distT > 680) return;

    facing = targetPos.x > pos.x ? 1 : -1;
    SanemiState form;
    if ((wideCrowd >= 3 || targetIsBoss) && stormCd <= 0 && distT < 280)
        form = SanemiState::RisingDustStorm;
    else if ((distT > 240 || targetIsBoss) && gustCd <= 0)
        form = SanemiState::DustWhirlwind;
    else if (airCd <= 0 && (crowd >= 2 || distT > 130))
        form = SanemiState::ColdMountainWind;
    else if ((wideCrowd >= 2 || distT > 150) && gustCd <= 0)
        form = SanemiState::GaleSlash;
    else
        form = SanemiState::LightCombo;

    if (form == lastForm) {
        if (form == SanemiState::LightCombo)
            form = (gustCd <= 0) ? SanemiState::GaleSlash : SanemiState::DustWhirlwind;
        else if (form == SanemiState::DustWhirlwind)
            form = (airCd <= 0) ? SanemiState::ColdMountainWind : SanemiState::LightCombo;
        else if (form == SanemiState::GaleSlash)
            form = SanemiState::LightCombo;
        else
            form = SanemiState::LightCombo;
    }
    lastForm = form;
    state = form;
    formHits = 0;
    tickT = 0;

    switch (form) {
        case SanemiState::DustWhirlwind:
            stateTimer = 0.44f;
            gustCd = 2.35f;
            curId = cs.NewId();
            fx.Text({ pos.x, pos.y - h - 12 }, SanemiCol(), 0.82f, "FIRST FORM: DUST WHIRLWIND CUTTER");
            PlaySfx(SFX_WIND, 0.82f, 1.05f);
            break;
        case SanemiState::GaleSlash:
            stateTimer = 0.52f;
            gustCd = 2.65f;
            curId = cs.NewId();
            fx.Text({ pos.x, pos.y - h - 12 }, SanemiCol(), 0.82f, "SEVENTH FORM: GALE, SUDDEN GUSTS");
            PlaySfx(SFX_SLASH, 0.65f, 1.35f);
            break;
        case SanemiState::RisingDustStorm:
            stateTimer = 0.68f;
            stormCd = 5.8f;
            fx.Text({ pos.x, pos.y - h - 12 }, SanemiCol(), 0.90f, "FOURTH FORM: RISING DUST STORM");
            PlaySfx(SFX_WIND, 0.95f, 0.78f);
            break;
        case SanemiState::ColdMountainWind:
            stateTimer = 0.74f;
            airCd = 3.9f;
            curId = cs.NewId();
            vel.x = facing * 620.0f;
            vel.y = -510.0f;
            onGround = false;
            fx.Text({ pos.x, pos.y - h - 12 }, SanemiCol(), 0.82f, "FIFTH FORM: COLD MOUNTAIN WIND");
            PlaySfx(SFX_WHOOSH, 0.75f, 1.42f);
            break;
        default:
            stateTimer = 0.58f;
            fx.Text({ pos.x, pos.y - h - 12 }, SanemiCol(), 0.78f, "WIND HASHIRA COMBO");
            PlaySfx(SFX_SLASH, 0.55f, 1.45f);
            break;
    }
    attackTimer = mastery.Cadence();
}

void Sanemi::Update(float dt, Player& player, std::vector<Enemy>& enemies,
                    Boss& boss, Akaza& akaza, UpperMoon* moon,
                    CombatSystem& cs, Effects& fx) {
    if (state == SanemiState::Inactive) {
        summonCd = fmaxf(summonCd - dt, 0);
        return;
    }
    if (state == SanemiState::Fallen) {
        if (stateTimer > 0) stateTimer -= dt;
        return;
    }

    hitFlash = fmaxf(hitFlash - dt, 0);
    iframes = fmaxf(iframes - dt, 0);
    afterimageT = fmaxf(afterimageT - dt, 0);
    staggerT = fmaxf(staggerT - dt, 0);
    attackTimer = fmaxf(attackTimer - dt, 0);
    gustCd = fmaxf(gustCd - dt, 0);
    stormCd = fmaxf(stormCd - dt, 0);
    airCd = fmaxf(airCd - dt, 0);
    typhoonCd = fmaxf(typhoonCd - dt, 0);
    barrierCd = fmaxf(barrierCd - dt, 0);

    bool ultDanger = UltimateDanger(boss, akaza, moon);
    if (ultDanger && !ultDangerLast && barrierCd <= 0 &&
        state != SanemiState::Arrive && state != SanemiState::Withdraw) {
        state = SanemiState::WindBarrier;
        stateTimer = 1.95f + 0.10f * mastery.Level();
        tickT = 0;
        formHits = 0;
        barrierShieldMax = mastery.BarrierShield() * 1.25f;
        barrierShield = barrierShieldMax;
        barrierCd = mastery.BarrierCd();
        facing = UltimateSourceX(boss, akaza, moon, player.pos.x) > pos.x ? 1 : -1;
        activeT = fmaxf(activeT, stateTimer + 0.4f);
        vel.x = 0;
        iframes = fmaxf(iframes, 0.30f);
        fx.Text({ pos.x, pos.y - h - 16 }, SanemiCol(), 1.18f,
                "WIND BARRIER: ULTIMATE GUARD");
        fx.Ring(pos, 22, 225, 780, 10, SanemiCol());
        PlaySfx(SFX_WIND, 1.0f, 0.62f);
    }
    ultDangerLast = ultDanger;

    float dmgMul = mastery.DmgMult();

    switch (state) {
        case SanemiState::Arrive: {
            stateTimer -= dt;
            float spd = 1880.0f;
            float dx = targetPos.x - pos.x;
            if (fabsf(dx) > 16) vel.x = (dx > 0 ? 1.0f : -1.0f) * spd;
            else vel.x = 0;
            WindTrail(fx, pos, facing, 1.1f);
            if (stateTimer <= 0) {
                state = SanemiState::Follow;
                vel.x = 0;
                WindBurst(fx, { pos.x, pos.y - 8 }, 1.05f);
                fx.AddShake(0.30f);
            }
            break;
        }
        case SanemiState::Follow: {
            if (staggerT > 0) {
                vel.x *= 1.0f - Clampf(6.5f * dt, 0, 1);
                break;
            }
            PickAction(player, enemies, boss, akaza, moon, cs, fx);
            if (state != SanemiState::Follow) break;
            float orbit = targetIsBoss ? 82.0f : 48.0f;
            float weave = sinf((float)GetTime() * 11.0f) * 38.0f;
            float wantX = targetPos.x - facing * orbit + weave;
            float dx = wantX - pos.x;
            facing = targetPos.x > pos.x ? 1 : -1;
            if (fabsf(dx) > 14) {
                vel.x = (dx > 0 ? 1.0f : -1.0f) * mastery.MoveSpeed();
                if (GetRandomValue(0, 1) == 0) WindTrail(fx, pos, facing, 0.75f);
            } else {
                vel.x = facing * 80.0f;
            }
            break;
        }
        case SanemiState::LightCombo: {
            stateTimer -= dt;
            float elapsed = 0.58f - stateTimer;
            vel.x = facing * (240.0f + 100.0f * sinf(elapsed * 30.0f));
            const float marks[5] = { 0.045f, 0.145f, 0.255f, 0.370f, 0.500f };
            if (formHits < 5 && elapsed >= marks[formHits]) {
                int hit = formHits++;
                Rectangle r = { facing > 0 ? pos.x - 10 : pos.x - 122, pos.y - 50, 132, 98 };
                cs.Add(r, (9.0f + hit * 2.6f) * dmgMul, facing * (230.0f + hit * 70.0f),
                       -135.0f - hit * 24.0f, 0.050f, Team::Player, HitKind::Sanemi, cs.NewId());
                Vector2 c = { pos.x + facing * (42.0f + hit * 10.0f), pos.y - 8.0f };
                fx.SlashArc(c, 62 + hit * 8.0f, facing > 0 ? -74 : 254,
                            facing > 0 ? 60 : 120, hit % 2 ? WindDark() : SanemiCol());
                WindTrail(fx, c, facing, 0.55f);
                if (hit == 4) {
                    WindBurst(fx, { pos.x + facing * 82.0f, pos.y - 10.0f }, 0.60f);
                    fx.AddShake(0.20f);
                }
                PlaySfx(hit == 4 ? SFX_WIND : SFX_SLASH, 0.52f, 1.30f + hit * 0.09f);
            }
            if (stateTimer <= 0) state = SanemiState::Follow;
            break;
        }
        case SanemiState::DustWhirlwind: {
            stateTimer -= dt;
            vel.x = facing * 1420.0f;
            WindTrail(fx, pos, facing, 1.05f);
            Rectangle r = { pos.x - 78 + facing * 54, pos.y - 58, 156, 116 };
            cs.Add(r, 9.5f * dmgMul, facing * 255.0f, -145, 0.035f,
                   Team::Player, HitKind::Sanemi, curId);
            tickT -= dt;
            if (tickT <= 0) {
                tickT = 0.105f;
                formHits++;
                fx.SlashArc({ pos.x + facing * 50.0f, pos.y - 12.0f + frnd(-22, 22) },
                            82 + formHits * 4.0f, facing > 0 ? -60 : 240,
                            facing > 0 ? 52 : 128, SanemiCol());
                PlaySfx(SFX_WIND, 0.38f, 1.10f + formHits * 0.05f);
            }
            if (stateTimer <= 0) {
                state = SanemiState::Follow;
                vel.x *= 0.20f;
            }
            break;
        }
        case SanemiState::GaleSlash: {
            stateTimer -= dt;
            float elapsed = 0.52f - stateTimer;
            vel.x *= 1.0f - Clampf(7.0f * dt, 0, 1);
            if (formHits == 0 && elapsed >= 0.12f) {
                formHits = 1;
                Rectangle r = { facing > 0 ? pos.x - 8 : pos.x - 354, pos.y - 70, 362, 138 };
                cs.Add(r, 25.0f * dmgMul, facing * 430.0f, -270, 0.12f,
                       Team::Player, HitKind::Sanemi, curId);
                fx.MoonWind({ pos.x + facing * 150.0f, pos.y - 10.0f }, facing, SanemiCol(), 1.25f);
                fx.SlashArc({ pos.x + facing * 160.0f, pos.y - 8.0f }, 152,
                            facing > 0 ? -44 : 224, facing > 0 ? 48 : 136, SanemiCol());
                fx.Ring({ pos.x + facing * 154.0f, pos.y - 8.0f }, 10, 116, 500, 5, WindDark());
                PlaySfx(SFX_WIND, 0.78f, 1.10f);
            }
            if (formHits == 1 && elapsed >= 0.34f) {
                formHits = 2;
                Rectangle r = { facing > 0 ? pos.x - 80 : pos.x - 230, pos.y - 46, 310, 92 };
                cs.Add(r, 17.0f * dmgMul, facing * 300.0f, -160, 0.08f,
                       Team::Player, HitKind::Sanemi, cs.NewId());
                fx.MoonWind({ pos.x + facing * 90.0f, pos.y - 20.0f }, facing, WindEdge(), 0.9f);
            }
            if (stateTimer <= 0) state = SanemiState::Follow;
            break;
        }
        case SanemiState::RisingDustStorm: {
            stateTimer -= dt;
            vel.x *= 1.0f - Clampf(6.5f * dt, 0, 1);
            float elapsed = 0.68f - stateTimer;
            if (formHits == 0 && elapsed >= 0.18f) {
                formHits = 1;
                Rectangle r = { pos.x - 200, pos.y - 100, 400, 186 };
                cs.Add(r, 43.0f * dmgMul, facing * 430.0f, -470, 0.15f,
                       Team::Player, HitKind::Sanemi, cs.NewId());
                for (int i = 0; i < 5; i++) {
                    Vector2 p = { pos.x + frnd(-170, 170), pos.y + frnd(-78, 42) };
                    fx.SlashArc(p, 84 + i * 11.0f, -80 + i * 26.0f, 70 + i * 26.0f, SanemiCol());
                    fx.WindSpiral(p);
                }
                WindBurst(fx, { pos.x, pos.y - 8 }, 1.28f);
                fx.AddShake(0.58f);
                fx.AddHitstop(0.07f);
                PlaySfx(SFX_WIND, 1.0f, 0.72f);
            }
            if (stateTimer <= 0) state = SanemiState::Follow;
            break;
        }
        case SanemiState::ColdMountainWind: {
            stateTimer -= dt;
            WindTrail(fx, pos, facing, 0.95f);
            Rectangle r = { pos.x - 90, pos.y - 86, 180, 160 };
            cs.Add(r, 14.0f * dmgMul, facing * 230.0f, -360, 0.035f,
                   Team::Player, HitKind::Sanemi, curId);
            tickT -= dt;
            if (tickT <= 0) {
                tickT = 0.13f;
                fx.SlashArc(pos, 100 + frnd(-8, 12), 0, 360, SanemiCol());
                PlaySfx(SFX_SLASH, 0.38f, 1.45f);
            }
            if ((onGround && stateTimer < 0.42f) || stateTimer <= 0) {
                state = SanemiState::Follow;
                WindBurst(fx, { pos.x + facing * 30.0f, cfg::GROUND_Y - 24.0f }, 0.85f);
                fx.AddShake(0.24f);
            }
            break;
        }
        case SanemiState::IdatenTyphoon: {
            stateTimer -= dt;
            tickT -= dt;
            float elapsed = 1.34f - stateTimer;
            if (tickT <= 0 && formHits < 9) {
                tickT = 0.085f;
                int hit = formHits++;
                int dir = (hit % 2 == 0) ? facing : -facing;
                vel.x = dir * 760.0f;
                Rectangle r = { pos.x - 280, pos.y - 130, 560, 240 };
                cs.Add(r, 13.5f * dmgMul, dir * 280.0f, -240, 0.055f,
                       Team::Player, HitKind::Sanemi, cs.NewId());
                Vector2 p = { pos.x + dir * frnd(42, 220), pos.y + frnd(-92, 42) };
                fx.SlashArc(p, 96 + hit * 4.0f, -70, 70, hit % 2 ? WindDark() : SanemiCol());
                WindTrail(fx, p, dir, 0.7f);
                PlaySfx(SFX_WIND, 0.48f, 0.95f + hit * 0.045f);
            }
            if (elapsed >= 1.04f && formHits < 13) {
                formHits = 13;
                Rectangle r = { pos.x - 430, pos.y - 165, 860, 300 };
                cs.Add(r, 58.0f * dmgMul, 0, -620, 0.18f,
                       Team::Player, HitKind::Sanemi, cs.NewId());
                for (int i = 0; i < 7; i++) {
                    Vector2 p = { pos.x + frnd(-360, 360), pos.y + frnd(-125, 82) };
                    WindBurst(fx, p, 0.75f);
                }
                fx.Ring(pos, 40, 430, 830, 15, SanemiCol());
                fx.Flash(WindEdge(), 0.24f);
                fx.AddShake(0.92f);
                fx.AddHitstop(0.11f);
                PlaySfx(SFX_EXPLO, 0.85f, 0.82f);
                PlaySfx(SFX_WIND, 1.0f, 0.52f);
            }
            if (stateTimer <= 0) state = SanemiState::Follow;
            break;
        }
        case SanemiState::WindBarrier: {
            stateTimer -= dt;
            tickT -= dt;
            vel.x *= 1.0f - Clampf(10.0f * dt, 0, 1);
            float r = 165.0f + 11.0f * mastery.Level();
            float allyR = 118.0f + 7.0f * mastery.Level();
            int stopped = 0;
            for (auto& hb : cs.Boxes()) {
                if (hb.team != Team::Enemy || hb.life <= 0) continue;
                Vector2 c = { hb.rect.x + hb.rect.width * 0.5f,
                              hb.rect.y + hb.rect.height * 0.5f };
                if (Dist(c, pos) < r || Dist(c, player.pos) < allyR) {
                    hb.life = 0;
                    stopped += (hb.kind == HitKind::BossProjectile || hb.kind == HitKind::BossAoe) ? 6 : 3;
                    WindBurst(fx, c, 0.50f);
                }
            }
            stopped += boss.NullifyCrescents(pos, r) * 5;
            stopped += boss.NullifyCrescents(player.pos, allyR) * 4;
            stopped += boss.NullifyRings(pos, r) * 13;
            stopped += boss.NullifyRings(player.pos, allyR) * 11;
            stopped += akaza.NullifyOrbs(pos, r) * 5;
            stopped += akaza.NullifyOrbs(player.pos, allyR) * 4;
            if (moon) {
                stopped += moon->NullifyShards(pos, r) * 5;
                stopped += moon->NullifyShards(player.pos, allyR) * 4;
            }
            if (stopped > 0) {
                barrierShield -= (float)stopped;
                fx.AddShake(0.12f + 0.012f * fminf((float)stopped, 20.0f));
                fx.AddHitstop(0.02f);
                PlaySfx(SFX_WIND, 0.60f, 1.18f);
            }
            if (tickT <= 0) {
                tickT = 0.075f;
                curId = cs.NewId();
                Rectangle zone = { pos.x - r, pos.y - r * 0.58f, r * 2.0f, r * 1.18f };
                cs.Add(zone, 4.5f * dmgMul, 0, -90, 0.035f,
                       Team::Player, HitKind::Sanemi, curId);
                fx.SlashArc(pos, r * 0.88f, 0, 360, SanemiCol());
                fx.Ring(pos, 22, r, 760, 5, SanemiCol());
                WindTrail(fx, { pos.x + frnd(-r * 0.65f, r * 0.65f),
                                pos.y + frnd(-r * 0.45f, r * 0.45f) }, facing, 0.7f);
            }
            if (barrierShield <= 0) {
                fx.Text({ pos.x, pos.y - h - 12 }, SanemiCol(), 0.95f, "wind barrier breaks");
                WindBurst(fx, pos, 0.95f);
                stateTimer = 0;
            }
            if (stateTimer <= 0) state = SanemiState::Follow;
            break;
        }
        case SanemiState::Withdraw: {
            vel.x = facing * 1580.0f;
            WindTrail(fx, pos, facing, 1.0f);
            if (pos.x < -95 || pos.x > cfg::SCREEN_W + 95) {
                state = SanemiState::Inactive;
                summonCd = 0;
                vel = { 0, 0 };
                hitMem.Clear();
            }
            break;
        }
        default: break;
    }

    if (state != SanemiState::Arrive) vel.y += cfg::GRAVITY * dt;
    else vel.y = 0;
    pos.x += vel.x * dt;
    pos.y += vel.y * dt;
    if (state != SanemiState::Withdraw && state != SanemiState::Arrive)
        pos.x = Clampf(pos.x, 28.0f, (float)cfg::SCREEN_W - 28.0f);
    onGround = GroundClamp(pos, vel, h * 0.5f);
}

// ---------------------------------------------------------------- drawing

void Sanemi::Draw() const {
    if (state == SanemiState::Inactive) return;
    float gt = (float)GetTime();

    float alpha = 1.0f;
    if (state == SanemiState::Fallen) {
        alpha = Clampf(stateTimer / 2.2f, 0, 1);
        if (alpha <= 0.01f) return;
    }
    if (iframes > 0 && state != SanemiState::WindBarrier)
        alpha *= fmodf(gt * 18.0f, 2.0f) < 1.0f ? 0.55f : 1.0f;

    DrawEllipse((int)pos.x, (int)(cfg::GROUND_Y + 6), 23, 6, Fade(BLACK, 0.35f * alpha));
    if (afterimageT > 0 || fabsf(vel.x) > 560.0f) {
        float a = Clampf(afterimageT / 0.55f, 0, 1);
        for (int i = 1; i <= 4; i++)
            DrawEllipse((int)(pos.x - facing * i * 24.0f), (int)(cfg::GROUND_Y + 6),
                        17, 5, Fade(SanemiCol(), (0.09f + 0.02f * i) * fmaxf(a, 0.45f)));
    }

    float bx = pos.x, by = pos.y;
    bool kneel = (state == SanemiState::Fallen);
    if (kneel) by += 10;
    float bob = (state == SanemiState::Follow) ? sinf(gt * 18.0f) * 2.0f : 0.0f;
    by += bob;

    Color uniform = C(25, 28, 34);
    Color haori = C(205, 224, 194);
    Color haoriDark = C(64, 126, 90);
    Color skin = C(236, 204, 176);
    Color hair = C(236, 238, 232);
    if (hitFlash > 0) { uniform = C(255, 150, 120); haori = uniform; haoriDark = C(255, 190, 170); }

    DrawRectangle((int)(bx - 11), (int)(by + 8), 8, kneel ? 12 : 24, Fade(uniform, alpha));
    DrawRectangle((int)(bx + 3), (int)(by + 8), 8, kneel ? 12 : 24, Fade(C(18, 20, 26), alpha));
    DrawRectangleRounded({ bx - 14, by - 24, 28, 34 }, 0.22f, 4, Fade(uniform, alpha));
    DrawTriangle({ bx - 14, by - 23 }, { bx - 34, by + 18 }, { bx - 7, by + 13 }, Fade(haori, alpha));
    DrawTriangle({ bx + 14, by - 23 }, { bx + 34, by + 18 }, { bx + 7, by + 13 }, Fade(haori, alpha));
    DrawLineEx({ bx - 28, by + 12 }, { bx - 10, by - 8 }, 3, Fade(haoriDark, alpha));
    DrawLineEx({ bx + 28, by + 12 }, { bx + 10, by - 8 }, 3, Fade(haoriDark, alpha));
    DrawRectangle((int)(bx - 12), (int)(by + 4), 24, 4, Fade(C(225, 222, 215), alpha));
    DrawLineEx({ bx - 6, by - 21 }, { bx - 11, by + 2 }, 2.0f, Fade(C(140, 48, 46), alpha));
    DrawLineEx({ bx + 7, by - 20 }, { bx + 3, by + 2 }, 2.0f, Fade(C(140, 48, 46), alpha));

    Vector2 headC = { bx + facing * 2.0f, by - 34.0f };
    DrawCircleV(headC, 10.2f, Fade(skin, alpha));
    for (int i = -3; i <= 3; i++) {
        float hx = headC.x + i * 4.2f;
        DrawTriangle({ hx, headC.y - 19.0f - fabsf((float)i) },
                     { hx - 4.5f, headC.y - 6.0f },
                     { hx + 4.5f, headC.y - 6.0f }, Fade(hair, alpha));
    }
    DrawCircleSector(headC, 11.5f, 165, 380, 14, Fade(hair, alpha));
    DrawLineEx({ headC.x - facing * 5.0f, headC.y - 5.0f },
               { headC.x + facing * 5.0f, headC.y + 3.0f }, 1.4f, Fade(C(160, 55, 55), alpha));
    DrawCircleV({ headC.x + facing * 4.2f, headC.y + 1.0f }, 1.6f, Fade(C(115, 90, 70), alpha));

    Vector2 hand = { bx + facing * 13.0f, by - 7.0f };
    float ang = 28.0f;
    bool spin = false;
    switch (state) {
        case SanemiState::Arrive:
        case SanemiState::Withdraw:
        case SanemiState::DustWhirlwind:
            ang = facing > 0 ? 0.0f : 180.0f;
            break;
        case SanemiState::LightCombo:
            ang = sinf(gt * 52.0f) * 74.0f;
            break;
        case SanemiState::GaleSlash:
            ang = facing > 0 ? -18.0f : 198.0f;
            break;
        case SanemiState::RisingDustStorm:
            ang = fmodf(gt * 1350.0f, 360.0f);
            spin = true;
            break;
        case SanemiState::ColdMountainWind:
        case SanemiState::IdatenTyphoon:
        case SanemiState::WindBarrier:
            ang = fmodf(gt * 1600.0f, 360.0f);
            spin = true;
            break;
        case SanemiState::Fallen:
            ang = 84.0f;
            break;
        default:
            break;
    }
    if (facing < 0 && !spin && state != SanemiState::Arrive && state != SanemiState::Withdraw)
        ang = 180.0f - ang;
    DrawLineEx({ bx + facing * 7.0f, by - 15 }, hand, 5, Fade(uniform, alpha));
    Rectangle blade = { hand.x, hand.y, 54, 4.8f };
    DrawRectanglePro(blade, { 0, 2.4f }, ang, Fade(C(225, 232, 232), alpha));
    Rectangle edge = { hand.x + 8, hand.y - 1, 43, 2.0f };
    DrawRectanglePro(edge, { 0, 1.0f }, ang, Fade(SanemiCol(), alpha));
    Rectangle hilt = { hand.x, hand.y, 9, 8 };
    DrawRectanglePro(hilt, { 4.5f, 4 }, ang, Fade(C(52, 72, 54), alpha));

    if (state == SanemiState::WindBarrier) {
        float p = fmodf(gt * 2.5f, 1.0f);
        DrawRing(pos, 152 * p, 152 * p + 7, 0, 360, 58, Fade(SanemiCol(), 0.60f * (1 - p)));
        DrawRing(pos, 78, 84, 0, 360, 50, Fade(WindDark(), 0.30f + 0.12f * sinf(gt * 20.0f)));
        DrawRing(pos, 118, 124, 0, 360, 54, Fade(WindEdge(), 0.16f + 0.10f * sinf(gt * 28.0f)));
        if (barrierShieldMax > 0) {
            float f = Clampf(barrierShield / barrierShieldMax, 0, 1);
            DrawRectangle((int)(pos.x - 36), (int)(pos.y - h * 0.5f - 40), 72, 5, C(18, 28, 24));
            DrawRectangle((int)(pos.x - 35), (int)(pos.y - h * 0.5f - 39),
                          (int)(70 * f), 3, SanemiCol());
        }
    }
    if (state == SanemiState::IdatenTyphoon) {
        float p = fmodf(gt * 2.0f, 1.0f);
        DrawRing(pos, 260 * p, 260 * p + 8, 0, 360, 64, Fade(SanemiCol(), 0.48f * (1 - p)));
        DrawRing(pos, 128, 134, 0, 360, 48, Fade(WindDark(), 0.22f));
    }

    if (hp < maxHp && Active()) {
        float bw = w + 16;
        float f = Clampf(hp / maxHp, 0, 1);
        DrawRectangle((int)(pos.x - bw * 0.5f), (int)(pos.y - h * 0.5f - 16), (int)bw, 5, C(16, 18, 26));
        DrawRectangle((int)(pos.x - bw * 0.5f + 1), (int)(pos.y - h * 0.5f - 15),
                      (int)((bw - 2) * f), 3, SanemiCol());
    }
    if (Active() && state != SanemiState::Withdraw) {
        float f = Clampf(hp / maxHp, 0, 1);
        DrawRing({ pos.x, pos.y - h * 0.5f - 27 }, 5, 8, -90, -90 + 360 * f, 24,
                 Fade(SanemiCol(), 0.85f));
    }
}

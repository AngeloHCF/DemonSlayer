#include "tengen.h"
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

static const char* SAVE_FILE = "tengen_mastery.txt";
static const int XP_TH[5] = { 22, 54, 92, 145, 215 };

static Color TengenCol() { return C(255, 212, 88); }
static Color SoundPink() { return C(255, 90, 170); }
static Color SoundBlue() { return C(85, 220, 255); }
static Color BladeCol() { return C(230, 235, 238); }

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

static void SoundTrail(Effects& fx, Vector2 p, int facing) {
    fx.Sparks(p, facing > 0 ? 180.0f : 0.0f, 80, 5, TengenCol(), 480, 2.5f);
    fx.Sparks({ p.x, p.y + frnd(-10, 10) }, facing > 0 ? 160.0f : 20.0f, 55, 3,
              SoundBlue(), 360, 2.0f);
    if (GetRandomValue(0, 2) == 0)
        fx.Ember({ p.x + frnd(-14, 14), p.y + frnd(-18, 18) });
}

static void SoundPop(Effects& fx, Vector2 p, float scale = 1.0f) {
    fx.FireExplosion(p);
    fx.Ring(p, 8, 95 * scale, 520, 6, TengenCol());
    fx.Ring(p, 4, 66 * scale, 390, 4, SoundBlue());
}

static void DrawChain(Vector2 a, Vector2 b, float alpha) {
    DrawLineEx(a, b, 2.7f, Fade(C(70, 66, 62), 0.8f * alpha));
    int links = (int)Clampf(Dist(a, b) / 13.0f, 2.0f, 22.0f);
    for (int i = 1; i < links; i++) {
        float t = i / (float)links;
        Vector2 p = { Lerpf(a.x, b.x, t), Lerpf(a.y, b.y, t) };
        DrawCircleLines((int)p.x, (int)p.y, 3.0f, Fade(C(230, 225, 205), alpha));
    }
}

static void DrawCleaver(Vector2 hand, float ang, float alpha, bool flipped = false) {
    Rectangle grip = { hand.x, hand.y, 24.0f, 5.5f };
    DrawRectanglePro(grip, { 3.0f, 2.75f }, ang, Fade(C(72, 48, 38), alpha));
    float r = ang * DEG2RAD;
    Vector2 t = { cosf(r), sinf(r) };
    Vector2 n = { -sinf(r), cosf(r) };
    float side = flipped ? -1.0f : 1.0f;
    Vector2 base = { hand.x + t.x * 18.0f, hand.y + t.y * 18.0f };
    Vector2 tip = { hand.x + t.x * 58.0f, hand.y + t.y * 58.0f };
    DrawTriangle({ base.x + n.x * side * 16.0f, base.y + n.y * side * 16.0f },
                 { base.x - n.x * side * 7.0f, base.y - n.y * side * 7.0f },
                 { tip.x + n.x * side * 13.0f, tip.y + n.y * side * 13.0f },
                 Fade(BladeCol(), alpha));
    DrawTriangle({ base.x - n.x * side * 7.0f, base.y - n.y * side * 7.0f },
                 { tip.x - n.x * side * 10.0f, tip.y - n.y * side * 10.0f },
                 { tip.x + n.x * side * 13.0f, tip.y + n.y * side * 13.0f },
                 Fade(C(180, 188, 194), alpha));
    DrawLineEx({ base.x + n.x * side * 12.0f, base.y + n.y * side * 12.0f },
               { tip.x + n.x * side * 10.0f, tip.y + n.y * side * 10.0f },
               2.0f, Fade(TengenCol(), alpha));
}

// ---------------------------------------------------------------- mastery

int TengenMastery::Level() const {
    int lv = 0;
    for (int i = 0; i < 5; i++) if (xp >= XP_TH[i]) lv = i + 1;
    return lv;
}

int TengenMastery::NextThreshold() const {
    int lv = Level();
    return lv >= 5 ? -1 : XP_TH[lv];
}

void TengenMastery::Load() {
    if (!FileExists(SAVE_FILE)) return;
    char* txt = LoadFileText(SAVE_FILE);
    if (!txt) return;
    sscanf(txt, "%d %d %d", &xp, &summons, &kills);
    UnloadFileText(txt);
    if (xp < 0) xp = 0;
    if (xp > 999) xp = 999;
}

void TengenMastery::Save() const {
    char buf[64];
    snprintf(buf, sizeof(buf), "%d %d %d", xp, summons, kills);
    SaveFileText(SAVE_FILE, buf);
}

// ---------------------------------------------------------------- lifecycle

void Tengen::ResetRun() {
    state = TengenState::Inactive;
    fallen = false;
    summonedThisRun = false;
    summonCd = 0;
    activeT = 0;
    hp = maxHp = mastery.MaxHp();
    pos = { -120, cfg::GROUND_Y - 40 };
    vel = { 0, 0 };
    hitMem.Clear();
    stateTimer = attackTimer = 0;
    chainCd = rushCd = airCd = roarCd = scoreCd = deflectCd = 0;
    tickT = 0;
    curId = -1;
    formHits = 0;
    hitFlash = iframes = dodgeT = flashStepT = staggerT = 0;
    deflectShield = deflectShieldMax = 0;
    ultDangerLast = false;
    exitDir = -1;
}

bool Tengen::CanSummon() const {
    return !fallen && state == TengenState::Inactive && summonCd <= 0;
}

void Tengen::Summon(Vector2 playerPos, Effects& fx) {
    state = TengenState::Arrive;
    stateTimer = 0.46f;
    summonedThisRun = true;
    mastery.summons++;
    maxHp = mastery.MaxHp();
    if (hp <= 0) hp = maxHp;
    hp = fminf(hp, maxHp);
    activeT = mastery.Duration();
    hitMem.Clear();
    chainCd = rushCd = airCd = roarCd = 0;
    scoreCd = 7.0f;
    deflectCd = 4.0f;
    deflectShield = deflectShieldMax = 0;
    ultDangerLast = false;
    attackTimer = 0.18f;
    bool fromLeft = playerPos.x > cfg::SCREEN_W * 0.5f;
    pos = { fromLeft ? -68.0f : cfg::SCREEN_W + 68.0f, cfg::GROUND_Y - h * 0.5f };
    facing = fromLeft ? 1 : -1;
    exitDir = fromLeft ? -1 : 1;
    targetPos = { playerPos.x - facing * 105.0f, playerPos.y };
    iframes = 1.0f;
    flashStepT = 0.5f;
    fx.Text({ playerPos.x, playerPos.y - 122 }, TengenCol(), 1.55f, "TENGEN UZUI");
    fx.Text({ playerPos.x, playerPos.y - 96 }, SoundBlue(), 0.95f, "SOUND HASHIRA");
    fx.Ring(playerPos, 12, 170, 600, 7, TengenCol());
    fx.Flash(C(255, 220, 120), 0.12f);
    PlaySfx(SFX_SOUND, 1.0f, 0.86f);
    PlaySfx(SFX_WHOOSH, 0.85f, 1.28f);
}

void Tengen::BeginWithdraw(Effects& fx) {
    if (state == TengenState::Inactive || state == TengenState::Fallen ||
        state == TengenState::Withdraw) return;

    state = TengenState::Withdraw;
    stateTimer = 0;
    attackTimer = 0;
    tickT = 0;
    curId = -1;
    formHits = 0;
    deflectShield = deflectShieldMax = 0;
    ultDangerLast = false;
    hitMem.Clear();
    facing = exitDir;
    vel = { facing * 1450.0f, 0 };
    iframes = 0.8f;
    fx.Text({ pos.x, pos.y - h - 10 }, TengenCol(), 0.9f, "TENGEN WITHDRAWS FLASHILY");
}

Rectangle Tengen::Rect() const {
    return { pos.x - w * 0.5f, pos.y - h * 0.5f, w, h };
}

void Tengen::TakeDamage(float dmg, float kbx, HitKind kind, Effects& fx) {
    if (!Active() || state == TengenState::Arrive || state == TengenState::Withdraw) return;
    if (iframes > 0) return;

    bool bossHit = (kind == HitKind::BossDash || kind == HitKind::BossAoe ||
                    kind == HitKind::BossProjectile);
    if (state == TengenState::ExplosiveDeflection && deflectShield > 0) {
        deflectShield -= bossHit ? dmg * 0.55f : dmg * 0.25f;
        SoundPop(fx, { pos.x + facing * 44.0f, pos.y - 16.0f }, 0.75f);
        fx.AddShake(0.13f);
        PlaySfx(SFX_SOUND, 0.55f, 1.25f);
        if (deflectShield <= 0)
            fx.Text({ pos.x, pos.y - h - 12 }, TengenCol(), 0.95f, "deflection breaks");
        return;
    }

    float dodge = mastery.DodgeChance();
    if (bossHit) dodge *= 0.58f;
    if (state != TengenState::ScoreUltimate && frnd(0, 1) < dodge) {
        float dir = kbx >= 0 ? 1.0f : -1.0f;
        vel.x = dir * 560.0f;
        vel.y = fminf(vel.y, -120.0f);
        iframes = 0.34f;
        dodgeT = 0.30f;
        flashStepT = 0.32f;
        SoundTrail(fx, pos, (int)-dir);
        fx.Ring(pos, 8, 64, 430, 4, SoundBlue());
        PlaySfx(SFX_WHOOSH, 0.5f, 1.45f);
        if (state != TengenState::Follow && state != TengenState::ExplosiveDeflection)
            state = TengenState::Follow;
        return;
    }

    hp -= dmg * mastery.DmgTaken();
    hitFlash = 0.14f;
    if (bossHit) {
        iframes = 0.62f;
        staggerT = 0.40f;
        state = TengenState::Follow;
        vel.x = kbx * 1.05f;
        vel.y = -330.0f;
        fx.AddShake(0.32f);
        fx.AddHitstop(0.05f);
        fx.Ring({ pos.x, pos.y - 8 }, 8, 84, 460, 5, TengenCol());
        PlaySfx(SFX_EXPLO, 0.45f, 1.25f);
    } else {
        iframes = 0.46f;
        vel.x = kbx * 0.55f;
        vel.y = -160.0f;
    }
    fx.BloodSpray({ pos.x, pos.y - 8 }, kbx > 0 ? 1 : -1, bossHit ? 1.4f : 0.9f);
    fx.Text({ pos.x, pos.y - h }, C(255, 115, 95), 0.9f, "-%.0f", dmg);
    PlaySfx(SFX_HURT, 0.45f, 1.05f);

    if (hp <= 0) {
        hp = 0;
        fallen = true;
        state = TengenState::Fallen;
        stateTimer = 2.2f;
        vel = { 0, 0 };
        fx.AddShake(0.68f);
        fx.AddHitstop(0.16f);
        SoundPop(fx, pos, 1.3f);
        fx.DeathBurst(pos, TengenCol(), 1.55f);
        fx.Text({ pos.x, pos.y - h - 20 }, C(230, 75, 70), 1.5f, "TENGEN HAS FALLEN");
        PlaySfx(SFX_DEATH, 1.0f, 0.62f);
        mastery.Save();
    }
}

// ---------------------------------------------------------------- AI

void Tengen::PickAction(Player& player, std::vector<Enemy>& enemies,
                        Boss& boss, Akaza& akaza, UpperMoon* moon,
                        CombatSystem& cs, Effects& fx) {
    int best = -1;
    float bestScore = -1e9f;
    int crowd = 0, wideCrowd = 0, windups = 0;
    for (int i = 0; i < (int)enemies.size(); i++) {
        const Enemy& e = enemies[i];
        if (!e.alive) continue;
        float dT = Dist(e.pos, pos);
        float dP = Dist(e.pos, player.pos);
        if (dT < 165) crowd++;
        if (dT < 285) wideCrowd++;
        if (e.Busy() && dP < 220) windups++;
        float score = 330.0f - dT * 0.28f;
        if (dP < 130) score += 160;
        if (wideCrowd >= 3) score += 60;
        if (e.Busy()) score += 150;
        if (e.type == EType::Fast) score += 70;
        if (score > bestScore) { bestScore = score; best = i; }
    }

    bool moonUp = moon && moon->Alive();
    targetIsBoss = (best < 0 && (boss.Alive() || akaza.Alive() || moonUp));
    if (best >= 0)          targetPos = enemies[best].pos;
    else if (akaza.Alive()) targetPos = akaza.pos;
    else if (moonUp)        targetPos = moon->pos;
    else if (targetIsBoss)  targetPos = boss.pos;
    else                    targetPos = { player.pos.x - 120.0f, player.pos.y };

    float distT = Dist(targetPos, pos);

    bool bossThreat = boss.Alive() &&
        (boss.state == BState::Desperation || boss.state == BState::WhipStorm ||
         boss.CrescentsNear(player.pos, 260) >= 2 || boss.CrescentsNear(pos, 230) >= 2);
    bool akazaThreat = akaza.Alive() &&
        (akaza.state == AkState::Desperation || akaza.OrbsNear(player.pos, 250) >= 2 ||
         akaza.OrbsNear(pos, 220) >= 2);
    bool moonThreat = moon && moon->Menacing(pos, player.pos);
    if (deflectCd <= 0 && (bossThreat || akazaThreat || moonThreat || windups >= 3)) {
        state = TengenState::ExplosiveDeflection;
        stateTimer = 1.75f + 0.08f * mastery.Level();
        tickT = 0;
        formHits = 0;
        deflectShieldMax = mastery.DeflectShield();
        deflectShield = deflectShieldMax;
        deflectCd = mastery.DeflectCd();
        facing = UltimateSourceX(boss, akaza, moon, player.pos.x) > pos.x ? 1 : -1;
        activeT = fmaxf(activeT, stateTimer + 0.35f);
        vel.x = 0;
        iframes = fmaxf(iframes, 0.25f);
        fx.Text({ pos.x, pos.y - h - 16 }, TengenCol(), 1.12f, "EXPLOSIVE DEFLECTION");
        fx.Ring(pos, 16, 180, 650, 8, TengenCol());
        PlaySfx(SFX_SOUND, 1.0f, 0.75f);
        return;
    }

    if (scoreCd <= 0 && (targetIsBoss || wideCrowd >= 4 || windups >= 3) && distT < 540.0f) {
        state = TengenState::ScoreUltimate;
        stateTimer = 1.42f;
        tickT = 0;
        formHits = 0;
        curId = cs.NewId();
        scoreCd = 22.0f - mastery.Level();
        facing = targetPos.x > pos.x ? 1 : -1;
        vel.x = 0;
        fx.Text({ pos.x, pos.y - h - 16 }, TengenCol(), 1.18f, "MUSICAL SCORE: FLASHY FINALE");
        fx.Ring(pos, 24, 270, 720, 10, TengenCol());
        fx.Flash(C(255, 210, 100), 0.18f);
        PlaySfx(SFX_SOUND, 1.0f, 0.62f);
        return;
    }

    if (attackTimer > 0 || (best < 0 && !targetIsBoss)) return;
    if (distT > 620) return;

    facing = targetPos.x > pos.x ? 1 : -1;
    TengenState form;
    if ((wideCrowd >= 3 || targetIsBoss) && roarCd <= 0 && distT < 230)
        form = TengenState::Roar;
    else if ((distT > 230 || targetIsBoss) && rushCd <= 0)
        form = TengenState::ExplosiveRush;
    else if (airCd <= 0 && (crowd >= 2 || distT > 150))
        form = TengenState::RisingBeat;
    else if ((wideCrowd >= 2 || distT > 150) && chainCd <= 0)
        form = TengenState::ChainSweep;
    else
        form = TengenState::LightCombo;

    if (form == lastForm) {
        if (form == TengenState::LightCombo)
            form = (chainCd <= 0) ? TengenState::ChainSweep : TengenState::ExplosiveRush;
        else if (form == TengenState::ChainSweep)
            form = TengenState::LightCombo;
        else if (form == TengenState::ExplosiveRush)
            form = (airCd <= 0) ? TengenState::RisingBeat : TengenState::LightCombo;
        else
            form = TengenState::LightCombo;
    }
    lastForm = form;
    state = form;
    formHits = 0;
    tickT = 0;

    switch (form) {
        case TengenState::ChainSweep:
            stateTimer = 0.50f;
            chainCd = 2.8f;
            curId = cs.NewId();
            fx.Text({ pos.x, pos.y - h - 12 }, TengenCol(), 0.82f, "SOUND BREATHING: CHAIN CLEAVE");
            PlaySfx(SFX_CHAIN, 0.72f, 1.28f);
            break;
        case TengenState::ExplosiveRush:
            stateTimer = 0.56f;
            rushCd = 3.6f;
            curId = cs.NewId();
            fx.Text({ pos.x, pos.y - h - 12 }, TengenCol(), 0.84f, "FIRST FORM: ROAR RUSH");
            PlaySfx(SFX_SOUND, 0.82f, 1.08f);
            break;
        case TengenState::RisingBeat:
            stateTimer = 0.70f;
            airCd = 4.4f;
            curId = cs.NewId();
            vel.x = facing * 520.0f;
            vel.y = -470.0f;
            onGround = false;
            fx.Text({ pos.x, pos.y - h - 12 }, TengenCol(), 0.82f, "RISING BEAT");
            PlaySfx(SFX_WHOOSH, 0.65f, 1.35f);
            break;
        case TengenState::Roar:
            stateTimer = 0.64f;
            roarCd = 6.4f;
            fx.Text({ pos.x, pos.y - h - 12 }, TengenCol(), 0.9f, "FOURTH FORM: CONSTANT RESOUNDING SLASHES");
            PlaySfx(SFX_SOUND, 0.95f, 0.82f);
            break;
        default:
            stateTimer = 0.64f;
            fx.Text({ pos.x, pos.y - h - 12 }, TengenCol(), 0.78f, "FLASHY COMBO");
            PlaySfx(SFX_SLASH, 0.55f, 1.35f);
            break;
    }
    attackTimer = mastery.Cadence();
}

void Tengen::Update(float dt, Player& player, std::vector<Enemy>& enemies,
                    Boss& boss, Akaza& akaza, UpperMoon* moon,
                    CombatSystem& cs, Effects& fx) {
    if (state == TengenState::Inactive) {
        summonCd = fmaxf(summonCd - dt, 0);
        return;
    }
    if (state == TengenState::Fallen) {
        if (stateTimer > 0) stateTimer -= dt;
        return;
    }

    hitFlash = fmaxf(hitFlash - dt, 0);
    iframes = fmaxf(iframes - dt, 0);
    dodgeT = fmaxf(dodgeT - dt, 0);
    flashStepT = fmaxf(flashStepT - dt, 0);
    staggerT = fmaxf(staggerT - dt, 0);
    attackTimer = fmaxf(attackTimer - dt, 0);
    chainCd = fmaxf(chainCd - dt, 0);
    rushCd = fmaxf(rushCd - dt, 0);
    airCd = fmaxf(airCd - dt, 0);
    roarCd = fmaxf(roarCd - dt, 0);
    scoreCd = fmaxf(scoreCd - dt, 0);
    deflectCd = fmaxf(deflectCd - dt, 0);

    bool ultDanger = UltimateDanger(boss, akaza, moon);
    if (ultDanger && !ultDangerLast && deflectCd <= 0 &&
        state != TengenState::Arrive && state != TengenState::Withdraw) {
        state = TengenState::ExplosiveDeflection;
        stateTimer = 1.9f + 0.10f * mastery.Level();
        tickT = 0;
        formHits = 0;
        deflectShieldMax = mastery.DeflectShield() * 1.2f;
        deflectShield = deflectShieldMax;
        deflectCd = mastery.DeflectCd();
        facing = UltimateSourceX(boss, akaza, moon, player.pos.x) > pos.x ? 1 : -1;
        activeT = fmaxf(activeT, stateTimer + 0.4f);
        vel.x = 0;
        iframes = fmaxf(iframes, 0.32f);
        fx.Text({ pos.x, pos.y - h - 16 }, TengenCol(), 1.18f,
                "EXPLOSIVE DEFLECTION: ULTIMATE GUARD");
        fx.Ring(pos, 20, 215, 720, 10, TengenCol());
        PlaySfx(SFX_SOUND, 1.0f, 0.65f);
    }
    ultDangerLast = ultDanger;

    float dmgMul = mastery.DmgMult();

    switch (state) {
        case TengenState::Arrive: {
            stateTimer -= dt;
            float spd = 1760.0f;
            float dx = targetPos.x - pos.x;
            if (fabsf(dx) > 18) vel.x = (dx > 0 ? 1.0f : -1.0f) * spd;
            else vel.x = 0;
            SoundTrail(fx, pos, facing);
            if (stateTimer <= 0) {
                state = TengenState::Follow;
                vel.x = 0;
                SoundPop(fx, { pos.x, pos.y - 10 }, 1.05f);
                fx.AddShake(0.28f);
            }
            break;
        }
        case TengenState::Follow: {
            if (staggerT > 0) {
                vel.x *= 1.0f - Clampf(6.0f * dt, 0, 1);
                break;
            }
            PickAction(player, enemies, boss, akaza, moon, cs, fx);
            if (state != TengenState::Follow) break;
            float orbit = targetIsBoss ? 125.0f : 76.0f;
            float weave = sinf((float)GetTime() * 8.0f) * 34.0f;
            float wantX = targetPos.x - facing * orbit + weave;
            if (!targetIsBoss && Dist(targetPos, player.pos) > 260.0f)
                wantX = player.pos.x + (pos.x < player.pos.x ? -95.0f : 95.0f);
            float dx = wantX - pos.x;
            facing = targetPos.x > pos.x ? 1 : -1;
            if (fabsf(dx) > 24) {
                vel.x = (dx > 0 ? 1.0f : -1.0f) * mastery.MoveSpeed();
                if (GetRandomValue(0, 2) == 0) SoundTrail(fx, pos, facing);
            } else {
                vel.x *= 1.0f - Clampf(10.0f * dt, 0, 1);
            }
            break;
        }
        case TengenState::LightCombo: {
            stateTimer -= dt;
            float elapsed = 0.64f - stateTimer;
            vel.x = facing * (170.0f + 80.0f * sinf(elapsed * 22.0f));
            const float marks[4] = { 0.08f, 0.22f, 0.38f, 0.54f };
            if (formHits < 4 && elapsed >= marks[formHits]) {
                int hit = formHits++;
                Rectangle r = { facing > 0 ? pos.x - 12 : pos.x - 118, pos.y - 48, 130, 94 };
                cs.Add(r, (12.0f + hit * 3.0f) * dmgMul, facing * (220.0f + hit * 65.0f),
                       -130.0f - hit * 25.0f, 0.055f, Team::Player, HitKind::Tengen, cs.NewId());
                Vector2 c = { pos.x + facing * (42.0f + hit * 10.0f), pos.y - 9.0f };
                fx.SlashArc(c, 62 + hit * 7.0f, facing > 0 ? -70 : 250,
                            facing > 0 ? 58 : 122, hit % 2 ? SoundBlue() : TengenCol());
                if (hit == 3) {
                    SoundPop(fx, { pos.x + facing * 78.0f, pos.y - 12.0f }, 0.62f);
                    fx.AddShake(0.18f);
                }
                PlaySfx(hit == 3 ? SFX_SOUND : SFX_SLASH, 0.52f, 1.25f + hit * 0.08f);
            }
            if (stateTimer <= 0) state = TengenState::Follow;
            break;
        }
        case TengenState::ChainSweep: {
            stateTimer -= dt;
            float elapsed = 0.50f - stateTimer;
            vel.x *= 1.0f - Clampf(7.0f * dt, 0, 1);
            if (formHits == 0 && elapsed >= 0.16f) {
                formHits = 1;
                Rectangle r = { facing > 0 ? pos.x - 12 : pos.x - 286, pos.y - 62, 298, 122 };
                cs.Add(r, 24.0f * dmgMul, facing * 390.0f, -230, 0.11f,
                       Team::Player, HitKind::Tengen, curId);
                fx.SlashArc({ pos.x + facing * 145.0f, pos.y - 10.0f }, 132,
                            facing > 0 ? -48 : 228, facing > 0 ? 42 : 138, TengenCol());
                fx.Ring({ pos.x + facing * 138.0f, pos.y - 8.0f }, 10, 100, 460, 5, SoundPink());
                PlaySfx(SFX_CHAIN, 0.75f, 1.22f);
            }
            if (formHits == 1 && elapsed >= 0.34f) {
                formHits = 2;
                Rectangle r = { facing > 0 ? pos.x - 186 : pos.x - 14, pos.y - 54, 200, 108 };
                cs.Add(r, 18.0f * dmgMul, -facing * 280.0f, -170, 0.08f,
                       Team::Player, HitKind::Tengen, cs.NewId());
                SoundTrail(fx, { pos.x - facing * 88.0f, pos.y - 10.0f }, -facing);
            }
            if (stateTimer <= 0) state = TengenState::Follow;
            break;
        }
        case TengenState::ExplosiveRush: {
            stateTimer -= dt;
            vel.x = facing * 1160.0f;
            SoundTrail(fx, pos, facing);
            Rectangle r = { pos.x - 66 + facing * 46, pos.y - 50, 132, 100 };
            cs.Add(r, 8.5f * dmgMul, facing * 240.0f, -120, 0.035f,
                   Team::Player, HitKind::Tengen, curId);
            tickT -= dt;
            if (tickT <= 0) {
                tickT = 0.13f;
                formHits++;
                SoundPop(fx, { pos.x + facing * 58.0f, pos.y - 8.0f + frnd(-18, 18) }, 0.55f);
                PlaySfx(SFX_SOUND, 0.42f, 1.15f + formHits * 0.04f);
            }
            if (stateTimer <= 0) {
                state = TengenState::Follow;
                vel.x *= 0.18f;
            }
            break;
        }
        case TengenState::RisingBeat: {
            stateTimer -= dt;
            SoundTrail(fx, pos, facing);
            Rectangle r = { pos.x - 72, pos.y - 68, 144, 128 };
            cs.Add(r, 13.5f * dmgMul, facing * 210.0f, -310, 0.04f,
                   Team::Player, HitKind::Tengen, curId);
            if ((onGround && stateTimer < 0.40f) || stateTimer <= 0) {
                state = TengenState::Follow;
                SoundPop(fx, { pos.x + facing * 28.0f, cfg::GROUND_Y - 24.0f }, 0.85f);
                fx.AddShake(0.24f);
            }
            break;
        }
        case TengenState::Roar: {
            stateTimer -= dt;
            vel.x *= 1.0f - Clampf(7.0f * dt, 0, 1);
            float elapsed = 0.64f - stateTimer;
            if (formHits == 0 && elapsed >= 0.22f) {
                formHits = 1;
                Rectangle r = { pos.x - 175, pos.y - 82, 350, 156 };
                cs.Add(r, 45.0f * dmgMul, facing * 470.0f, -380, 0.14f,
                       Team::Player, HitKind::Tengen, cs.NewId());
                SoundPop(fx, { pos.x + facing * 62.0f, pos.y - 12.0f }, 1.28f);
                fx.SlashArc(pos, 150, 0, 360, TengenCol());
                fx.AddShake(0.52f);
                fx.AddHitstop(0.065f);
                PlaySfx(SFX_SOUND, 0.95f, 0.78f);
            }
            if (stateTimer <= 0) state = TengenState::Follow;
            break;
        }
        case TengenState::ScoreUltimate: {
            stateTimer -= dt;
            tickT -= dt;
            float elapsed = 1.42f - stateTimer;
            if (tickT <= 0 && formHits < 8) {
                tickT = 0.105f;
                int hit = formHits++;
                int dir = (hit % 2 == 0) ? facing : -facing;
                vel.x = dir * 620.0f;
                Rectangle r = { pos.x - 235, pos.y - 108, 470, 202 };
                cs.Add(r, 15.0f * dmgMul, dir * 260.0f, -210, 0.06f,
                       Team::Player, HitKind::Tengen, cs.NewId());
                Vector2 p = { pos.x + dir * frnd(42, 185), pos.y + frnd(-70, 36) };
                fx.SlashArc(p, 90 + hit * 5.0f, -65, 65, hit % 2 ? SoundBlue() : TengenCol());
                SoundPop(fx, p, 0.62f);
                PlaySfx(SFX_SOUND, 0.50f, 1.0f + hit * 0.045f);
            }
            if (elapsed >= 1.12f && formHits < 12) {
                formHits = 12;
                Rectangle r = { pos.x - 390, pos.y - 150, 780, 270 };
                cs.Add(r, 58.0f * dmgMul, 0, -560, 0.18f,
                       Team::Player, HitKind::Tengen, cs.NewId());
                for (int i = 0; i < 6; i++) {
                    Vector2 p = { pos.x + frnd(-330, 330), pos.y + frnd(-112, 80) };
                    SoundPop(fx, p, 0.95f);
                }
                fx.Ring(pos, 40, 390, 760, 14, TengenCol());
                fx.Flash(C(255, 210, 110), 0.26f);
                fx.AddShake(0.86f);
                fx.AddHitstop(0.11f);
                PlaySfx(SFX_EXPLO, 1.0f, 0.78f);
            }
            if (stateTimer <= 0) state = TengenState::Follow;
            break;
        }
        case TengenState::ExplosiveDeflection: {
            stateTimer -= dt;
            tickT -= dt;
            vel.x *= 1.0f - Clampf(9.0f * dt, 0, 1);
            float r = 155.0f + 9.0f * mastery.Level();
            float allyR = 120.0f + 8.0f * mastery.Level();
            int stopped = 0;
            for (auto& hb : cs.Boxes()) {
                if (hb.team != Team::Enemy || hb.life <= 0) continue;
                Vector2 c = { hb.rect.x + hb.rect.width * 0.5f,
                              hb.rect.y + hb.rect.height * 0.5f };
                if (Dist(c, pos) < r || Dist(c, player.pos) < allyR) {
                    hb.life = 0;
                    stopped += (hb.kind == HitKind::BossProjectile || hb.kind == HitKind::BossAoe) ? 6 : 3;
                    SoundPop(fx, c, 0.52f);
                }
            }
            stopped += boss.NullifyCrescents(pos, r) * 5;
            stopped += boss.NullifyCrescents(player.pos, allyR) * 5;
            stopped += boss.NullifyRings(pos, r) * 13;
            stopped += boss.NullifyRings(player.pos, allyR) * 13;
            stopped += akaza.NullifyOrbs(pos, r) * 5;
            stopped += akaza.NullifyOrbs(player.pos, allyR) * 5;
            if (moon) {
                stopped += moon->NullifyShards(pos, r) * 5;
                stopped += moon->NullifyShards(player.pos, allyR) * 5;
            }
            if (stopped > 0) {
                deflectShield -= (float)stopped;
                fx.AddShake(0.10f + 0.012f * fminf((float)stopped, 20.0f));
                fx.AddHitstop(0.02f);
                PlaySfx(SFX_SOUND, 0.55f, 1.28f);
            }
            if (tickT <= 0) {
                tickT = 0.08f;
                fx.SlashArc(pos, r * 0.85f, 0, 360, TengenCol());
                fx.Ring(pos, 20, r, 720, 5, TengenCol());
                SoundTrail(fx, { pos.x + frnd(-r * 0.65f, r * 0.65f),
                                 pos.y + frnd(-r * 0.45f, r * 0.45f) }, facing);
            }
            if (deflectShield <= 0) {
                fx.Text({ pos.x, pos.y - h - 12 }, TengenCol(), 0.95f, "deflection breaks");
                SoundPop(fx, pos, 0.9f);
                stateTimer = 0;
            }
            if (stateTimer <= 0) state = TengenState::Follow;
            break;
        }
        case TengenState::Withdraw: {
            vel.x = facing * 1450.0f;
            SoundTrail(fx, pos, facing);
            if (pos.x < -90 || pos.x > cfg::SCREEN_W + 90) {
                state = TengenState::Inactive;
                summonCd = 0;
                vel = { 0, 0 };
                hitMem.Clear();
            }
            break;
        }
        default: break;
    }

    if (state != TengenState::Arrive) vel.y += cfg::GRAVITY * dt;
    else vel.y = 0;
    pos.x += vel.x * dt;
    pos.y += vel.y * dt;
    if (state != TengenState::Withdraw && state != TengenState::Arrive)
        pos.x = Clampf(pos.x, 30.0f, (float)cfg::SCREEN_W - 30.0f);
    onGround = GroundClamp(pos, vel, h * 0.5f);
}

// ---------------------------------------------------------------- drawing

void Tengen::Draw() const {
    if (state == TengenState::Inactive) return;
    float gt = (float)GetTime();

    float alpha = 1.0f;
    if (state == TengenState::Fallen) {
        alpha = Clampf(stateTimer / 2.2f, 0, 1);
        if (alpha <= 0.01f) return;
    }
    if (iframes > 0 && state != TengenState::ExplosiveDeflection)
        alpha *= fmodf(gt * 16.0f, 2.0f) < 1.0f ? 0.55f : 1.0f;

    DrawEllipse((int)pos.x, (int)(cfg::GROUND_Y + 6), 23, 6, Fade(BLACK, 0.35f * alpha));
    if (flashStepT > 0) {
        float a = Clampf(flashStepT / 0.5f, 0, 1);
        for (int i = 1; i <= 3; i++)
            DrawEllipse((int)(pos.x - facing * i * 28.0f), (int)(cfg::GROUND_Y + 6),
                        18, 5, Fade(TengenCol(), 0.12f * a));
    }

    float bx = pos.x, by = pos.y;
    bool kneel = (state == TengenState::Fallen);
    if (kneel) by += 10;
    float bob = (state == TengenState::Follow) ? sinf(gt * 16.0f) * 2.2f : 0.0f;
    by += bob;
    if (dodgeT > 0) bx -= facing * 12.0f * Clampf(dodgeT / 0.30f, 0, 1);

    Color uniform = C(26, 26, 34);
    Color wraps = C(232, 232, 224);
    Color gold = TengenCol();
    Color skin = C(236, 204, 176);
    Color hair = C(238, 238, 232);
    if (hitFlash > 0) { uniform = C(255, 150, 120); wraps = uniform; gold = C(255, 220, 180); }

    DrawRectangle((int)(bx - 11), (int)(by + 8), 8, kneel ? 12 : 24, Fade(uniform, alpha));
    DrawRectangle((int)(bx + 3), (int)(by + 8), 8, kneel ? 12 : 24, Fade(C(18, 18, 25), alpha));
    DrawRectangleRounded({ bx - 14, by - 24, 28, 34 }, 0.22f, 4, Fade(uniform, alpha));
    DrawRectangle((int)(bx - 16), (int)(by - 18), 32, 5, Fade(gold, alpha));
    DrawRectangle((int)(bx - 13), (int)(by + 3), 26, 4, Fade(wraps, alpha));
    DrawLineEx({ bx - 18, by - 16 }, { bx - 34, by + 10 }, 6, Fade(wraps, alpha));
    DrawLineEx({ bx + 18, by - 16 }, { bx + 34, by + 10 }, 6, Fade(wraps, alpha));

    Vector2 headC = { bx + facing * 2.0f, by - 34.0f };
    DrawCircleV(headC, 10.5f, Fade(skin, alpha));
    DrawRectangle((int)(headC.x - 13), (int)(headC.y - 9), 26, 6, Fade(C(42, 42, 50), alpha));
    for (int i = -2; i <= 2; i++)
        DrawCircleV({ headC.x + i * 5.0f, headC.y - 7.0f + fabsf((float)i) },
                    2.6f, Fade(gold, alpha));
    DrawCircleSector({ headC.x - facing * 3.0f, headC.y - 7.0f }, 13.5f, 185, 350, 12,
                     Fade(hair, alpha));
    DrawCircleV({ headC.x + facing * 4.0f, headC.y + 1.0f }, 1.6f, Fade(C(120, 70, 40), alpha));

    Vector2 handL = { bx - facing * 12.0f, by - 8.0f };
    Vector2 handR = { bx + facing * 14.0f, by - 8.0f };
    Vector2 bladeL = { handL.x - facing * 34.0f, handL.y - 2.0f };
    Vector2 bladeR = { handR.x + facing * 48.0f, handR.y - 5.0f };
    float angL = facing > 0 ? 205.0f : -25.0f;
    float angR = facing > 0 ? -18.0f : 198.0f;

    switch (state) {
        case TengenState::Arrive:
        case TengenState::Withdraw:
            bladeL = { bx - facing * 88.0f, by - 8.0f };
            bladeR = { bx + facing * 100.0f, by - 10.0f };
            angL = facing > 0 ? 182.0f : -2.0f;
            angR = facing > 0 ? 0.0f : 180.0f;
            break;
        case TengenState::LightCombo:
            bladeL = { bx - facing * (48.0f + sinf(gt * 38.0f) * 22.0f), by - 14.0f + cosf(gt * 29.0f) * 18.0f };
            bladeR = { bx + facing * (58.0f + cosf(gt * 42.0f) * 22.0f), by - 13.0f + sinf(gt * 31.0f) * 18.0f };
            angL = fmodf(gt * 1100.0f, 360.0f);
            angR = fmodf(-gt * 1250.0f, 360.0f);
            break;
        case TengenState::ChainSweep:
            bladeL = { bx - facing * 128.0f, by - 20.0f };
            bladeR = { bx + facing * 168.0f, by - 14.0f + sinf(gt * 32.0f) * 22.0f };
            angL = facing > 0 ? 178.0f : 2.0f;
            angR = facing > 0 ? -8.0f : 188.0f;
            break;
        case TengenState::ExplosiveRush:
            bladeL = { bx - facing * 44.0f, by - 36.0f };
            bladeR = { bx + facing * 98.0f, by - 6.0f };
            angL = facing > 0 ? -42.0f : 222.0f;
            angR = facing > 0 ? 10.0f : 170.0f;
            break;
        case TengenState::RisingBeat: {
            float a = gt * 18.0f;
            bladeL = { bx + cosf(a) * 88.0f, by - 10.0f + sinf(a) * 62.0f };
            bladeR = { bx + cosf(a + PI) * 92.0f, by - 10.0f + sinf(a + PI) * 66.0f };
            angL = fmodf(gt * 900.0f, 360.0f);
            angR = fmodf(gt * 900.0f + 180.0f, 360.0f);
            break;
        }
        case TengenState::Roar:
            bladeL = { bx - facing * 80.0f, by - 58.0f };
            bladeR = { bx + facing * 92.0f, by + 22.0f };
            angL = facing > 0 ? -100.0f : 280.0f;
            angR = facing > 0 ? 42.0f : 138.0f;
            break;
        case TengenState::ScoreUltimate: {
            float a = gt * 23.0f;
            bladeL = { bx + cosf(a) * 160.0f, by - 12.0f + sinf(a) * 96.0f };
            bladeR = { bx + cosf(a + PI) * 170.0f, by - 12.0f + sinf(a + PI) * 104.0f };
            angL = fmodf(gt * 1500.0f, 360.0f);
            angR = fmodf(gt * 1500.0f + 180.0f, 360.0f);
            break;
        }
        case TengenState::ExplosiveDeflection: {
            float a = gt * 28.0f;
            bladeL = { bx + cosf(a) * 120.0f, by - 10.0f + sinf(a) * 82.0f };
            bladeR = { bx + cosf(a + PI) * 120.0f, by - 10.0f + sinf(a + PI) * 82.0f };
            angL = fmodf(gt * 1680.0f, 360.0f);
            angR = fmodf(gt * 1680.0f + 180.0f, 360.0f);
            break;
        }
        case TengenState::Fallen:
            bladeL = { bx - 38.0f, cfg::GROUND_Y - 14.0f };
            bladeR = { bx + 38.0f, cfg::GROUND_Y - 14.0f };
            angL = 170.0f; angR = 10.0f;
            break;
        default:
            break;
    }

    DrawLineEx({ bx - facing * 8.0f, by - 18.0f }, handL, 5, Fade(uniform, alpha));
    DrawLineEx({ bx + facing * 8.0f, by - 18.0f }, handR, 5, Fade(uniform, alpha));
    DrawChain(bladeL, bladeR, alpha);
    DrawCleaver(bladeL, angL, alpha, true);
    DrawCleaver(bladeR, angR, alpha, false);

    if (state == TengenState::ExplosiveDeflection) {
        float p = fmodf(gt * 2.2f, 1.0f);
        DrawRing(pos, 145 * p, 145 * p + 6, 0, 360, 52, Fade(TengenCol(), 0.58f * (1 - p)));
        DrawRing(pos, 72, 77, 0, 360, 44, Fade(SoundBlue(), 0.28f + 0.12f * sinf(gt * 18.0f)));
        if (deflectShieldMax > 0) {
            float f = Clampf(deflectShield / deflectShieldMax, 0, 1);
            DrawRectangle((int)(pos.x - 34), (int)(pos.y - h * 0.5f - 40), 68, 5, C(24, 20, 22));
            DrawRectangle((int)(pos.x - 33), (int)(pos.y - h * 0.5f - 39),
                          (int)(66 * f), 3, TengenCol());
        }
    }
    if (state == TengenState::ScoreUltimate) {
        float p = fmodf(gt * 1.8f, 1.0f);
        DrawRing(pos, 240 * p, 240 * p + 8, 0, 360, 56, Fade(TengenCol(), 0.50f * (1 - p)));
        DrawRing(pos, 118, 122, 0, 360, 48, Fade(SoundPink(), 0.18f));
    }

    if (hp < maxHp && Active()) {
        float bw = w + 16;
        float f = Clampf(hp / maxHp, 0, 1);
        DrawRectangle((int)(pos.x - bw * 0.5f), (int)(pos.y - h * 0.5f - 16), (int)bw, 5, C(16, 18, 26));
        DrawRectangle((int)(pos.x - bw * 0.5f + 1), (int)(pos.y - h * 0.5f - 15),
                      (int)((bw - 2) * f), 3, TengenCol());
    }
    if (Active() && state != TengenState::Withdraw) {
        float f = Clampf(hp / maxHp, 0, 1);
        DrawRing({ pos.x, pos.y - h * 0.5f - 27 }, 5, 8, -90, -90 + 360 * f, 24,
                 Fade(TengenCol(), 0.85f));
    }
}

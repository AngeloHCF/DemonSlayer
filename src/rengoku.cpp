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
    blazingCd = tigerCd = ninthCd = 0;
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
    blazingCd = tigerCd = 0;
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
    if (state == RengokuState::FlamingWall && flameWallShield > 0) {
        flameWallShield -= bossHit ? 8.0f : 3.0f;
        fx.Ring(pos, 18, 92, 420, 5, RengokuCol());
        if (flameWallShield <= 0)
            fx.Text({ pos.x, pos.y - h - 12 }, RengokuCol(), 0.9f, "flaming wall breaks");
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
    if (crowd >= 3 && tigerCd <= 0) form = RengokuState::FormTiger;
    else if (distT > 190) form = RengokuState::FormUnknowing;
    else if ((targetIsBoss || crowd >= 2) && blazingCd <= 0) form = RengokuState::FormBlazing;
    else form = RengokuState::FormRisingSun;

    if (form == lastForm) {
        if (form == RengokuState::FormRisingSun)
            form = (blazingCd <= 0) ? RengokuState::FormBlazing : RengokuState::FormUnknowing;
        else if (form == RengokuState::FormUnknowing)
            form = RengokuState::FormRisingSun;
        else if (form == RengokuState::FormBlazing)
            form = (tigerCd <= 0) ? RengokuState::FormTiger : RengokuState::FormRisingSun;
        else
            form = RengokuState::FormBlazing;
    }
    lastForm = form;
    state = form;

    switch (form) {
        case RengokuState::FormUnknowing:
            stateTimer = 0.36f;
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
        case RengokuState::FormTiger:
            stateTimer = 0.80f;
            tickT = 0;
            formHits = 0;
            tigerCd = 7.0f;
            fx.Text({ pos.x, pos.y - h - 14 }, RengokuCol(), 0.9f, "FLAME TIGER");
            PlaySfx(SFX_FIRE, 0.9f, 0.75f);
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
    tigerCd = fmaxf(tigerCd - dt, 0);
    ninthCd = fmaxf(ninthCd - dt, 0);

    bool ultDanger = UltimateDanger(boss, akaza, moon);
    if (ultDanger && !ultDangerLast &&
        state != RengokuState::Arrive && state != RengokuState::Withdraw) {
        state = RengokuState::FlamingWall;
        stateTimer = 2.1f + 0.12f * mastery.Level();
        tickT = 0;
        formHits = 0;
        flameWallShieldMax = mastery.FlamingWallShield();
        flameWallShield = flameWallShieldMax;
        facing = UltimateSourceX(boss, akaza, moon, player.pos.x) > pos.x ? 1 : -1;
        activeT = fmaxf(activeT, stateTimer + 0.4f);
        vel.x = 0;
        iframes = fmaxf(iframes, 0.35f);
        fx.Text({ pos.x, pos.y - h - 16 }, RengokuCol(), 1.15f, "FLAMING WALL");
        fx.Ring({ pos.x + facing * 66.0f, pos.y - 30.0f }, 18, 150, 460, 7, RengokuCol());
        PlaySfx(SFX_FIRE, 1.0f, 0.75f);
    }
    ultDangerLast = ultDanger;

    float dmgMul = mastery.DmgMult();

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
                FlameTrail(fx, pos, facing);
                Rectangle r = { pos.x - 72 + facing * 48, pos.y - 42, 150, 84 };
                cs.Add(r, 30.0f * dmgMul, facing * 420.0f, -210, 0.035f,
                       Team::Player, HitKind::Rengoku, curId);
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
                fx.Ring(pos, 16, 120, 500, 7, RengokuCol());
                PlaySfx(SFX_FIRE, 0.75f, 1.15f);
            }
            if (stateTimer <= 0) state = RengokuState::Follow;
            break;
        }
        case RengokuState::FormBlazing: {
            stateTimer -= dt;
            vel.x *= 1.0f - Clampf(6.0f * dt, 0, 1);
            FlameTrail(fx, { pos.x + facing * 16.0f, pos.y - 8.0f }, facing);
            if (formHits == 0 && stateTimer <= 0.30f) {
                formHits = 1;
                Rectangle r = {
                    facing > 0 ? pos.x - 14 : pos.x - 142,
                    pos.y - 54, 156, 108
                };
                cs.Add(r, 43.0f * dmgMul, facing * 520.0f, -300, 0.09f,
                       Team::Player, HitKind::Rengoku, cs.NewId());
                Vector2 c = { pos.x + facing * 58.0f, pos.y - 8.0f };
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
                FlameTrail(fx, pos, facing);
                Rectangle r = {
                    facing > 0 ? pos.x - 18 : pos.x - 132,
                    pos.y - 48, 150, 96
                };
                cs.Add(r, 18.5f * dmgMul, facing * 340.0f, -210, 0.07f,
                       Team::Player, HitKind::Rengoku, cs.NewId());
                fx.SlashArc({ pos.x + facing * 38.0f, pos.y - 4.0f },
                            76 + formHits * 8.0f, -50, 55, RengokuCol());
                PlaySfx(SFX_SLASH, 0.45f, 0.8f + formHits * 0.08f);
            }
            if (stateTimer <= 0) {
                state = RengokuState::Follow;
                fx.FireExplosion({ pos.x + facing * 40.0f, pos.y - 6.0f });
            }
            break;
        }
        case RengokuState::FlamingWall: {
            stateTimer -= dt;
            tickT -= dt;
            vel.x *= 1.0f - Clampf(9.0f * dt, 0, 1);

            Rectangle wall = {
                facing > 0 ? pos.x + 38.0f : pos.x - 104.0f,
                cfg::GROUND_Y - 215.0f,
                66.0f,
                220.0f
            };

            int stopped = 0;
            for (auto& hb : cs.Boxes()) {
                if (hb.team != Team::Enemy || hb.life <= 0) continue;
                if (CheckCollisionRecs(hb.rect, wall)) {
                    hb.life = 0;
                    stopped += 2;
                    Vector2 c = { hb.rect.x + hb.rect.width * 0.5f,
                                  hb.rect.y + hb.rect.height * 0.5f };
                    fx.FireExplosion(c);
                }
            }
            stopped += boss.NullifyCrescentsInRect(wall);
            stopped += boss.NullifyRingsInRect(wall) * 8;
            stopped += akaza.NullifyOrbsInRect(wall);
            if (moon) stopped += moon->NullifyShardsInRect(wall);

            if (stopped > 0) {
                flameWallShield -= (float)stopped;
                fx.AddShake(0.08f);
                PlaySfx(SFX_FIRE, 0.35f, 1.35f);
            }
            if (tickT <= 0) {
                tickT = 0.08f;
                for (int i = 0; i < 3; i++)
                    fx.Ember({ wall.x + frnd(0, wall.width), wall.y + frnd(0, wall.height) });
                fx.Sparks({ wall.x + wall.width * 0.5f, wall.y + wall.height * 0.55f },
                          facing > 0 ? 180.0f : 0.0f, 70, 3, C(255, 210, 80), 360, 3);
                fx.Ring({ wall.x + wall.width * 0.5f, cfg::GROUND_Y - 72.0f },
                        8, 76, 360, 4, RengokuCol());
            }
            if (flameWallShield <= 0) {
                fx.Text({ pos.x, pos.y - h - 12 }, RengokuCol(), 0.9f,
                        "flaming wall breaks");
                stateTimer = 0;
            }
            if (stateTimer <= 0) state = RengokuState::Follow;
            break;
        }
        case RengokuState::NinthForm: {
            stateTimer -= dt;
            if (stateTimer > 0.72f) {
                vel.x *= 1.0f - Clampf(8.0f * dt, 0, 1);
                FlameTrail(fx, pos, facing);
            } else if (stateTimer > 0.22f) {
                vel.x = facing * 1420.0f;
                FlameTrail(fx, pos, facing);
                Rectangle r = { pos.x - 110 + facing * 80, pos.y - 72, 220, 144 };
                cs.Add(r, 66.0f * dmgMul, facing * 640.0f, -420, 0.04f,
                       Team::Player, HitKind::Rengoku, curId);
                if (formHits == 0 && stateTimer < 0.50f) {
                    formHits = 1;
                    fx.FireExplosion({ pos.x + facing * 70.0f, pos.y - 8.0f });
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
        case RengokuState::FormTiger: ang = sinf(gt * 38.0f) * 50.0f; break;
        case RengokuState::FlamingWall: ang = -18.0f; break;
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

    if (state == RengokuState::NinthForm) {
        float p = fmodf(gt * 1.6f, 1.0f);
        DrawRing(pos, 155 * p, 155 * p + 5, 0, 360, 48, Fade(RengokuCol(), 0.55f * (1 - p)));
    }
    if (state == RengokuState::FlamingWall) {
        Rectangle wall = {
            facing > 0 ? pos.x + 38.0f : pos.x - 104.0f,
            cfg::GROUND_Y - 215.0f,
            66.0f,
            220.0f
        };
        float pulse = 0.45f + 0.18f * sinf(gt * 18.0f);
        DrawRectangleGradientV((int)wall.x, (int)wall.y, (int)wall.width, (int)wall.height,
                               Fade(C(255, 225, 110), pulse),
                               Fade(C(220, 45, 24), pulse + 0.12f));
        DrawRectangleLinesEx(wall, 3, Fade(C(255, 245, 190), 0.82f));
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

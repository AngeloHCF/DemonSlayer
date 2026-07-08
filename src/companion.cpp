#include "companion.h"
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

static const char* SAVE_FILE = "giyu_mastery.txt";
static const int XP_TH[5] = { 20, 50, 90, 140, 200 };

static bool UltimateDanger(const Boss& boss, const Akaza& akaza, const UpperMoon* moon) {
    return (boss.Alive() && boss.state == BState::Desperation) ||
           (akaza.Alive() && akaza.state == AkState::Desperation) ||
           (moon && moon->Alive() && moon->kind == MOON_KOKU &&
            moon->state == MState::Desperation);
}

// ---------------------------------------------------------------- mastery

int GiyuMastery::Level() const {
    int lv = 0;
    for (int i = 0; i < 5; i++) if (xp >= XP_TH[i]) lv = i + 1;
    return lv;
}

int GiyuMastery::NextThreshold() const {
    int lv = Level();
    return lv >= 5 ? -1 : XP_TH[lv];
}

void GiyuMastery::Load() {
    if (!FileExists(SAVE_FILE)) return;
    char* txt = LoadFileText(SAVE_FILE);
    if (!txt) return;
    sscanf(txt, "%d %d %d", &xp, &summons, &kills);
    UnloadFileText(txt);
    if (xp < 0) xp = 0;
    if (xp > 999) xp = 999;
}

void GiyuMastery::Save() const {
    char buf[64];
    snprintf(buf, sizeof(buf), "%d %d %d", xp, summons, kills);
    SaveFileText(SAVE_FILE, buf);
}

// ---------------------------------------------------------------- lifecycle

void Giyu::ResetRun() {
    state = GiyuState::Inactive;
    fallen = false;
    summonedThisRun = false;
    summonCd = 0;
    activeT = 0;
    hp = maxHp = mastery.MaxHp();
    pos = { -100, cfg::GROUND_Y - 40 };
    vel = { 0, 0 };
    hitMem.Clear();
    stateTimer = attackTimer = 0;
    whirlCd = wheelCd = deadCalmCd = 0;
    hitFlash = iframes = 0;
    deadCalmShield = deadCalmShieldMax = 0;
    ultDangerLast = false;
    exitDir = -1;
}

bool Giyu::CanSummon() const {
    return !fallen && state == GiyuState::Inactive && summonCd <= 0;
}

void Giyu::Summon(Vector2 playerPos, Effects& fx) {
    state = GiyuState::Arrive;
    stateTimer = 0.55f;
    summonedThisRun = true;
    mastery.summons++;
    maxHp = mastery.MaxHp();
    if (hp <= 0) hp = maxHp;
    hp = fminf(hp, maxHp);
    activeT = mastery.Duration();
    hitMem.Clear();
    whirlCd = wheelCd = 0;
    deadCalmCd = 5.0f;
    deadCalmShield = deadCalmShieldMax = 0;
    ultDangerLast = false;
    attackTimer = 0.4f;
    // he arrives from the nearest edge, cutting through the dark
    bool fromLeft = playerPos.x > cfg::SCREEN_W * 0.5f;
    pos = { fromLeft ? -60.0f : cfg::SCREEN_W + 60.0f, cfg::GROUND_Y - h * 0.5f };
    facing = fromLeft ? 1 : -1;
    exitDir = fromLeft ? -1 : 1;
    targetPos = { playerPos.x - facing * 95.0f, playerPos.y };
    iframes = 1.0f;
    fx.Text({ playerPos.x, playerPos.y - 110 }, C(120, 190, 255), 1.5f, "GIYU TOMIOKA");
    PlaySfx(SFX_WATER, 1.0f, 0.8f);
    PlaySfx(SFX_WHOOSH, 0.8f, 0.7f);
}

void Giyu::BeginWithdraw(Effects& fx) {
    if (state == GiyuState::Inactive || state == GiyuState::Fallen ||
        state == GiyuState::Withdraw) return;

    state = GiyuState::Withdraw;
    stateTimer = 0;
    attackTimer = 0;
    tickT = 0;
    curId = -1;
    deadCalmShield = deadCalmShieldMax = 0;
    ultDangerLast = false;
    hitMem.Clear();
    facing = exitDir;
    vel = { facing * 1200.0f, 0 };
    iframes = 0.8f;
    fx.Text({ pos.x, pos.y - h - 10 }, C(120, 190, 255), 0.9f, "GIYU WITHDRAWS");
}

Rectangle Giyu::Rect() const {
    return { pos.x - w * 0.5f, pos.y - h * 0.5f, w, h };
}

void Giyu::TakeDamage(float dmg, float kbx, HitKind kind, Effects& fx) {
    if (!Active() || state == GiyuState::Arrive || state == GiyuState::Withdraw) return;
    if (iframes > 0) return;

    bool bossHit = (kind == HitKind::BossDash || kind == HitKind::BossAoe ||
                    kind == HitKind::BossProjectile);
    if (state == GiyuState::DeadCalm && deadCalmShield > 0) {
        deadCalmShield -= bossHit ? 8.0f : 3.0f;
        fx.Ring(pos, 18, 90, 420, 5, C(180, 225, 255));
        if (deadCalmShield <= 0)
            fx.Text({ pos.x, pos.y - h - 12 }, C(180, 225, 255), 0.9f, "dead calm breaks");
        return;
    }
    // Dead Calm nullifies lesser attacks — but the Demon King's own blows land
    if (state == GiyuState::DeadCalm && !bossHit) return;

    // a Hashira reads the strike before it lands... usually
    float dodge = mastery.DodgeChance();
    if (bossHit) dodge *= 0.5f;                  // Muzan is faster than any demon
    if (state != GiyuState::DeadCalm &&
        state != GiyuState::FormWhirl && state != GiyuState::FormWheel &&
        frnd(0, 1) < dodge) {
        float dir = kbx >= 0 ? 1.0f : -1.0f;
        vel.x = dir * 420.0f;                    // flow away from the blow
        iframes = 0.35f;
        fx.WaterTrail(pos, (int)-dir);
        PlaySfx(SFX_WHOOSH, 0.4f, 1.2f);
        if (state == GiyuState::FormSlash || state == GiyuState::FormTide)
            state = GiyuState::Follow;
        return;
    }

    hp -= dmg * mastery.DmgTaken();
    hitFlash = 0.15f;
    if (bossHit) {
        // even a Hashira is sent flying by the Demon King
        iframes = 0.7f;
        staggerT = 0.55f;
        state = GiyuState::Follow;
        vel.x = kbx * 1.4f;
        vel.y = -360.0f;
        fx.AddShake(0.35f);
        fx.AddHitstop(0.05f);
        fx.Ring({ pos.x, pos.y - 6 }, 8, 80, 460, 5, C(255, 120, 100));
        PlaySfx(SFX_STONE, 0.5f, 1.35f);
    } else {
        iframes = 0.5f;
        vel.x = kbx * 0.6f;
    }
    fx.BloodSpray({ pos.x, pos.y - 8 }, kbx > 0 ? 1 : -1, bossHit ? 1.8f : 1.0f);
    fx.Text({ pos.x, pos.y - h }, C(255, 110, 110), 0.9f, "-%.0f", dmg);
    PlaySfx(SFX_HURT, 0.5f, 0.85f);

    if (hp <= 0) {
        hp = 0;
        fallen = true;
        state = GiyuState::Fallen;
        stateTimer = 2.2f;
        vel = { 0, 0 };
        fx.AddShake(0.5f);
        fx.AddHitstop(0.15f);
        fx.DeathBurst(pos, C(90, 150, 220), 1.6f);
        fx.Text({ pos.x, pos.y - h - 20 }, C(220, 60, 70), 1.5f, "GIYU HAS FALLEN");
        PlaySfx(SFX_DEATH, 1.0f, 0.55f);
        mastery.Save();
    }
}

// ---------------------------------------------------------------- AI

void Giyu::PickAction(Player& player, std::vector<Enemy>& enemies,
                      Boss& boss, Akaza& akaza, UpperMoon* moon,
                      CombatSystem& cs, Effects& fx) {
    // --- threat scoring: protect the player first --------------------
    int best = -1;
    float bestScore = -1e9f;
    int crowd = 0, windups = 0;
    for (int i = 0; i < (int)enemies.size(); i++) {
        const Enemy& e = enemies[i];
        if (!e.alive) continue;
        float dG = Dist(e.pos, pos);
        float dP = Dist(e.pos, player.pos);
        if (dG < 150) crowd++;
        if (e.Busy() && dP < 190) windups++;
        float score = 320.0f - dG * 0.4f;
        if (e.Busy() && dP < 150) score += 380;      // about to hurt the player
        if (e.type == EType::Brute) score += 80;
        if (dP < 110) score += 130;
        if (score > bestScore) { bestScore = score; best = i; }
    }
    bool moonUp = moon && moon->Alive();
    targetIsBoss = (best < 0 && (boss.Alive() || akaza.Alive() || moonUp));
    if (best >= 0)          targetPos = enemies[best].pos;
    else if (akaza.Alive()) targetPos = akaza.pos;
    else if (moonUp)        targetPos = moon->pos;
    else if (targetIsBoss)  targetPos = boss.pos;
    else                    targetPos = { player.pos.x - 110.0f, player.pos.y };

    float distT = Dist(targetPos, pos);

    // --- Eleventh Form: Dead Calm (max mastery) -----------------------
    if (mastery.HasDeadCalm() && deadCalmCd <= 0) {
        bool bossThreat = boss.Alive() &&
            (boss.state == BState::Dash || boss.CrescentsNear(pos, 260) >= 2 ||
             boss.CrescentsNear(player.pos, 220) >= 2);
        bool akazaThreat = akaza.Alive() &&
            (akaza.state == AkState::DashBlow || akaza.state == AkState::Combo ||
             akaza.OrbsNear(pos, 260) >= 2 || akaza.OrbsNear(player.pos, 220) >= 2);
        bool moonThreat = moon && moon->Menacing(pos, player.pos);
        if (bossThreat || akazaThreat || moonThreat || windups >= 3) {
            state = GiyuState::DeadCalm;
            stateTimer = 2.0f;
            tickT = 0;
            deadCalmShieldMax = mastery.DeadCalmShield();
            deadCalmShield = deadCalmShieldMax;
            deadCalmCd = 20.0f;
            vel.x = 0;
            fx.Text({ pos.x, pos.y - h - 12 }, C(180, 225, 255), 1.2f, "ELEVENTH FORM: DEAD CALM");
            PlaySfx(SFX_MIST, 0.9f, 0.7f);
            return;
        }
    }

    if (attackTimer > 0 || (best < 0 && !targetIsBoss)) return;
    if (distT > 520) return;                          // close in first

    facing = targetPos.x > pos.x ? 1 : -1;

    // --- choose a form for the moment, varying them like a swordsman --
    GiyuState form;
    if (crowd >= 3 && whirlCd <= 0)  form = GiyuState::FormWhirl;
    else if (wheelCd <= 0 && distT > 150) form = GiyuState::FormWheel;
    else if (distT > 160)            form = GiyuState::FormSlash;
    else                             form = GiyuState::FormTide;

    if (form == lastForm) {           // never the same cut twice in a row
        if (form == GiyuState::FormTide)
            form = (wheelCd <= 0) ? GiyuState::FormWheel : GiyuState::FormSlash;
        else if (form == GiyuState::FormSlash)
            form = GiyuState::FormTide;
        else if (form == GiyuState::FormWheel)
            form = (distT > 160) ? GiyuState::FormSlash : GiyuState::FormTide;
    }
    lastForm = form;
    state = form;

    Color callC = C(150, 210, 255);
    switch (form) {
        case GiyuState::FormWhirl:
            stateTimer = 0.55f;
            tickT = 0;
            whirlCd = 5.0f;
            fx.Ring(pos, 14, 150, 560, 7, C(110, 190, 255));
            fx.Text({ pos.x, pos.y - h - 14 }, callC, 0.85f, "SIXTH FORM: WHIRLPOOL");
            PlaySfx(SFX_WATER, 0.8f, 0.9f);
            break;
        case GiyuState::FormWheel:
            stateTimer = 0.6f;
            wheelCd = 6.0f;
            tideHits = 0;                             // tracks the one mid-air re-arm
            curId = cs.NewId();
            vel.x = facing * 640.0f;
            vel.y = -430.0f;
            onGround = false;
            fx.Text({ pos.x, pos.y - h - 14 }, callC, 0.85f, "SECOND FORM: WATER WHEEL");
            PlaySfx(SFX_WATER, 0.85f, 1.1f);
            break;
        case GiyuState::FormSlash:
            stateTimer = 0.30f;
            curId = cs.NewId();
            fx.Text({ pos.x, pos.y - h - 14 }, callC, 0.85f, "FIRST FORM: WATER SURFACE SLASH");
            PlaySfx(SFX_SLASH, 0.6f, 0.95f);
            break;
        default:
            stateTimer = 0.5f;
            tickT = 0;
            tideHits = 0;
            fx.Text({ pos.x, pos.y - h - 14 }, callC, 0.85f, "FOURTH FORM: STRIKING TIDE");
            PlaySfx(SFX_SLASH, 0.6f, 1.1f);
            break;
    }
    attackTimer = mastery.Cadence();
}

void Giyu::Update(float dt, Player& player, std::vector<Enemy>& enemies,
                  Boss& boss, Akaza& akaza, UpperMoon* moon,
                  CombatSystem& cs, Effects& fx) {
    if (state == GiyuState::Inactive) {
        summonCd = fmaxf(summonCd - dt, 0);
        return;
    }
    if (state == GiyuState::Fallen) {
        if (stateTimer > 0) stateTimer -= dt;
        return;
    }

    hitFlash = fmaxf(hitFlash - dt, 0);
    iframes = fmaxf(iframes - dt, 0);
    staggerT = fmaxf(staggerT - dt, 0);
    attackTimer = fmaxf(attackTimer - dt, 0);
    whirlCd = fmaxf(whirlCd - dt, 0);
    wheelCd = fmaxf(wheelCd - dt, 0);
    deadCalmCd = fmaxf(deadCalmCd - dt, 0);

    bool ultDanger = UltimateDanger(boss, akaza, moon);
    if (ultDanger && !ultDangerLast &&
        state != GiyuState::Arrive && state != GiyuState::Withdraw) {
        state = GiyuState::DeadCalm;
        stateTimer = 1.75f + 0.12f * mastery.Level();
        tickT = 0;
        deadCalmShieldMax = mastery.DeadCalmShield();
        deadCalmShield = deadCalmShieldMax;
        deadCalmCd = fmaxf(deadCalmCd, 6.0f);
        activeT = fmaxf(activeT, stateTimer + 0.4f);
        vel.x = 0;
        iframes = fmaxf(iframes, 0.45f);
        fx.Text({ pos.x, pos.y - h - 16 }, C(180, 225, 255), 1.25f,
                "DEAD CALM: ULTIMATE GUARD");
        fx.Ring(pos, 20, 190, 520, 8, C(180, 225, 255));
        PlaySfx(SFX_MIST, 1.0f, 0.65f);
    }
    ultDangerLast = ultDanger;

    float dmgMul = mastery.DmgMult();

    switch (state) {
        case GiyuState::Arrive: {
            stateTimer -= dt;
            float spd = 1500.0f;
            float dx = targetPos.x - pos.x;
            if (fabsf(dx) > 20) vel.x = (dx > 0 ? 1.0f : -1.0f) * spd;
            else vel.x = 0;
            fx.WaterTrail(pos, facing);
            if (stateTimer <= 0) {
                state = GiyuState::Follow;
                vel.x = 0;
                fx.WaterBurst({ pos.x, pos.y + h * 0.3f });
                fx.AddShake(0.2f);
            }
            break;
        }
        case GiyuState::Follow: {
            if (staggerT > 0) {                  // reeling from a heavy blow
                vel.x *= 1.0f - Clampf(4.5f * dt, 0, 1);
                break;
            }
            PickAction(player, enemies, boss, akaza, moon, cs, fx);
            if (state != GiyuState::Follow) break;
            // keep a swordsman's spacing from the chosen target
            float want = targetIsBoss ? 150.0f : 85.0f;
            float dx = targetPos.x - pos.x;
            facing = dx > 0 ? 1 : -1;
            if (fabsf(dx) > want) vel.x = facing * mastery.MoveSpeed();
            else vel.x *= 1.0f - Clampf(9.0f * dt, 0, 1);
            break;
        }
        case GiyuState::FormSlash: {
            stateTimer -= dt;
            if (stateTimer > 0.18f) {
                vel.x *= 1.0f - Clampf(10.0f * dt, 0, 1);   // brief stance
            } else if (stateTimer > 0.04f) {
                vel.x = facing * 1350.0f;                   // the surface slash
                fx.WaterTrail(pos, facing);
                Rectangle r = { pos.x - 70 + facing * 40, pos.y - 40, 140, 80 };
                cs.Add(r, 26.0f * dmgMul, facing * 260.0f, -170, 0.03f,
                       Team::Player, HitKind::Giyu, curId);
            } else {
                vel.x *= 1.0f - Clampf(12.0f * dt, 0, 1);
            }
            if (stateTimer <= 0) state = GiyuState::Follow;
            break;
        }
        case GiyuState::FormTide: {
            stateTimer -= dt;
            tickT -= dt;
            vel.x *= 1.0f - Clampf(6.0f * dt, 0, 1);
            if (tickT <= 0 && tideHits < 3) {
                tickT = 0.15f;
                tideHits++;
                vel.x = facing * 260.0f;                    // step into each cut
                Rectangle r = {
                    facing > 0 ? pos.x : pos.x - 96,
                    pos.y - 38, 96, 76
                };
                cs.Add(r, 12.0f * dmgMul, facing * 200.0f, -150, 0.05f,
                       Team::Player, HitKind::Giyu, cs.NewId());
                float a0 = (tideHits % 2 == 0) ? 55.0f : -70.0f;
                float a1 = (tideHits % 2 == 0) ? -55.0f : 55.0f;
                if (facing < 0) { a0 = 180 - a0; a1 = 180 - a1; }
                fx.SlashArc(pos, 80, a0, a1, C(140, 205, 255));
                PlaySfx(SFX_SLASH, 0.45f, 1.0f + tideHits * 0.08f);
            }
            if (stateTimer <= 0) state = GiyuState::Follow;
            break;
        }
        case GiyuState::FormWhirl: {
            stateTimer -= dt;
            tickT -= dt;
            vel.x *= 1.0f - Clampf(4.0f * dt, 0, 1);
            fx.WaterTrail({ pos.x + frnd(-30, 30), pos.y + frnd(-30, 20) }, facing);
            if (tickT <= 0) {
                tickT = 0.18f;
                curId = cs.NewId();
            }
            Rectangle r = { pos.x - 130, pos.y - 66, 260, 130 };
            cs.Add(r, 12.0f * dmgMul, 0, -260, 0.03f,
                   Team::Player, HitKind::Giyu, curId);
            if (stateTimer <= 0) {
                state = GiyuState::Follow;
                fx.Ring(pos, 20, 120, 420, 6, C(140, 205, 255));
            }
            break;
        }
        case GiyuState::FormWheel: {
            stateTimer -= dt;
            fx.WaterTrail(pos, facing);
            Rectangle r = { pos.x - 40, pos.y - 44, 80, 88 };
            cs.Add(r, 17.0f * dmgMul, facing * 240.0f, -220, 0.03f,
                   Team::Player, HitKind::Giyu, curId);
            if (vel.y > 0 && tideHits == 0) {
                tideHits = 1;                                // re-arm exactly once
                curId = cs.NewId();                          // second bite on the way down
            }
            if (onGround || stateTimer <= 0) {
                state = GiyuState::Follow;
                fx.WaterBurst({ pos.x, pos.y + h * 0.4f });
            }
            break;
        }
        case GiyuState::DeadCalm: {
            stateTimer -= dt;
            tickT -= dt;
            vel.x = 0;
            // every hostile thing near him simply... stops.
            int stopped = 0;
            float calmR = 150.0f + 10.0f * mastery.Level();
            float allyR = 130.0f + 8.0f * mastery.Level();
            for (auto& hb : cs.Boxes()) {
                if (hb.team != Team::Enemy) continue;
                Vector2 c = { hb.rect.x + hb.rect.width * 0.5f,
                              hb.rect.y + hb.rect.height * 0.5f };
                if (Dist(c, pos) < calmR) {
                    hb.life = 0;
                    stopped += 2;
                    fx.Ring(c, 4, 40, 300, 3, C(190, 230, 255));
                }
            }
            int cut = boss.NullifyCrescents(pos, calmR);
            cut += boss.NullifyCrescents(player.pos, allyR);
            cut += boss.NullifyRings(pos, calmR);
            cut += boss.NullifyRings(player.pos, allyR);
            cut += akaza.NullifyOrbs(pos, calmR);
            cut += akaza.NullifyOrbs(player.pos, allyR);
            if (moon) {
                cut += moon->NullifyShards(pos, calmR);
                cut += moon->NullifyShards(player.pos, allyR);
            }
            stopped += cut;
            deadCalmShield -= stopped;
            if (cut > 0) PlaySfx(SFX_SLASH, 0.5f, 1.3f);
            if (tickT <= 0) {
                tickT = 0.4f;
                Rectangle r = { pos.x - calmR + 10, pos.y - 70, calmR * 2 - 20, 140 };
                cs.Add(r, 3.0f * dmgMul, 0, -60, 0.03f,
                       Team::Player, HitKind::Giyu, cs.NewId());
                fx.Ring(pos, 30, calmR, 320, 4, C(180, 225, 255));
            }
            if (deadCalmShield <= 0) {
                fx.Text({ pos.x, pos.y - h - 12 }, C(180, 225, 255), 0.9f,
                        "dead calm breaks");
                stateTimer = 0;
            }
            if (stateTimer <= 0) state = GiyuState::Follow;
            break;
        }
        case GiyuState::Withdraw: {
            vel.x = facing * 1200.0f;
            fx.WaterTrail(pos, facing);
            if (pos.x < -70 || pos.x > cfg::SCREEN_W + 70) {
                state = GiyuState::Inactive;
                summonCd = 0;
                vel = { 0, 0 };
                hitMem.Clear();
            }
            break;
        }
        default: break;
    }

    // physics
    if (state != GiyuState::Arrive) vel.y += cfg::GRAVITY * dt;
    else vel.y = 0;
    pos.x += vel.x * dt;
    pos.y += vel.y * dt;
    if (state != GiyuState::Withdraw && state != GiyuState::Arrive)
        pos.x = Clampf(pos.x, 30.0f, (float)cfg::SCREEN_W - 30.0f);
    onGround = GroundClamp(pos, vel, h * 0.5f);
}

// ---------------------------------------------------------------- drawing

void Giyu::Draw() const {
    if (state == GiyuState::Inactive) return;
    float gt = (float)GetTime();

    float alpha = 1.0f;
    if (state == GiyuState::Fallen) {
        alpha = Clampf(stateTimer / 2.2f, 0, 1);
        if (alpha <= 0.01f) return;
    }
    if (iframes > 0 && state != GiyuState::DeadCalm)
        alpha *= fmodf(gt * 14.0f, 2.0f) < 1.0f ? 0.55f : 1.0f;

    // shadow
    DrawEllipse((int)pos.x, (int)(cfg::GROUND_Y + 6), 20, 6, Fade(BLACK, 0.35f * alpha));

    float bx = pos.x, by = pos.y;
    bool kneel = (state == GiyuState::Fallen);
    if (kneel) by += 10;

    Color uniform = C(24, 30, 58);            // dark slayer uniform
    Color haoriR  = C(135, 32, 40);           // solid dark red half
    Color haoriG  = C(185, 145, 65);          // geometric patterned half
    Color skin    = C(235, 210, 190);
    Color hair    = C(16, 14, 20);
    if (hitFlash > 0) { uniform = C(255, 140, 130); haoriR = haoriG = uniform; }

    // legs
    DrawRectangle((int)(bx - 9), (int)(by + 8), 8, kneel ? 12 : 22, Fade(uniform, alpha));
    DrawRectangle((int)(bx + 1), (int)(by + 8), 8, kneel ? 12 : 22, Fade(C(18, 22, 44), alpha));
    // torso: uniform + split haori
    DrawRectangleRounded({ bx - 11, by - 20, 22, 30 }, 0.3f, 4, Fade(uniform, alpha));
    DrawRectangle((int)(bx - 13), (int)(by - 20), 7, 30, Fade(haoriR, alpha));
    DrawRectangle((int)(bx + 6),  (int)(by - 20), 7, 30, Fade(haoriG, alpha));
    // geometric pattern on the right half
    for (int i = 0; i < 3; i++)
        DrawRectangle((int)(bx + 7), (int)(by - 16 + i * 9), 4, 4, Fade(C(120, 70, 45), alpha));
    // white belt
    DrawRectangle((int)(bx - 11), (int)(by + 4), 22, 4, Fade(C(225, 222, 215), alpha));
    // head: long black hair, low ponytail
    Vector2 headC = { bx + facing * 2.0f, by - 28 };
    DrawCircleV(headC, 9, Fade(skin, alpha));
    DrawCircleSector(headC, 10.5f, 170, 372, 12, Fade(hair, alpha));
    DrawRectangle((int)(headC.x - facing * 12), (int)(headC.y - 2), 5, 16, Fade(hair, alpha));
    // calm, unreadable eyes
    DrawCircleV({ headC.x + facing * 4.0f, headC.y + 1 }, 1.4f, Fade(C(40, 60, 110), alpha));

    // sword
    Vector2 hand = { bx + facing * 11.0f, by - 5 };
    float ang = 40.0f;
    bool spin = false;
    switch (state) {
        case GiyuState::FormSlash: ang = 0; break;
        case GiyuState::FormTide:  ang = sinf(gt * 40.0f) * 65.0f; break;
        case GiyuState::FormWhirl: ang = fmodf(gt * 1500.0f, 360.0f); spin = true; break;
        case GiyuState::FormWheel: ang = fmodf(gt * 1100.0f, 360.0f); spin = true; break;
        case GiyuState::DeadCalm:  ang = 8.0f; break;      // perfectly level, perfectly still
        case GiyuState::Withdraw:  ang = 30.0f; break;
        case GiyuState::Fallen:    ang = 80.0f; break;
        default: ang = 40.0f; break;
    }
    if (facing < 0 && !spin) ang = 180.0f - ang;
    DrawLineEx({ bx + facing * 5.0f, by - 13 }, hand, 5, Fade(uniform, alpha));
    Rectangle blade = { hand.x, hand.y, 46, 4.5f };
    DrawRectanglePro(blade, { 0, 2.25f }, ang, Fade(C(150, 195, 255), alpha));
    Rectangle hilt = { hand.x, hand.y, 8, 7 };
    DrawRectanglePro(hilt, { 4, 3.5f }, ang, Fade(C(40, 45, 80), alpha));

    // Dead Calm stillness ripples
    if (state == GiyuState::DeadCalm) {
        float p = fmodf(gt * 1.4f, 1.0f);
        DrawRing(pos, 140 * p, 140 * p + 3, 0, 360, 40, Fade(C(180, 225, 255), 0.5f * (1 - p)));
        DrawRing(pos, 60, 63, 0, 360, 32, Fade(C(180, 225, 255), 0.25f + 0.1f * sinf(gt * 6)));
        if (deadCalmShieldMax > 0) {
            float f = Clampf(deadCalmShield / deadCalmShieldMax, 0, 1);
            DrawRectangle((int)(pos.x - 30), (int)(pos.y - h * 0.5f - 38), 60, 4, C(18, 22, 32));
            DrawRectangle((int)(pos.x - 29), (int)(pos.y - h * 0.5f - 37),
                          (int)(58 * f), 2, C(180, 225, 255));
        }
    }

    // hp bar (blue, only when hurt)
    if (hp < maxHp && Active()) {
        float bw = w + 14;
        float f = Clampf(hp / maxHp, 0, 1);
        DrawRectangle((int)(pos.x - bw * 0.5f), (int)(pos.y - h * 0.5f - 16), (int)bw, 5, C(16, 18, 26));
        DrawRectangle((int)(pos.x - bw * 0.5f + 1), (int)(pos.y - h * 0.5f - 15),
                      (int)((bw - 2) * f), 3, C(100, 175, 255));
    }
    // health ring above his head
    if (Active() && state != GiyuState::Withdraw) {
        float f = Clampf(hp / maxHp, 0, 1);
        DrawRing({ pos.x, pos.y - h * 0.5f - 26 }, 5, 8, -90, -90 + 360 * f, 24,
                 Fade(C(120, 190, 255), 0.8f));
    }
}

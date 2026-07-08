#include "game.h"
#include "config.h"
#include "audio.h"
#include <cmath>
#include <cstdio>
#include <algorithm>

static void CText(const char* t, int y, int size, Color c) {
    int w = MeasureText(t, size);
    DrawText(t, cfg::SCREEN_W / 2 - w / 2 + 2, y + 2, size, Fade(BLACK, 0.6f));
    DrawText(t, cfg::SCREEN_W / 2 - w / 2, y, size, c);
}

// style display data shared by HUD + upgrade menu
struct StyleInfo { const char* name; const char* shortName; const char* key; Color col; };
// NOTE: the "key" field mirrors StyleKeyNumber() in styles.h — every style uses
// 1, because only the equipped style ever responds to input.
static const StyleInfo STYLE_INFO[STYLE_COUNT] = {
    { "WATER",   "WTR", "1", { 80, 160, 255, 255 } },
    { "FIRE",    "FIR", "1", { 255, 130, 50, 255 } },
    { "STONE",   "STN", "1", { 185, 175, 160, 255 } },
    { "LOVE",    "LOV", "1", { 255, 130, 195, 255 } },
    { "SERPENT", "SRP", "1", { 120, 220, 90, 255 } },
    { "WIND",    "WND", "1", { 200, 240, 220, 255 } },
    { "MIST",    "MST", "1", { 175, 185, 205, 255 } },
};
static const float STYLE_CD_BASE[STYLE_COUNT] = {
    cfg::WATER_CD, cfg::FIRE_CD, cfg::STONE_CD, cfg::LOVE_CD,
    cfg::SERPENT_CD, cfg::WIND_CD, cfg::MIST_CD
};
// one-line blurbs for the Breathing Style selection menu
static const char* STYLE_DESC[STYLE_COUNT] = {
    "flowing multi-hit dash with long invulnerability",
    "explosive forward burst of flame",
    "guard-crushing ground slam",
    "healing dance of blades",
    "venomous weaving flurry",
    "sweeping twin tornadoes",
    "vanish, blink, and ambush from the fog",
};
static const char* MASTERY_DESC[STYLE_COUNT] = {
    "CONSTANT FLUX - the dash flows back through the enemy line a second time",
    "RENGOKU - wider blast that leaves burning ground behind",
    "EARTH SPLITTER - the slam launches a quake racing along the ground",
    "FIVE-STEP DANCE - five dashes, and every cut mends you twice as much",
    "TWIN FANGS - the weave ends in a venomous finishing bite",
    "TWIN CYCLONES - tornadoes tear away in both directions",
    "SEA OF CLOUDS - a lingering mist field that slows all demons inside",
};
static const char* TRACK_DESC[3] = {
    "POWER - +30% damage per level",
    "FLOW - -18% cooldown per level",
    "REACH - +20% range, duration & technique speed per level",
};

void Game::Init() {
    state = GState::Title;
    player.prog = &prog;
    player.Reset({ cfg::SCREEN_W * 0.5f, cfg::GROUND_Y - 60 });
    giyu.mastery.Load();      // his experience endures across sessions
    giyu.ResetRun();
    shinobu.mastery.Load();
    shinobu.ResetRun();
    rengoku.mastery.Load();
    rengoku.ResetRun();
    gyomei.mastery.Load();
    gyomei.ResetRun();
    tengen.mastery.Load();
    tengen.ResetRun();
}

UpperMoon* Game::ActiveMoon() {
    if (douma.Alive()) return &douma;
    if (kokushibo.Alive()) return &kokushibo;
    return nullptr;
}

void Game::DebugStart(int jump) {
    StartRun();
    if (jump > 0) {
        toSpawn = 0;
        bannerT = 0;
        enemies.clear();
        prog.points = 0;        // something to play with when testing bosses
        Vector2 spawn = { cfg::SCREEN_W * 0.68f, cfg::GROUND_Y - 60 };
        switch (jump) {
            case 1:  wave = cfg::BOSS_WAVE;  akaza.Activate(spawn); break;
            case 3:  wave = cfg::WAVE_DOUMA; akazaHandled = true;
                     douma.Activate(spawn); break;
            case 4:  wave = cfg::WAVE_KOKU;  akazaHandled = doumaHandled = true;
                     kokushibo.Activate(spawn); break;
            default: wave = cfg::WAVE_KOKU;
                     akazaHandled = doumaHandled = kokuHandled = true;
                     boss.Activate(spawn);
                     StartMuzanSurvival();
                     break;
        }
    }
    state = GState::Playing;
}

void Game::UnlockAllForTesting() {
    // --unlock-all developer flag: every Breathing Style becomes selectable
    // in the menu, and the equipped style's upgrade tree is maxed each run.
    devUnlockAll = true;
    unlocks.UnlockAll();
}

int Game::HashiraLimit() const {
    if (boss.active && !boss.Defeated()) return 5;          // Muzan: all surviving Hashira
    if (kokushibo.active && !kokushibo.Defeated()) return 3;
    if (douma.active && !douma.Defeated()) return 2;
    return 1;                                               // normal waves and Akaza
}

int Game::ActiveHashiraCount() const {
    int n = 0;
    if (giyu.Active()) n++;
    if (shinobu.Active()) n++;
    if (rengoku.Active()) n++;
    if (gyomei.Active()) n++;
    if (tengen.Active()) n++;
    return n;
}

int Game::HashiraSlotsUsed() const {
    int n = 0;
    if (giyuCommitted) n++;
    if (shinobuCommitted) n++;
    if (rengokuCommitted) n++;
    if (gyomeiCommitted) n++;
    if (tengenCommitted) n++;
    return n;
}

bool Game::TrySummonHashira(int which) {
    if (boss.active && !boss.Defeated()) {
        fx.Text({ player.pos.x, player.pos.y - 110 }, C(240, 170, 100), 0.9f,
                "all surviving hashira join Muzan automatically");
        return false;
    }

    const int limit = HashiraLimit();
    if (HashiraSlotsUsed() >= limit) {
        fx.Text({ player.pos.x, player.pos.y - 94 }, C(190, 170, 140), 0.9f,
                "hashira slots committed (%d/%d)", HashiraSlotsUsed(), limit);
        PlaySfx(SFX_PICKUP, 0.35f, 0.55f);
        return false;
    }

    switch (which) {
        case 0:
            if (giyuCommitted) {
                fx.Text({ player.pos.x, player.pos.y - 86 }, C(120, 190, 255), 0.9f,
                        "giyu is already committed");
                return false;
            }
            if (giyu.fallen) {
                fx.Text({ player.pos.x, player.pos.y - 86 }, C(200, 70, 80), 1.0f,
                        "GIYU HAS FALLEN THIS RUN");
                return false;
            }
            if (giyu.state != GiyuState::Inactive) {
                fx.Text({ player.pos.x, player.pos.y - 86 }, C(120, 190, 255), 0.9f,
                        "giyu is already deployed");
                return false;
            }
            giyu.Summon(player.pos, fx);
            giyuCommitted = true;
            return true;
        case 1:
            if (shinobuCommitted) {
                fx.Text({ player.pos.x, player.pos.y - 104 }, C(190, 150, 255), 0.9f,
                        "shinobu is already committed");
                return false;
            }
            if (shinobu.fallen) {
                fx.Text({ player.pos.x, player.pos.y - 104 }, C(220, 70, 120), 1.0f,
                        "SHINOBU HAS FALLEN THIS RUN");
                return false;
            }
            if (shinobu.state != ShinobuState::Inactive) {
                fx.Text({ player.pos.x, player.pos.y - 104 }, C(190, 150, 255), 0.9f,
                        "shinobu is already deployed");
                return false;
            }
            shinobu.Summon(player.pos, fx);
            shinobuCommitted = true;
            return true;
        case 2:
            if (rengokuCommitted) {
                fx.Text({ player.pos.x, player.pos.y - 122 }, C(255, 150, 55), 0.9f,
                        "rengoku is already committed");
                return false;
            }
            if (rengoku.fallen) {
                fx.Text({ player.pos.x, player.pos.y - 122 }, C(240, 90, 70), 1.0f,
                        "RENGOKU HAS FALLEN THIS RUN");
                return false;
            }
            if (rengoku.state != RengokuState::Inactive) {
                fx.Text({ player.pos.x, player.pos.y - 122 }, C(255, 150, 55), 0.9f,
                        "rengoku is already deployed");
                return false;
            }
            rengoku.Summon(player.pos, fx);
            rengokuCommitted = true;
            return true;
        case 3:
            if (gyomeiCommitted) {
                fx.Text({ player.pos.x, player.pos.y - 140 }, C(188, 178, 158), 0.9f,
                        "gyomei is already committed");
                return false;
            }
            if (gyomei.fallen) {
                fx.Text({ player.pos.x, player.pos.y - 140 }, C(215, 70, 65), 1.0f,
                        "GYOMEI HAS FALLEN THIS RUN");
                return false;
            }
            if (gyomei.state != GyomeiState::Inactive) {
                fx.Text({ player.pos.x, player.pos.y - 140 }, C(188, 178, 158), 0.9f,
                        "gyomei is already deployed");
                return false;
            }
            gyomei.Summon(player.pos, fx);
            gyomeiCommitted = true;
            return true;
        default:
            if (tengenCommitted) {
                fx.Text({ player.pos.x, player.pos.y - 158 }, C(255, 212, 88), 0.9f,
                        "tengen is already committed");
                return false;
            }
            if (tengen.fallen) {
                fx.Text({ player.pos.x, player.pos.y - 158 }, C(230, 75, 70), 1.0f,
                        "TENGEN HAS FALLEN THIS RUN");
                return false;
            }
            if (tengen.state != TengenState::Inactive) {
                fx.Text({ player.pos.x, player.pos.y - 158 }, C(255, 212, 88), 0.9f,
                        "tengen is already deployed");
                return false;
            }
            tengen.Summon(player.pos, fx);
            tengenCommitted = true;
            return true;
    }
}

void Game::AutoSummonMuzanHashira() {
    if (!boss.active || boss.Defeated()) return;
    if (!giyu.fallen && !giyuCommitted && giyu.state == GiyuState::Inactive) {
        giyu.Summon(player.pos, fx);
        giyuCommitted = true;
    }
    if (!shinobu.fallen && !shinobuCommitted && shinobu.state == ShinobuState::Inactive) {
        shinobu.Summon(player.pos, fx);
        shinobuCommitted = true;
    }
    if (!rengoku.fallen && !rengokuCommitted && rengoku.state == RengokuState::Inactive) {
        rengoku.Summon(player.pos, fx);
        rengokuCommitted = true;
    }
    if (!gyomei.fallen && !gyomeiCommitted && gyomei.state == GyomeiState::Inactive) {
        gyomei.Summon(player.pos, fx);
        gyomeiCommitted = true;
    }
    if (!tengen.fallen && !tengenCommitted && tengen.state == TengenState::Inactive) {
        tengen.Summon(player.pos, fx);
        tengenCommitted = true;
    }
}

void Game::EndHashiraEncounter() {
    bool anyActive = false;
    bool anyRecovered = false;

    if (!giyu.fallen) {
        anyActive = anyActive || giyu.Active();
        giyu.maxHp = giyu.mastery.MaxHp();
        float before = giyu.hp;
        giyu.hp = fminf(giyu.maxHp, giyu.hp + giyu.maxHp * 0.20f);
        anyRecovered = anyRecovered || giyu.hp > before + 0.5f;
        if (giyu.Active()) {
            giyu.mastery.xp += 5;
            giyu.activeT = 0;
            giyu.summonCd = 0;
            giyu.mastery.Save();
            giyu.BeginWithdraw(fx);
        }
    }
    if (!shinobu.fallen) {
        anyActive = anyActive || shinobu.Active();
        shinobu.maxHp = shinobu.mastery.MaxHp();
        float before = shinobu.hp;
        shinobu.hp = fminf(shinobu.maxHp, shinobu.hp + shinobu.maxHp * 0.20f);
        anyRecovered = anyRecovered || shinobu.hp > before + 0.5f;
        if (shinobu.Active()) {
            shinobu.mastery.xp += 5;
            shinobu.activeT = 0;
            shinobu.summonCd = 0;
            shinobu.mastery.Save();
            shinobu.BeginWithdraw(fx);
        }
    }
    if (!rengoku.fallen) {
        anyActive = anyActive || rengoku.Active();
        rengoku.maxHp = rengoku.mastery.MaxHp();
        float before = rengoku.hp;
        rengoku.hp = fminf(rengoku.maxHp, rengoku.hp + rengoku.maxHp * 0.20f);
        anyRecovered = anyRecovered || rengoku.hp > before + 0.5f;
        if (rengoku.Active()) {
            rengoku.mastery.xp += 5;
            rengoku.activeT = 0;
            rengoku.summonCd = 0;
            rengoku.mastery.Save();
            rengoku.BeginWithdraw(fx);
        }
    }
    if (!gyomei.fallen) {
        anyActive = anyActive || gyomei.Active();
        gyomei.maxHp = gyomei.mastery.MaxHp();
        float before = gyomei.hp;
        gyomei.hp = fminf(gyomei.maxHp, gyomei.hp + gyomei.maxHp * 0.20f);
        anyRecovered = anyRecovered || gyomei.hp > before + 0.5f;
        if (gyomei.Active()) {
            gyomei.mastery.xp += 5;
            gyomei.activeT = 0;
            gyomei.summonCd = 0;
            gyomei.mastery.Save();
            gyomei.BeginWithdraw(fx);
        }
    }
    if (!tengen.fallen) {
        anyActive = anyActive || tengen.Active();
        tengen.maxHp = tengen.mastery.MaxHp();
        float before = tengen.hp;
        tengen.hp = fminf(tengen.maxHp, tengen.hp + tengen.maxHp * 0.20f);
        anyRecovered = anyRecovered || tengen.hp > before + 0.5f;
        if (tengen.Active()) {
            tengen.mastery.xp += 5;
            tengen.activeT = 0;
            tengen.summonCd = 0;
            tengen.mastery.Save();
            tengen.BeginWithdraw(fx);
        }
    }

    giyuCommitted = false;
    shinobuCommitted = false;
    rengokuCommitted = false;
    gyomeiCommitted = false;
    tengenCommitted = false;

    if (anyActive) {
        fx.Text({ player.pos.x, player.pos.y - 100 }, C(255, 215, 120), 1.0f,
                "hashira withdraw");
    }
    if (anyRecovered) {
        fx.Text({ player.pos.x, player.pos.y - 78 }, C(120, 220, 150), 0.9f,
                "surviving hashira recover 20%%");
    }
}

void Game::UpdateHashiraWithdrawals(float dt) {
    UpperMoon* moon = ActiveMoon();
    if (giyu.state == GiyuState::Withdraw)
        giyu.Update(dt, player, enemies, boss, akaza, moon, combat, fx);
    if (shinobu.state == ShinobuState::Withdraw)
        shinobu.Update(dt, player, enemies, boss, akaza, moon, combat, fx);
    if (rengoku.state == RengokuState::Withdraw)
        rengoku.Update(dt, player, enemies, boss, akaza, moon, combat, fx);
    if (gyomei.state == GyomeiState::Withdraw)
        gyomei.Update(dt, player, enemies, boss, akaza, moon, combat, fx);
    if (tengen.state == TengenState::Withdraw)
        tengen.Update(dt, player, enemies, boss, akaza, moon, combat, fx);
}

void Game::StartMuzanSurvival() {
    muzanTimer = 300.0f;
    muzanSurvival = true;
    sunriseFinale = false;
    sunriseOutroT = 0;
    bossDefeatHandled = false;
    fx.Text({ cfg::SCREEN_W * 0.5f, cfg::GROUND_Y - 230.0f }, C(255, 190, 130), 1.15f,
            "SURVIVE UNTIL SUNRISE");
}

void Game::BeginSunriseFinale() {
    if (sunriseFinale || !boss.active) return;
    sunriseFinale = true;
    sunriseOutroT = 6.2f;
    muzanTimer = 0;
    combat.Clear();
    enemies.clear();
    toSpawn = 0;
    player.iframes = fmaxf(player.iframes, sunriseOutroT);
    boss.BeginSunriseDeath(fx);
    fx.Text({ cfg::SCREEN_W * 0.5f, 180.0f }, C(255, 230, 160), 1.5f,
            "DAWN BREAKS");
}

void Game::StartRun() {
    prog.Reset();
    if (devUnlockAll) prog.UnlockAll();   // --unlock-all: max the equipped style's tree
    player.prog = &prog;
    player.Reset({ cfg::SCREEN_W * 0.5f, cfg::GROUND_Y - 60 });
    // equip the chosen Breathing Style for the whole run (fall back if it is locked)
    if (!unlocks.IsUnlocked(selectedStyle)) selectedStyle = unlocks.FirstUnlocked();
    player.equipped = selectedStyle;
    player.invincible = devInvincible;
    player.maxHp = 200;
    player.hp = 200;
    enemies.clear();
    pickups.clear();
    boss.Reset();
    akaza.Reset();
    douma.Reset();
    kokushibo.Reset();
    SetBossDrone(0);           // clear Kokushibo's dread from any prior run
    combat.Clear();
    fx.Reset();
    giyu.ResetRun();
    shinobu.ResetRun();
    rengoku.ResetRun();
    gyomei.ResetRun();
    tengen.ResetRun();
    giyuCommitted = shinobuCommitted = rengokuCommitted = gyomeiCommitted =
        tengenCommitted = false;
    kills = 0;
    elapsed = 0;
    deathT = 0;
    bossDefeatHandled = false;
    introTarget = 0;
    akazaHandled = doumaHandled = kokuHandled = false;
    moonFallT = 0;
    victoryTime = 0;
    muzanTimer = 300.0f;
    muzanSurvival = false;
    sunriseFinale = false;
    sunriseOutroT = 0;
    StartWave(1);
    state = GState::Playing;
}

void Game::ReturnToTitle() {
    // preserve any Hashira mastery earned this run, then tear the run down
    giyu.mastery.Save();
    shinobu.mastery.Save();
    rengoku.mastery.Save();
    gyomei.mastery.Save();
    tengen.mastery.Save();
    enemies.clear();
    pickups.clear();
    boss.Reset();
    akaza.Reset();
    douma.Reset();
    kokushibo.Reset();
    SetBossDrone(0);
    combat.Clear();
    fx.Reset();
    giyu.ResetRun();
    shinobu.ResetRun();
    rengoku.ResetRun();
    gyomei.ResetRun();
    tengen.ResetRun();
    giyuCommitted = shinobuCommitted = rengokuCommitted = gyomeiCommitted =
        tengenCommitted = false;
    toSpawn = 0;
    muzanSurvival = false;
    sunriseFinale = false;
    state = GState::Title;
}

void Game::StartWave(int n) {
    wave = n;
    toSpawn = (int)fminf(36, 6 + n * 2.0f + (n >= 8 ? 4 : 0));
    spawnTimer = 1.2f;
    bannerT = 2.2f;
    if (n > 1) {
        player.Heal(20, fx);
        prog.points += cfg::PTS_PER_WAVE;
        PlaySfx(SFX_UPGRADE, 0.5f, 0.8f);
    }
}

void Game::SpawnDemonAtEdge(int waveNum) {
    bool leftSide = GetRandomValue(0, 1) == 0;
    float jitter = frnd(0, 70);      // stagger spawn spots so packs don't overlap
    float x = leftSide ? -50.0f - jitter : cfg::SCREEN_W + 50.0f + jitter;

    // the horde hardens as the night wears on
    EType t = EType::Basic;
    int roll = GetRandomValue(0, 99);
    int bruteC = waveNum >= 3 ? (int)fminf(30, 8 + 2.0f * waveNum) : 0;
    int fastC  = waveNum >= 2 ? (int)fminf(55, 20 + 2.5f * waveNum) : 0;
    if (roll < bruteC) t = EType::Brute;
    else if (roll < bruteC + fastC) t = EType::Fast;

    enemies.emplace_back(t, Vector2{ x, cfg::GROUND_Y - 40 }, waveNum);
}

void Game::SpawnEnemy() {
    if ((int)enemies.size() >= cfg::MAX_ALIVE) return;
    SpawnDemonAtEdge(wave);
    toSpawn--;
}

void Game::SeparateEnemies(float dt) {
    for (size_t i = 0; i < enemies.size(); i++) {
        for (size_t j = i + 1; j < enemies.size(); j++) {
            Enemy& a = enemies[i];
            Enemy& b = enemies[j];
            if (!a.alive || !b.alive) continue;
            if (CheckCollisionRecs(a.Rect(), b.Rect())) {
                // firm spacing so demons never stack inside each other
                // (attackers get a gentler push so lunges still connect)
                float push = (a.Busy() || b.Busy()) ? 28.0f * dt : 90.0f * dt;
                if (a.pos.x < b.pos.x) { a.pos.x -= push; b.pos.x += push; }
                else if (a.pos.x > b.pos.x) { a.pos.x += push; b.pos.x -= push; }
                else { a.pos.x -= push; b.pos.x += push; }   // perfectly stacked: force apart
            }
        }
    }
}

void Game::ResolveCombat() {
    for (auto& hb : combat.Boxes()) {
        if (hb.life <= 0) continue;      // nullified (Dead Calm) or expired
        if (hb.team == Team::Player) {
            for (auto& e : enemies) {
                if (!e.alive || e.hitMem.Seen(hb.attackId)) continue;
                if (!CheckCollisionRecs(hb.rect, e.Rect())) continue;
                e.hitMem.Remember(hb.attackId);
                e.TakeDamage(hb.damage, hb.kbX, hb.kind, fx);
                if (hb.damage > 0) {
                    fx.AddHitstop(hb.damage >= 20 ? 0.045f : 0.02f);  // hit pause = weight
                    PlaySfx(SFX_HIT, 0.55f);
                    if (hb.kind == HitKind::Love) {                    // love mends
                        float mend = prog.Mastery(STYLE_LOVE) ? 4.0f : 2.0f;
                        player.hp = fminf(player.hp + mend, player.maxHp);
                    }
                }
                if (!e.alive && hb.kind == HitKind::Giyu && giyu.mastery.xp < 999) {
                    giyu.mastery.xp++;                                 // his blade remembers
                    giyu.mastery.kills++;
                }
                if (!e.alive && hb.kind == HitKind::Shinobu && shinobu.mastery.xp < 999) {
                    shinobu.mastery.xp++;
                    shinobu.mastery.kills++;
                }
                if (!e.alive && hb.kind == HitKind::Rengoku && rengoku.mastery.xp < 999) {
                    rengoku.mastery.xp++;
                    rengoku.mastery.kills++;
                }
                if (!e.alive && hb.kind == HitKind::Gyomei && gyomei.mastery.xp < 999) {
                    gyomei.mastery.xp++;
                    gyomei.mastery.kills++;
                }
                if (!e.alive && hb.kind == HitKind::Tengen && tengen.mastery.xp < 999) {
                    tengen.mastery.xp++;
                    tengen.mastery.kills++;
                }
            }
            if (boss.Alive() && !boss.hitMem.Seen(hb.attackId) &&
                CheckCollisionRecs(hb.rect, boss.Rect())) {
                boss.hitMem.Remember(hb.attackId);
                boss.TakeDamage(hb.damage, hb.kbX, hb.kind, fx);
                if (hb.kind == HitKind::Giyu && hb.damage >= 15 &&
                    boss.ForceOpening(fx))
                    giyu.mastery.xp += 3;                              // openings teach him
                if (hb.kind == HitKind::Shinobu && hb.damage >= 10 &&
                    boss.ForceOpening(fx))
                    shinobu.mastery.xp += 3;
                if (hb.kind == HitKind::Rengoku && hb.damage >= 24 &&
                    boss.ForceOpening(fx))
                    rengoku.mastery.xp += 3;
                if (hb.kind == HitKind::Gyomei && hb.damage >= 28 &&
                    boss.ForceOpening(fx))
                    gyomei.mastery.xp += 3;
                if (hb.kind == HitKind::Tengen && hb.damage >= 18 &&
                    boss.ForceOpening(fx))
                    tengen.mastery.xp += 3;
                if (hb.damage > 0) {
                    fx.AddHitstop(hb.kind == HitKind::Fire ? 0.04f : 0.015f);
                    PlaySfx(SFX_HIT, 0.6f, 0.85f);
                    if (hb.kind == HitKind::Love) {
                        float mend = prog.Mastery(STYLE_LOVE) ? 4.0f : 2.0f;
                        player.hp = fminf(player.hp + mend, player.maxHp);
                    }
                }
            }
            if (akaza.Alive() && !akaza.hitMem.Seen(hb.attackId) &&
                CheckCollisionRecs(hb.rect, akaza.Rect())) {
                akaza.hitMem.Remember(hb.attackId);
                akaza.TakeDamage(hb.damage, hb.kbX, hb.kind, fx);
                if (hb.kind == HitKind::Giyu && hb.damage >= 15 &&
                    akaza.ForceOpening(fx))
                    giyu.mastery.xp += 3;
                if (hb.kind == HitKind::Shinobu && hb.damage >= 10 &&
                    akaza.ForceOpening(fx))
                    shinobu.mastery.xp += 3;
                if (hb.kind == HitKind::Rengoku && hb.damage >= 24 &&
                    akaza.ForceOpening(fx))
                    rengoku.mastery.xp += 3;
                if (hb.kind == HitKind::Gyomei && hb.damage >= 28 &&
                    akaza.ForceOpening(fx))
                    gyomei.mastery.xp += 3;
                if (hb.kind == HitKind::Tengen && hb.damage >= 18 &&
                    akaza.ForceOpening(fx))
                    tengen.mastery.xp += 3;
                if (hb.damage > 0) {
                    fx.AddHitstop(hb.kind == HitKind::Fire ? 0.04f : 0.015f);
                    PlaySfx(SFX_HIT, 0.6f, 1.0f);
                    if (hb.kind == HitKind::Love) {
                        float mend = prog.Mastery(STYLE_LOVE) ? 4.0f : 2.0f;
                        player.hp = fminf(player.hp + mend, player.maxHp);
                    }
                }
            }
            UpperMoon* moonsArr[2] = { &douma, &kokushibo };
            for (UpperMoon* m : moonsArr) {
                if (!m->Alive() || m->hitMem.Seen(hb.attackId)) continue;
                if (!CheckCollisionRecs(hb.rect, m->Rect())) continue;
                m->hitMem.Remember(hb.attackId);
                m->TakeDamage(hb.damage, hb.kbX, hb.kind, fx);
                if (hb.kind == HitKind::Giyu && hb.damage >= 15 &&
                    m->ForceOpening(fx))
                    giyu.mastery.xp += 3;
                if (hb.kind == HitKind::Shinobu && hb.damage >= 10 &&
                    m->ForceOpening(fx))
                    shinobu.mastery.xp += 3;
                if (hb.kind == HitKind::Rengoku && hb.damage >= 24 &&
                    m->ForceOpening(fx))
                    rengoku.mastery.xp += 3;
                if (hb.kind == HitKind::Gyomei && hb.damage >= 28 &&
                    m->ForceOpening(fx))
                    gyomei.mastery.xp += 3;
                if (hb.kind == HitKind::Tengen && hb.damage >= 18 &&
                    m->ForceOpening(fx))
                    tengen.mastery.xp += 3;
                if (hb.damage > 0) {
                    fx.AddHitstop(hb.kind == HitKind::Fire ? 0.04f : 0.015f);
                    PlaySfx(SFX_HIT, 0.6f, 0.95f);
                    if (hb.kind == HitKind::Love) {
                        float mend = prog.Mastery(STYLE_LOVE) ? 4.0f : 2.0f;
                        player.hp = fminf(player.hp + mend, player.maxHp);
                    }
                }
            }
        } else {
            if (giyu.Active() && !giyu.hitMem.Seen(hb.attackId) &&
                CheckCollisionRecs(hb.rect, giyu.Rect())) {
                giyu.hitMem.Remember(hb.attackId);
                giyu.TakeDamage(hb.damage, hb.kbX, hb.kind, fx);
            }
            if (shinobu.Active() && !shinobu.hitMem.Seen(hb.attackId) &&
                CheckCollisionRecs(hb.rect, shinobu.Rect())) {
                shinobu.hitMem.Remember(hb.attackId);
                shinobu.TakeDamage(hb.damage, hb.kbX, hb.kind, fx);
            }
            if (rengoku.Active() && !rengoku.hitMem.Seen(hb.attackId) &&
                CheckCollisionRecs(hb.rect, rengoku.Rect())) {
                rengoku.hitMem.Remember(hb.attackId);
                rengoku.TakeDamage(hb.damage, hb.kbX, hb.kind, fx);
            }
            if (gyomei.Active() && !gyomei.hitMem.Seen(hb.attackId) &&
                CheckCollisionRecs(hb.rect, gyomei.Rect())) {
                gyomei.hitMem.Remember(hb.attackId);
                gyomei.TakeDamage(hb.damage, hb.kbX, hb.kind, fx);
            }
            if (tengen.Active() && !tengen.hitMem.Seen(hb.attackId) &&
                CheckCollisionRecs(hb.rect, tengen.Rect())) {
                tengen.hitMem.Remember(hb.attackId);
                tengen.TakeDamage(hb.damage, hb.kbX, hb.kind, fx);
            }
            if (player.hitMem.Seen(hb.attackId)) continue;
            if (!CheckCollisionRecs(hb.rect, player.Rect())) continue;
            // the Demon King's blows send slayers flying
            bool heavy = hb.kind == HitKind::BossDash || hb.kind == HitKind::BossAoe;
            if (player.TakeDamage(hb.damage, hb.kbX, fx, heavy))
                player.hitMem.Remember(hb.attackId);
        }
    }
}

void Game::UpdatePlaying(float dt) {
    elapsed += dt;
    if (bannerT > 0) bannerT -= dt;

    // hint for homing styles: nearest living target
    player.hasHunt = false;
    float best = 1e9f;
    for (const auto& e : enemies) {
        if (!e.alive) continue;
        float d = fabsf(e.pos.x - player.pos.x);
        if (d < best) { best = d; player.huntX = e.pos.x; player.hasHunt = true; }
    }
    if (boss.Alive()) {
        float d = fabsf(boss.pos.x - player.pos.x);
        if (d < best) { player.huntX = boss.pos.x; player.hasHunt = true; }
    }

    player.Update(dt, combat, fx);
    AutoSummonMuzanHashira();

    // spawning (paused during wave banner; ramps up slightly over time)
    if (toSpawn > 0 && bannerT <= 0) {
        spawnTimer -= dt;
        if (spawnTimer <= 0) {
            SpawnEnemy();
            float base = fmaxf(0.4f, 1.8f - 0.12f * (wave - 1));
            base *= fmaxf(0.7f, 1.0f - elapsed * 0.002f);   // creeping ramp
            spawnTimer = base * frnd(0.8f, 1.2f);
        }
    }

    bool hidden = player.hiddenT > 0;
    for (auto& e : enemies) {
        // demons attack whichever slayer is closer; a hidden player is ignored
        Vector2 tgt = player.pos;
        bool tgtHidden = hidden;
        float bestD = hidden ? 1e9f : fabsf(player.pos.x - e.pos.x);
        if (giyu.Active() && fabsf(giyu.pos.x - e.pos.x) < bestD) {
            tgt = giyu.pos;
            tgtHidden = false;
            bestD = fabsf(giyu.pos.x - e.pos.x);
        }
        if (shinobu.Active() && fabsf(shinobu.pos.x - e.pos.x) < bestD) {
            tgt = shinobu.pos;
            tgtHidden = false;
            bestD = fabsf(shinobu.pos.x - e.pos.x);
        }
        if (rengoku.Active() && fabsf(rengoku.pos.x - e.pos.x) < bestD) {
            tgt = rengoku.pos;
            tgtHidden = false;
            bestD = fabsf(rengoku.pos.x - e.pos.x);
        }
        if (gyomei.Active() && fabsf(gyomei.pos.x - e.pos.x) < bestD) {
            tgt = gyomei.pos;
            tgtHidden = false;
            bestD = fabsf(gyomei.pos.x - e.pos.x);
        }
        if (tengen.Active() && fabsf(tengen.pos.x - e.pos.x) < bestD) {
            tgt = tengen.pos;
            tgtHidden = false;
        }
        e.Update(dt, tgt, combat, fx, tgtHidden);
    }
    SeparateEnemies(dt);

    int summonReq = 0;
    boss.Update(dt, player, &giyu, &shinobu, &rengoku, &gyomei, &tengen, combat, fx, summonReq);
    if (muzanSurvival && boss.active && !boss.Defeated()) {
        if (!sunriseFinale && boss.Alive()) {
            muzanTimer = fmaxf(0, muzanTimer - dt);
            if (muzanTimer <= 0) BeginSunriseFinale();
        } else if (sunriseFinale) {
            sunriseOutroT = fmaxf(sunriseOutroT - dt, 0);
        }
    }
    for (int i = 0; i < summonReq; i++) {
        if (!sunriseFinale && (int)enemies.size() < cfg::MAX_ALIVE)
            SpawnDemonAtEdge(cfg::WAVE_KOKU);   // endgame-hardened demons
    }

    akaza.Update(dt, player, &giyu, &shinobu, &rengoku, &gyomei, &tengen, combat, fx);
    douma.Update(dt, player, &giyu, &shinobu, &rengoku, &gyomei, &tengen, combat, fx);
    kokushibo.Update(dt, player, &giyu, &shinobu, &rengoku, &gyomei, &tengen, combat, fx);
    giyu.Update(dt, player, enemies, boss, akaza, ActiveMoon(), combat, fx);
    shinobu.Update(dt, player, enemies, boss, akaza, ActiveMoon(), combat, fx);
    rengoku.Update(dt, player, enemies, boss, akaza, ActiveMoon(), combat, fx);
    gyomei.Update(dt, player, enemies, boss, akaza, ActiveMoon(), combat, fx);
    tengen.Update(dt, player, enemies, boss, akaza, ActiveMoon(), combat, fx);

    ResolveCombat();          // resolve first: every hitbox lands at least once,
    combat.Update(dt);        // even on a slow frame

    // kill accounting (combat kills AND poison kills), loot drops
    for (auto& e : enemies) {
        if (!e.alive && !e.counted) {
            e.counted = true;
            kills++;
            if (GetRandomValue(0, 99) < 18) {   // heal orb drop
                Pickup p;
                p.pos = { e.pos.x, e.pos.y - 10 };
                p.vel = { frnd(-40, 40), -220 };
                pickups.push_back(p);
            }
        }
    }
    enemies.erase(std::remove_if(enemies.begin(), enemies.end(),
                  [](const Enemy& e) { return !e.alive; }), enemies.end());

    // pickups
    for (auto& p : pickups) {
        p.life -= dt;
        p.vel.y += cfg::GRAVITY * 0.6f * dt;
        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
        if (p.pos.y > cfg::GROUND_Y - 10) { p.pos.y = cfg::GROUND_Y - 10; p.vel.y *= -0.45f; }
        if (CheckCollisionCircleRec(p.pos, 10, player.Rect())) {
            player.Heal(12, fx);
            PlaySfx(SFX_PICKUP, 0.6f);
            p.life = 0;
        }
    }
    pickups.erase(std::remove_if(pickups.begin(), pickups.end(),
                  [](const Pickup& p) { return p.life <= 0; }), pickups.end());

    // boss ambience
    if (boss.Alive() && GetRandomValue(0, 5) == 0)
        fx.Ember({ frnd(0, (float)cfg::SCREEN_W), cfg::GROUND_Y - frnd(0, 60) });

    // --- flow control -------------------------------------------
    if (player.state == PState::Dead) {
        deathT += dt;
        if (deathT > 1.6f) {
            giyu.mastery.Save();
            shinobu.mastery.Save();
            rengoku.mastery.Save();
            gyomei.mastery.Save();
            tengen.mastery.Save();
            SetBossDrone(0);
            state = GState::GameOver;
        }
        return;
    }

    if (moonFallT > 0) moonFallT -= dt;

    // wave cleared? the gauntlet decides what comes next
    bool anyEncounterActive = (boss.active && !boss.Defeated()) || akaza.Alive() ||
                              douma.Alive() || kokushibo.Alive();
    bool defeatedEncounterPending = (akaza.Defeated() && !akazaHandled) ||
                                    (douma.Defeated() && !doumaHandled) ||
                                    (kokushibo.Defeated() && !kokuHandled) ||
                                    (boss.Defeated() && !bossDefeatHandled);
    if (!anyEncounterActive && !defeatedEncounterPending &&
        toSpawn <= 0 && enemies.empty()) {
        EndHashiraEncounter();
        if (wave >= cfg::WAVE_KOKU && !kokuHandled && !kokushibo.active) {
            introTarget = 2;
            prog.points += cfg::PTS_PER_WAVE;
            state = GState::BossIntro;
            introT = 4.2f;                 // the strongest Upper Moon takes his time
            SetBossDrone(1);               // the night changes the instant he stirs
            PlaySfx(SFX_ROAR, 1.0f, 0.5f);
        } else if (wave >= cfg::WAVE_DOUMA && !doumaHandled && !douma.active) {
            introTarget = 1;
            prog.points += cfg::PTS_PER_WAVE;
            state = GState::BossIntro;
            introT = 3.0f;
            PlaySfx(SFX_MIST, 1.0f, 0.6f);
        } else if (wave >= cfg::BOSS_WAVE && !akazaHandled && !akaza.active) {
            introTarget = 0;
            prog.points += cfg::PTS_PER_WAVE;
            state = GState::BossIntro;
            introT = 2.8f;
            PlaySfx(SFX_ROAR, 0.8f, 1.1f);
        } else if (wave < cfg::WAVE_KOKU) {
            StartWave(wave + 1);
        }
    }

    // fallen Upper Moons: the horde presses on toward the next milestone
    if (akaza.Defeated() && !akazaHandled) {
        akazaHandled = true;
        prog.points += 4;
        player.Heal(40, fx);
        if (giyu.summonedThisRun && !giyu.fallen) giyu.mastery.xp += 6;
        if (shinobu.summonedThisRun && !shinobu.fallen) shinobu.mastery.xp += 6;
        if (rengoku.summonedThisRun && !rengoku.fallen) rengoku.mastery.xp += 6;
        if (gyomei.summonedThisRun && !gyomei.fallen) gyomei.mastery.xp += 6;
        if (tengen.summonedThisRun && !tengen.fallen) tengen.mastery.xp += 6;
        EndHashiraEncounter();
        giyu.mastery.Save();
        shinobu.mastery.Save();
        rengoku.mastery.Save();
        gyomei.mastery.Save();
        tengen.mastery.Save();
        moonFallT = 3.5f;
        moonFallText = "UPPER MOON THREE FALLS";
        akaza.Reset();
        StartWave(wave + 1);
    }
    if (douma.Defeated() && !doumaHandled) {
        doumaHandled = true;
        prog.points += 5;
        player.Heal(40, fx);
        if (giyu.summonedThisRun && !giyu.fallen) giyu.mastery.xp += 8;
        if (shinobu.summonedThisRun && !shinobu.fallen) shinobu.mastery.xp += 8;
        if (rengoku.summonedThisRun && !rengoku.fallen) rengoku.mastery.xp += 8;
        if (gyomei.summonedThisRun && !gyomei.fallen) gyomei.mastery.xp += 8;
        if (tengen.summonedThisRun && !tengen.fallen) tengen.mastery.xp += 8;
        EndHashiraEncounter();
        giyu.mastery.Save();
        shinobu.mastery.Save();
        rengoku.mastery.Save();
        gyomei.mastery.Save();
        tengen.mastery.Save();
        moonFallT = 3.5f;
        moonFallText = "UPPER MOON TWO FALLS";
        douma.Reset();
        StartWave(wave + 1);
    }
    // the last moon falls - only the Demon King remains
    if (kokushibo.Defeated() && !kokuHandled) {
        kokuHandled = true;
        prog.points += 6;
        player.Heal(50, fx);
        if (giyu.summonedThisRun && !giyu.fallen) giyu.mastery.xp += 10;
        if (shinobu.summonedThisRun && !shinobu.fallen) shinobu.mastery.xp += 10;
        if (rengoku.summonedThisRun && !rengoku.fallen) rengoku.mastery.xp += 10;
        if (gyomei.summonedThisRun && !gyomei.fallen) gyomei.mastery.xp += 10;
        if (tengen.summonedThisRun && !tengen.fallen) tengen.mastery.xp += 10;
        EndHashiraEncounter();
        giyu.mastery.Save();
        shinobu.mastery.Save();
        rengoku.mastery.Save();
        gyomei.mastery.Save();
        tengen.mastery.Save();
        kokushibo.Reset();
        introTarget = 3;
        state = GState::BossIntro;
        introT = 4.6f;
        PlaySfx(SFX_ROAR, 0.9f, 0.7f);
    }

    // boss defeated?
    if (boss.Defeated() && !bossDefeatHandled) {
        bossDefeatHandled = true;
        victoryTime = elapsed;
        muzanSurvival = false;
        sunriseFinale = false;
        if (giyu.summonedThisRun && !giyu.fallen) giyu.mastery.xp += 10;
        if (shinobu.summonedThisRun && !shinobu.fallen) shinobu.mastery.xp += 10;
        if (rengoku.summonedThisRun && !rengoku.fallen) rengoku.mastery.xp += 10;
        if (gyomei.summonedThisRun && !gyomei.fallen) gyomei.mastery.xp += 10;
        if (tengen.summonedThisRun && !tengen.fallen) tengen.mastery.xp += 10;
        EndHashiraEncounter();
        giyu.mastery.Save();
        shinobu.mastery.Save();
        rengoku.mastery.Save();
        gyomei.mastery.Save();
        tengen.mastery.Save();
        // the progenitor falls - his demons crumble with him
        for (auto& e : enemies) {
            fx.DeathBurst(e.pos, C(160, 40, 60), 1.0f);
            e.alive = false;
        }
        state = GState::Victory;
    }
}

void Game::UpdateStyleSelect() {
    if (IsKeyPressed(KEY_ESCAPE)) { state = GState::Title; return; }
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S))
        styleSelCursor = (styleSelCursor + 1) % STYLE_COUNT;
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W))
        styleSelCursor = (styleSelCursor + STYLE_COUNT - 1) % STYLE_COUNT;
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_J)) {
        if (unlocks.IsUnlocked(styleSelCursor)) {
            selectedStyle = styleSelCursor;     // remembered for the run
            PlaySfx(SFX_UPGRADE, 0.9f);
            StartRun();
        } else {
            PlaySfx(SFX_PICKUP, 0.35f, 0.5f);   // locked: rejection blip
        }
    }
}

void Game::CycleEquippedStyle(int dir) {
    // step to the next unlocked style in the given direction (dev convenience)
    int s = player.equipped;
    for (int i = 0; i < STYLE_COUNT; i++) {
        s = (s + dir + STYLE_COUNT) % STYLE_COUNT;
        if (unlocks.IsUnlocked(s)) break;
    }
    player.equipped = s;
    selectedStyle = s;        // remembered for the next run too
    player.cd[s] = 0;         // ready to use immediately
    PlaySfx(SFX_UPGRADE, 0.7f);
}

void Game::UpdateSettings() {
    // rows in display order: volume, [dev] change style, back
    int order[3], n = 0;
    order[n++] = 0;                        // master volume
    if (devUnlockAll) order[n++] = 1;      // change breathing style (dev only)
    order[n++] = 2;                        // back
    if (settingsSel >= n) settingsSel = n - 1;

    if (IsKeyPressed(KEY_ESCAPE)) { state = settingsFrom; return; }
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S))
        settingsSel = (settingsSel + 1) % n;
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W))
        settingsSel = (settingsSel + n - 1) % n;

    int row = order[settingsSel];
    if (row == 0) {                        // master volume
        float v = AudioGetMasterVolume();
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) {
            AudioSetMasterVolume(v + 0.05f); PlaySfx(SFX_PICKUP, 0.4f, 1.2f);
        }
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) {
            AudioSetMasterVolume(v - 0.05f); PlaySfx(SFX_PICKUP, 0.4f, 0.9f);
        }
    } else if (row == 1) {                 // change breathing style
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) CycleEquippedStyle(+1);
        if (IsKeyPressed(KEY_LEFT)  || IsKeyPressed(KEY_A)) CycleEquippedStyle(-1);
    } else {                               // back
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_J))
            state = settingsFrom;
    }
}

void Game::UpdateUpgradeMenu() {
    if (IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P)) {
        state = GState::Playing;
        return;
    }
    // upgrades only apply to the equipped style; row navigation is disabled
    selRow = player.equipped;
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D))
        selCol = (selCol + 1) % 4;
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A))
        selCol = (selCol + 3) % 4;
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_J)) {
        if (prog.Buy(selRow, selCol)) PlaySfx(SFX_UPGRADE, 0.9f);
        else PlaySfx(SFX_PICKUP, 0.35f, 0.5f);
    }
}

void Game::Update(float rawDt) {
    float dt = fminf(rawDt, 1.0f / 30.0f);
    fx.Update(dt);                       // real dt: shake/particles run in hitstop
    float gdt = dt * fx.TimeScale();     // gameplay dt
    player.invincible = devInvincible;

    switch (state) {
        case GState::Title:
            if (IsKeyPressed(KEY_ENTER)) {
                // open the Breathing Style menu; start on a style you can equip
                styleSelCursor = unlocks.IsUnlocked(selectedStyle)
                                 ? selectedStyle : unlocks.FirstUnlocked();
                state = GState::StyleSelect;
            }
            if (IsKeyPressed(KEY_S)) {          // settings: keybinds & volume
                settingsFrom = GState::Title;
                settingsSel = 0;
                state = GState::Settings;
            }
            if (IsKeyPressed(KEY_ESCAPE)) quit = true;
            break;

        case GState::StyleSelect:
            UpdateStyleSelect();
            break;

        case GState::Playing:
            if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE)) {
                pauseSel = 0;               // default highlight on Resume
                state = GState::Paused;
                return;
            }
            if (IsKeyPressed(KEY_TAB)) {
                selRow = player.equipped;   // upgrades apply to the equipped style
                state = GState::Upgrade;
                return;
            }
            if (IsKeyPressed(KEY_F8))  {
                devInvincible = !devInvincible;
                player.invincible = devInvincible;
                fx.Text({ player.pos.x, player.pos.y - 112 },
                        devInvincible ? C(255, 230, 120) : C(180, 170, 150), 0.95f,
                        devInvincible ? "DEV MODE: INVINCIBLE" : "DEV MODE OFF");
                PlaySfx(SFX_PICKUP, 0.45f, devInvincible ? 1.35f : 0.75f);
            }
            if (IsKeyPressed(KEY_G)) TrySummonHashira(0);
            if (IsKeyPressed(KEY_B)) TrySummonHashira(1);
            if (IsKeyPressed(KEY_R)) TrySummonHashira(2);
            if (IsKeyPressed(KEY_Y)) TrySummonHashira(3);
            if (IsKeyPressed(KEY_T)) TrySummonHashira(4);
            UpdatePlaying(gdt);
            break;

        case GState::Upgrade:
            UpdateUpgradeMenu();
            break;

        case GState::BossIntro: {
            introT -= dt;
            UpdateHashiraWithdrawals(gdt);
            if (fmodf(introT, 0.4f) < 0.05f) fx.AddShake(0.12f);
            fx.Ember({ frnd(0, (float)cfg::SCREEN_W), cfg::GROUND_Y - frnd(0, 100) });
            if (introT <= 0) {
                float side = player.pos.x < cfg::SCREEN_W * 0.5f
                             ? cfg::SCREEN_W * 0.78f : cfg::SCREEN_W * 0.22f;
                Vector2 spawn = { side, cfg::GROUND_Y - 60 };
                switch (introTarget) {
                    case 0:  akaza.Activate(spawn); break;
                    case 1:  douma.Activate(spawn); break;
                    case 2:  kokushibo.Activate(spawn); break;
                    default:
                        boss.Activate(spawn);
                        StartMuzanSurvival();
                        break;
                }
                state = GState::Playing;
            }
            break;
        }
        case GState::Paused:
            if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE)) {
                state = GState::Playing;
                break;
            }
            if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S))
                pauseSel = (pauseSel + 1) % 3;
            if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W))
                pauseSel = (pauseSel + 2) % 3;
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_J)) {
                if (pauseSel == 0) state = GState::Playing;
                else if (pauseSel == 1) {              // open settings
                    settingsFrom = GState::Paused;
                    settingsSel = 0;
                    state = GState::Settings;
                } else ReturnToTitle();                // quit to the main menu
            }
            break;

        case GState::Settings:
            UpdateSettings();
            break;

        case GState::Victory:
            UpdateHashiraWithdrawals(gdt);
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_R)) StartRun();
            if (IsKeyPressed(KEY_ESCAPE)) quit = true;
            break;
        case GState::GameOver:
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_R)) StartRun();
            if (IsKeyPressed(KEY_ESCAPE)) quit = true;
            break;
    }
}

// --- drawing ------------------------------------------------------

void Game::DrawBackground() const {
    float gt = (float)GetTime();

    // each Upper Moon warps the night into his own arena
    int arena = 0;                                        // 0 plain night
    if (akaza.active && !akaza.Defeated()) arena = 1;     // cold indigo
    else if (douma.active && !douma.Defeated()) arena = 2;      // white frost
    else if (kokushibo.active && !kokushibo.Defeated()) arena = 3; // violet moonfield
    bool cold = arena == 1 || arena == 2;
    // Kokushibo's arena grows darker and more oppressive as he escalates
    float kokuI = (arena == 3)
                  ? (kokushibo.phase >= 3 ? 1.0f : kokushibo.phase >= 2 ? 0.6f : 0.3f)
                  : 0.0f;

    // deep night sky
    if (arena == 1)
        DrawRectangleGradientV(0, 0, cfg::SCREEN_W, cfg::SCREEN_H, C(6, 8, 22), C(18, 26, 50));
    else if (arena == 2)
        DrawRectangleGradientV(0, 0, cfg::SCREEN_W, cfg::SCREEN_H, C(10, 12, 26), C(28, 36, 62));
    else if (arena == 3)
        DrawRectangleGradientV(0, 0, cfg::SCREEN_W, cfg::SCREEN_H, C(7, 3, 15), C(26, 11, 40));
    else
        DrawRectangleGradientV(0, 0, cfg::SCREEN_W, cfg::SCREEN_H, C(5, 4, 14), C(26, 14, 32));

    // sparse dim stars
    for (int i = 0; i < 60; i++) {
        int x = (i * 97 + 31) % cfg::SCREEN_W;
        int y = (i * 57 + 13) % (int)(cfg::GROUND_Y - 260);
        float tw = 0.25f + 0.2f * sinf(gt * 2.0f + i * 1.7f);
        DrawRectangle(x, y, (i % 5 == 0) ? 2 : 1, (i % 5 == 0) ? 2 : 1, Fade(WHITE, tw));
    }

    // moon (blood-tinged for Muzan, ice-pale for the cold moons, violet for Kokushibo)
    Color moonC = boss.active ? C(225, 165, 150)
                : cold        ? C(216, 226, 246)
                : arena == 3  ? C(180, 120, 205) : C(216, 210, 190);
    DrawCircleGradient(1060, 120, 90, Fade(moonC, 0.22f), BLANK);
    DrawCircleV({ 1060, 120 }, 46, moonC);
    DrawCircleV({ 1046, 110 }, 7, Fade(C(180, 172, 158), 0.5f));
    DrawCircleV({ 1072, 132 }, 5, Fade(C(180, 172, 158), 0.4f));

    // mountain silhouettes
    DrawTriangle({ 220, cfg::GROUND_Y - 210 }, { -80, cfg::GROUND_Y }, { 520, cfg::GROUND_Y }, C(16, 12, 26));
    DrawTriangle({ 640, cfg::GROUND_Y - 150 }, { 380, cfg::GROUND_Y }, { 900, cfg::GROUND_Y }, C(12, 9, 20));
    DrawTriangle({ 1080, cfg::GROUND_Y - 240 }, { 760, cfg::GROUND_Y }, { 1400, cfg::GROUND_Y }, C(19, 14, 28));

    // distant bamboo stalks
    for (int i = 0; i < 10; i++) {
        int x = (i * 137 + 60) % cfg::SCREEN_W;
        int hgt = 90 + (i * 53) % 70;
        DrawRectangle(x, (int)cfg::GROUND_Y - hgt, 5, hgt, C(11, 19, 17));
        DrawRectangle(x - 6, (int)cfg::GROUND_Y - hgt + 12, 6, 3, C(11, 19, 17));
    }

    // ground
    DrawRectangle(0, (int)cfg::GROUND_Y, cfg::SCREEN_W, cfg::SCREEN_H - (int)cfg::GROUND_Y, C(18, 14, 22));
    DrawRectangle(0, (int)cfg::GROUND_Y, cfg::SCREEN_W, 4, C(48, 40, 54));
    for (int i = 0; i < 40; i++) {
        int x = (i * 61 + 17) % cfg::SCREEN_W;
        DrawLineEx({ (float)x, cfg::GROUND_Y }, { (float)x + 3, cfg::GROUND_Y - 6 }, 2, C(40, 46, 38));
    }

    // drifting fog banks
    for (int i = 0; i < 9; i++) {
        float speed = 7.0f + (i % 3) * 6.0f;
        float fw = 260.0f + (i * 83) % 200;
        float x = fmodf(i * 173.0f + gt * speed, cfg::SCREEN_W + fw * 2) - fw;
        float y = cfg::GROUND_Y - 22 - (i * 37) % 85;
        DrawEllipse((int)x, (int)y, fw, 24.0f + (i * 13) % 16, Fade(C(140, 140, 170), 0.05f));
    }
    // low ground mist
    DrawRectangleGradientV(0, (int)cfg::GROUND_Y - 60, cfg::SCREEN_W, 60,
                           Fade(C(130, 130, 160), 0.0f), Fade(C(130, 130, 160), 0.10f));

    // snowfall over the cold arenas (a blizzard for Douma)
    if (cold) {
        int flakes = arena == 2 ? 84 : 46;
        for (int i = 0; i < flakes; i++) {
            float sp = 34.0f + (i % 4) * 13.0f;
            float sy = fmodf(i * 53.7f + gt * sp, cfg::GROUND_Y + 10.0f);
            float sx = fmodf(i * 97.3f + sinf(gt * 0.8f + i) * 18.0f + 4096.0f,
                             (float)cfg::SCREEN_W);
            DrawRectangle((int)sx, (int)sy, 2, 2, Fade(WHITE, 0.55f));
        }
        DrawRectangle(0, 0, cfg::SCREEN_W, cfg::SCREEN_H,
                      Fade(C(60, 100, 170), arena == 2 ? 0.07f : 0.05f));
    }
    // violet haze under the six eyes — and the light draining as he escalates
    if (arena == 3) {
        DrawRectangle(0, 0, cfg::SCREEN_W, cfg::SCREEN_H,
                      Fade(C(120, 40, 160), 0.05f + 0.03f * kokuI + 0.02f * sinf(gt * 2.2f)));
        DrawRectangle(0, 0, cfg::SCREEN_W, cfg::SCREEN_H,
                      Fade(C(4, 0, 10), 0.10f + 0.16f * kokuI));
    }

    // red battle tint while the Demon King lives
    if (boss.active && !boss.Defeated())
        DrawRectangle(0, 0, cfg::SCREEN_W, cfg::SCREEN_H,
                      Fade(C(130, 0, 25), 0.07f + 0.02f * sinf(gt * 3.0f)));

    if (muzanSurvival || sunriseFinale) {
        float dawn = sunriseFinale ? 1.0f : Clampf((300.0f - muzanTimer) / 300.0f, 0, 1);
        float warm = dawn * dawn;
        DrawRectangleGradientV(0, 0, cfg::SCREEN_W, cfg::SCREEN_H,
                               Fade(C(255, 185, 105), 0.04f + 0.22f * warm),
                               Fade(C(255, 235, 185), 0.02f + 0.13f * warm));
        DrawCircleGradient(1030, 82, 54 + 38 * warm,
                           Fade(C(255, 230, 160), 0.15f + 0.55f * warm),
                           Fade(C(255, 190, 100), 0.0f));
    }
}

static void DrawVignette() {
    DrawRectangleGradientV(0, 0, cfg::SCREEN_W, 130, Fade(BLACK, 0.45f), Fade(BLACK, 0.0f));
    DrawRectangleGradientV(0, cfg::SCREEN_H - 150, cfg::SCREEN_W, 150,
                           Fade(BLACK, 0.0f), Fade(BLACK, 0.5f));
    DrawRectangleGradientH(0, 0, 170, cfg::SCREEN_H, Fade(BLACK, 0.4f), Fade(BLACK, 0.0f));
    DrawRectangleGradientH(cfg::SCREEN_W - 170, 0, 170, cfg::SCREEN_H,
                           Fade(BLACK, 0.0f), Fade(BLACK, 0.4f));
}

static void DrawAbilityIcon(int x, int y, const char* key, const char* name,
                            float cdT, float cdMax, Color col, int lv, bool mastered) {
    Rectangle box = { (float)x, (float)y, 46, 46 };
    DrawRectangleRounded(box, 0.25f, 4, C(22, 18, 26));
    DrawRectangleRounded({ box.x + 4, box.y + 4, 38, 38 }, 0.25f, 4, Fade(col, cdT > 0 ? 0.35f : 1.0f));
    if (cdT > 0) {
        float f = cdT / fmaxf(cdMax, 0.01f);
        DrawRectangleRounded({ box.x + 4, box.y + 4 + 38 * (1 - f), 38, 38 * f }, 0.25f, 4,
                             Fade(BLACK, 0.55f));
        char buf[8];
        snprintf(buf, sizeof(buf), "%.1f", cdT);
        int tw = MeasureText(buf, 16);
        DrawText(buf, x + 23 - tw / 2, y + 15, 16, WHITE);
    }
    // upgrade pips under the icon
    int totalLv = lv;   // 0..9 across three tracks
    for (int i = 0; i < 9; i++) {
        Color pc = i < totalLv ? Fade(col, 0.95f) : C(45, 40, 52);
        DrawRectangle(x + 2 + i * 5, y + 48, 3, 3, pc);
    }
    if (mastered)
        DrawText("*", x + 38, y - 2, 20, C(255, 215, 120));
    DrawText(key, x + 4, y + 54, 13, C(200, 200, 210));
    DrawText(name, x + 17, y + 54, 13, Fade(col, 0.9f));
}

void Game::DrawUI() const {
    // player HP
    DrawRectangleRounded({ 20, 18, 264, 26 }, 0.4f, 6, C(20, 16, 24));
    float hf = Clampf(player.hp / player.maxHp, 0, 1);
    Color hpc = hf > 0.5f ? C(90, 200, 110) : (hf > 0.25f ? C(230, 190, 70) : C(220, 60, 60));
    if (hf > 0)
        DrawRectangleRounded({ 24, 22, fmaxf(256 * hf, 6), 18 }, 0.4f, 6, hpc);
    DrawText(TextFormat("%d / %d", (int)player.hp, (int)player.maxHp), 30, 24, 14, C(10, 12, 14));

    // equipped breathing style (only the chosen style is available this run)
    {
        int es = player.equipped;
        const StyleUpgrades& u = prog.up[es];
        DrawAbilityIcon(20, 56, STYLE_INFO[es].key, STYLE_INFO[es].shortName,
                        player.cd[es], STYLE_CD_BASE[es] * prog.CdMult(es),
                        STYLE_INFO[es].col, u.power + u.flow + u.reach, u.mastery);
        DrawText(TextFormat("%s BREATHING", STYLE_INFO[es].name), 78, 62, 18,
                 Fade(STYLE_INFO[es].col, 0.95f));
    }

    // upgrade hint
    if (prog.points > 0) {
        float pulse = 0.7f + 0.3f * sinf((float)GetTime() * 6.0f);
        DrawText(TextFormat("TAB - UPGRADE  (%d pts)", prog.points), 20, 132, 18,
                 Fade(C(255, 215, 120), pulse));
    } else {
        DrawText("TAB - upgrades", 20, 132, 14, C(120, 115, 135));
    }
    if (boss.active && !boss.Defeated()) {
        DrawText("HASHIRA: ALL SURVIVORS DEPLOY", 20, 154, 14, C(240, 170, 100));
    } else {
        DrawText(TextFormat("HASHIRA: %d / %d slots", HashiraSlotsUsed(), HashiraLimit()),
                 20, 154, 14, C(190, 175, 145));
    }
    if (devInvincible) {
        DrawText("DEV MODE: INVINCIBLE", 20, 174, 14, C(255, 230, 120));
    }

    // Giyu, the Water Hashira (bottom-left)
    {
        int gx = 20, gy = cfg::SCREEN_H - 96;
        Color gcol = C(100, 175, 255);
        DrawRectangleRounded({ (float)gx, (float)gy, 46, 46 }, 0.25f, 4, C(22, 18, 26));
        if (giyu.fallen) {
            DrawRectangleRounded({ gx + 4.0f, gy + 4.0f, 38, 38 }, 0.25f, 4, C(48, 32, 40));
            DrawText("X", gx + 16, gy + 11, 26, C(190, 60, 70));
            DrawText("FALLEN", gx + 54, gy + 16, 14, C(190, 60, 70));
        } else if (giyu.state == GiyuState::Withdraw) {
            DrawRectangleRounded({ gx + 4.0f, gy + 4.0f, 38, 38 }, 0.25f, 4, Fade(gcol, 0.45f));
            DrawText("EXIT", gx + 54, gy + 16, 14, Fade(gcol, 0.85f));
        } else if (giyu.Active()) {
            DrawRectangleRounded({ gx + 4.0f, gy + 4.0f, 38, 38 }, 0.25f, 4, gcol);
            float f = Clampf(giyu.hp / giyu.maxHp, 0, 1);
            DrawRectangle(gx + 54, gy + 20, (int)(88 * f), 8, gcol);
            DrawRectangleLines(gx + 54, gy + 20, 88, 8, C(60, 70, 95));
        } else if (giyu.summonCd > 0) {
            float f = Clampf(giyu.summonCd / giyu.mastery.SummonCd(), 0, 1);
            DrawRectangleRounded({ gx + 4.0f, gy + 4.0f, 38, 38 }, 0.25f, 4, Fade(gcol, 0.3f));
            DrawRectangleRounded({ gx + 4.0f, gy + 4 + 38 * (1 - f), 38, 38 * f }, 0.25f, 4,
                                 Fade(BLACK, 0.55f));
            DrawText(TextFormat("%.0f", giyu.summonCd), gx + 14, gy + 15, 16, WHITE);
        } else {
            float pulse = 0.72f + 0.28f * sinf((float)GetTime() * 5.0f);
            DrawRectangleRounded({ gx + 4.0f, gy + 4.0f, 38, 38 }, 0.25f, 4, Fade(gcol, pulse));
            DrawText("READY", gx + 54, gy + 16, 14, Fade(gcol, pulse));
        }
        DrawText("G", gx + 4, gy + 52, 13, C(200, 200, 210));
        DrawText("GIYU", gx + 17, gy + 52, 13, Fade(gcol, 0.9f));
        int glv = giyu.mastery.Level();
        for (int i = 0; i < 5; i++)
            DrawRectangle(gx + 54 + i * 9, gy + 36, 7, 4,
                          i < glv ? C(255, 215, 120) : C(45, 40, 52));
    }

    // Shinobu, the Insect Hashira (bottom-left)
    {
        int sx = 176, sy = cfg::SCREEN_H - 96;
        Color scol = C(190, 150, 255);
        DrawRectangleRounded({ (float)sx, (float)sy, 46, 46 }, 0.25f, 4, C(22, 18, 26));
        if (shinobu.fallen) {
            DrawRectangleRounded({ sx + 4.0f, sy + 4.0f, 38, 38 }, 0.25f, 4, C(48, 32, 48));
            DrawText("X", sx + 16, sy + 11, 26, C(220, 70, 120));
            DrawText("FALLEN", sx + 54, sy + 16, 14, C(220, 70, 120));
        } else if (shinobu.state == ShinobuState::Withdraw) {
            DrawRectangleRounded({ sx + 4.0f, sy + 4.0f, 38, 38 }, 0.25f, 4, Fade(scol, 0.45f));
            DrawText("EXIT", sx + 54, sy + 16, 14, Fade(scol, 0.85f));
        } else if (shinobu.Active()) {
            DrawRectangleRounded({ sx + 4.0f, sy + 4.0f, 38, 38 }, 0.25f, 4, scol);
            float f = Clampf(shinobu.hp / shinobu.maxHp, 0, 1);
            DrawRectangle(sx + 54, sy + 20, (int)(88 * f), 8, scol);
            DrawRectangleLines(sx + 54, sy + 20, 88, 8, C(70, 55, 95));
        } else if (shinobu.summonCd > 0) {
            float f = Clampf(shinobu.summonCd / shinobu.mastery.SummonCd(), 0, 1);
            DrawRectangleRounded({ sx + 4.0f, sy + 4.0f, 38, 38 }, 0.25f, 4, Fade(scol, 0.3f));
            DrawRectangleRounded({ sx + 4.0f, sy + 4 + 38 * (1 - f), 38, 38 * f }, 0.25f, 4,
                                 Fade(BLACK, 0.55f));
            DrawText(TextFormat("%.0f", shinobu.summonCd), sx + 14, sy + 15, 16, WHITE);
        } else {
            float pulse = 0.72f + 0.28f * sinf((float)GetTime() * 5.4f);
            DrawRectangleRounded({ sx + 4.0f, sy + 4.0f, 38, 38 }, 0.25f, 4, Fade(scol, pulse));
            DrawText("READY", sx + 54, sy + 16, 14, Fade(scol, pulse));
        }
        DrawText("B", sx + 4, sy + 52, 13, C(200, 200, 210));
        DrawText("SHIN", sx + 17, sy + 52, 13, Fade(scol, 0.9f));
        int slv = shinobu.mastery.Level();
        for (int i = 0; i < 5; i++)
            DrawRectangle(sx + 54 + i * 9, sy + 36, 7, 4,
                          i < slv ? C(255, 215, 120) : C(45, 40, 52));
    }

    // Rengoku, the Flame Hashira (bottom-left)
    {
        int rx = 332, ry = cfg::SCREEN_H - 96;
        Color rcol = C(255, 150, 55);
        DrawRectangleRounded({ (float)rx, (float)ry, 46, 46 }, 0.25f, 4, C(22, 18, 26));
        if (rengoku.fallen) {
            DrawRectangleRounded({ rx + 4.0f, ry + 4.0f, 38, 38 }, 0.25f, 4, C(54, 30, 28));
            DrawText("X", rx + 16, ry + 11, 26, C(240, 90, 70));
            DrawText("FALLEN", rx + 54, ry + 16, 14, C(240, 90, 70));
        } else if (rengoku.state == RengokuState::Withdraw) {
            DrawRectangleRounded({ rx + 4.0f, ry + 4.0f, 38, 38 }, 0.25f, 4, Fade(rcol, 0.45f));
            DrawText("EXIT", rx + 54, ry + 16, 14, Fade(rcol, 0.85f));
        } else if (rengoku.Active()) {
            DrawRectangleRounded({ rx + 4.0f, ry + 4.0f, 38, 38 }, 0.25f, 4, rcol);
            float f = Clampf(rengoku.hp / rengoku.maxHp, 0, 1);
            DrawRectangle(rx + 54, ry + 20, (int)(88 * f), 8, rcol);
            DrawRectangleLines(rx + 54, ry + 20, 88, 8, C(85, 55, 38));
        } else if (rengoku.summonCd > 0) {
            float f = Clampf(rengoku.summonCd / rengoku.mastery.SummonCd(), 0, 1);
            DrawRectangleRounded({ rx + 4.0f, ry + 4.0f, 38, 38 }, 0.25f, 4, Fade(rcol, 0.3f));
            DrawRectangleRounded({ rx + 4.0f, ry + 4 + 38 * (1 - f), 38, 38 * f }, 0.25f, 4,
                                 Fade(BLACK, 0.55f));
            DrawText(TextFormat("%.0f", rengoku.summonCd), rx + 14, ry + 15, 16, WHITE);
        } else {
            float pulse = 0.72f + 0.28f * sinf((float)GetTime() * 5.8f);
            DrawRectangleRounded({ rx + 4.0f, ry + 4.0f, 38, 38 }, 0.25f, 4, Fade(rcol, pulse));
            DrawText("READY", rx + 54, ry + 16, 14, Fade(rcol, pulse));
        }
        DrawText("R", rx + 4, ry + 52, 13, C(200, 200, 210));
        DrawText("REN", rx + 17, ry + 52, 13, Fade(rcol, 0.9f));
        int rlv = rengoku.mastery.Level();
        for (int i = 0; i < 5; i++)
            DrawRectangle(rx + 54 + i * 9, ry + 36, 7, 4,
                          i < rlv ? C(255, 215, 120) : C(45, 40, 52));
    }

    // Gyomei, the Stone Hashira (bottom-left)
    {
        int yx = 488, yy = cfg::SCREEN_H - 96;
        Color ycol = C(188, 178, 158);
        DrawRectangleRounded({ (float)yx, (float)yy, 46, 46 }, 0.25f, 4, C(22, 18, 26));
        if (gyomei.fallen) {
            DrawRectangleRounded({ yx + 4.0f, yy + 4.0f, 38, 38 }, 0.25f, 4, C(44, 38, 34));
            DrawText("X", yx + 16, yy + 11, 26, C(215, 70, 65));
            DrawText("FALLEN", yx + 54, yy + 16, 14, C(215, 70, 65));
        } else if (gyomei.state == GyomeiState::Withdraw) {
            DrawRectangleRounded({ yx + 4.0f, yy + 4.0f, 38, 38 }, 0.25f, 4, Fade(ycol, 0.45f));
            DrawText("EXIT", yx + 54, yy + 16, 14, Fade(ycol, 0.85f));
        } else if (gyomei.Active()) {
            DrawRectangleRounded({ yx + 4.0f, yy + 4.0f, 38, 38 }, 0.25f, 4, ycol);
            float f = Clampf(gyomei.hp / gyomei.maxHp, 0, 1);
            DrawRectangle(yx + 54, yy + 20, (int)(88 * f), 8, ycol);
            DrawRectangleLines(yx + 54, yy + 20, 88, 8, C(70, 66, 58));
        } else if (gyomei.summonCd > 0) {
            float f = Clampf(gyomei.summonCd / gyomei.mastery.SummonCd(), 0, 1);
            DrawRectangleRounded({ yx + 4.0f, yy + 4.0f, 38, 38 }, 0.25f, 4, Fade(ycol, 0.3f));
            DrawRectangleRounded({ yx + 4.0f, yy + 4 + 38 * (1 - f), 38, 38 * f }, 0.25f, 4,
                                 Fade(BLACK, 0.55f));
            DrawText(TextFormat("%.0f", gyomei.summonCd), yx + 14, yy + 15, 16, WHITE);
        } else {
            float pulse = 0.72f + 0.28f * sinf((float)GetTime() * 6.2f);
            DrawRectangleRounded({ yx + 4.0f, yy + 4.0f, 38, 38 }, 0.25f, 4, Fade(ycol, pulse));
            DrawText("READY", yx + 54, yy + 16, 14, Fade(ycol, pulse));
        }
        DrawText("Y", yx + 4, yy + 52, 13, C(200, 200, 210));
        DrawText("GYO", yx + 17, yy + 52, 13, Fade(ycol, 0.9f));
        int ylv = gyomei.mastery.Level();
        for (int i = 0; i < 5; i++)
            DrawRectangle(yx + 54 + i * 9, yy + 36, 7, 4,
                          i < ylv ? C(255, 215, 120) : C(45, 40, 52));
    }

    // Tengen, the Sound Hashira (bottom-left)
    {
        int tx = 644, ty = cfg::SCREEN_H - 96;
        Color tcol = C(255, 212, 88);
        DrawRectangleRounded({ (float)tx, (float)ty, 46, 46 }, 0.25f, 4, C(22, 18, 26));
        if (tengen.fallen) {
            DrawRectangleRounded({ tx + 4.0f, ty + 4.0f, 38, 38 }, 0.25f, 4, C(56, 38, 30));
            DrawText("X", tx + 16, ty + 11, 26, C(230, 75, 70));
            DrawText("FALLEN", tx + 54, ty + 16, 14, C(230, 75, 70));
        } else if (tengen.state == TengenState::Withdraw) {
            DrawRectangleRounded({ tx + 4.0f, ty + 4.0f, 38, 38 }, 0.25f, 4, Fade(tcol, 0.45f));
            DrawText("EXIT", tx + 54, ty + 16, 14, Fade(tcol, 0.85f));
        } else if (tengen.Active()) {
            DrawRectangleRounded({ tx + 4.0f, ty + 4.0f, 38, 38 }, 0.25f, 4, tcol);
            float f = Clampf(tengen.hp / tengen.maxHp, 0, 1);
            DrawRectangle(tx + 54, ty + 20, (int)(88 * f), 8, tcol);
            DrawRectangleLines(tx + 54, ty + 20, 88, 8, C(92, 64, 36));
        } else if (tengen.summonCd > 0) {
            float f = Clampf(tengen.summonCd / tengen.mastery.SummonCd(), 0, 1);
            DrawRectangleRounded({ tx + 4.0f, ty + 4.0f, 38, 38 }, 0.25f, 4, Fade(tcol, 0.3f));
            DrawRectangleRounded({ tx + 4.0f, ty + 4 + 38 * (1 - f), 38, 38 * f }, 0.25f, 4,
                                 Fade(BLACK, 0.55f));
            DrawText(TextFormat("%.0f", tengen.summonCd), tx + 14, ty + 15, 16, WHITE);
        } else {
            float pulse = 0.72f + 0.28f * sinf((float)GetTime() * 6.6f);
            DrawRectangleRounded({ tx + 4.0f, ty + 4.0f, 38, 38 }, 0.25f, 4, Fade(tcol, pulse));
            DrawText("READY", tx + 54, ty + 16, 14, Fade(tcol, pulse));
        }
        DrawText("T", tx + 4, ty + 52, 13, C(200, 200, 210));
        DrawText("TGN", tx + 17, ty + 52, 13, Fade(tcol, 0.9f));
        int tlv = tengen.mastery.Level();
        for (int i = 0; i < 5; i++)
            DrawRectangle(tx + 54 + i * 9, ty + 36, 7, 4,
                          i < tlv ? C(255, 215, 120) : C(45, 40, 52));
    }

    // right side: stats
    DrawText(TextFormat("KILLS %d", kills), cfg::SCREEN_W - 130, 20, 20, C(220, 210, 220));
    int m = (int)elapsed / 60, s = (int)elapsed % 60;
    DrawText(TextFormat("%02d:%02d", m, s), cfg::SCREEN_W - 130, 46, 20, C(160, 150, 170));

    // wave indicator / boss bar
    if (douma.active && !douma.Defeated()) {
        float bw = 520;
        float f = Clampf(douma.hp / douma.maxHp, 0, 1);
        float x0 = cfg::SCREEN_W * 0.5f - bw * 0.5f;
        CText("DOUMA - UPPER MOON TWO", 14, 20, C(170, 225, 250));
        DrawRectangleRounded({ x0, 40, bw, 18 }, 0.5f, 6, C(14, 20, 30));
        if (f > 0)
            DrawRectangleRounded({ x0 + 3, 43, fmaxf((bw - 6) * f, 5), 12 }, 0.5f, 6, C(120, 200, 235));
        DrawRectangle((int)(x0 + bw * 0.40f), 40, 2, 18, C(40, 60, 80));
        if (douma.vulnerable)      CText("VULNERABLE - STRIKE NOW!", 64, 16, C(255, 220, 90));
        else if (douma.guardBroken > 0) CText("GUARD BROKEN", 64, 16, C(210, 200, 185));
    } else if (kokushibo.active && !kokushibo.Defeated()) {
        float bw = 520;
        float f = Clampf(kokushibo.hp / kokushibo.maxHp, 0, 1);
        float x0 = cfg::SCREEN_W * 0.5f - bw * 0.5f;
        CText("KOKUSHIBO  -  UPPER MOON ONE", 14, 20, C(205, 150, 242));
        DrawRectangleRounded({ x0, 40, bw, 18 }, 0.5f, 6, C(20, 14, 30));
        if (f > 0)
            DrawRectangleRounded({ x0 + 3, 43, fmaxf((bw - 6) * f, 5), 12 }, 0.5f, 6, C(168, 104, 226));
        // phase thresholds: the blade quickens at 66%, the transformation at 33%
        DrawRectangle((int)(x0 + bw * 0.66f), 40, 2, 18, C(70, 50, 95));
        DrawRectangle((int)(x0 + bw * 0.33f), 40, 2, 18, C(130, 40, 62));
        DrawText(TextFormat("PHASE %d", kokushibo.phase), (int)(x0 + bw - 66), 42, 13, C(212, 150, 242));
        if (kokushibo.vulnerable)  CText("VULNERABLE - STRIKE NOW!", 64, 16, C(255, 220, 90));
        else if (kokushibo.guardBroken > 0) CText("GUARD BROKEN", 64, 16, C(210, 200, 185));
    } else if (akaza.active && !akaza.Defeated()) {
        float bw = 520;
        float f = Clampf(akaza.hp / akaza.maxHp, 0, 1);
        float x0 = cfg::SCREEN_W * 0.5f - bw * 0.5f;
        CText("AKAZA - UPPER MOON THREE", 14, 20, C(150, 190, 255));
        DrawRectangleRounded({ x0, 40, bw, 18 }, 0.5f, 6, C(14, 18, 30));
        if (f > 0)
            DrawRectangleRounded({ x0 + 3, 43, fmaxf((bw - 6) * f, 5), 12 }, 0.5f, 6, C(90, 140, 235));
        DrawRectangle((int)(x0 + bw * 0.40f), 40, 2, 18, C(40, 50, 80));
        if (akaza.vulnerable)
            CText("VULNERABLE - STRIKE NOW!", 64, 16, C(255, 220, 90));
        else if (akaza.guardBroken > 0)
            CText("GUARD BROKEN", 64, 16, C(210, 200, 185));
    } else if (boss.active && !boss.Defeated()) {
        float bw = 560;
        float f = Clampf(boss.hp / boss.maxHp, 0, 1);
        float x0 = cfg::SCREEN_W * 0.5f - bw * 0.5f;
        int remain = (int)ceilf(fmaxf(muzanTimer, 0));
        int mm = remain / 60, ss = remain % 60;
        Color timerC = muzanTimer <= 30.0f ? C(255, 230, 150) : C(255, 190, 120);
        CText("SURVIVE UNTIL SUNRISE", 10, 20, timerC);
        CText(TextFormat("%d:%02d", mm, ss), 34, 42, timerC);
        DrawRectangleRounded({ x0, 82, bw, 14 }, 0.5f, 6, C(24, 14, 20));
        if (f > 0)
            DrawRectangleRounded({ x0 + 3, 85, fmaxf((bw - 6) * f, 5), 8 }, 0.5f, 6, C(190, 25, 45));
        DrawText("MUZAN PRESSURE", (int)x0, 100, 13, C(220, 150, 150));
        DrawText(TextFormat("PHASE %d", boss.phase), (int)(x0 + bw - 70), 100, 13, C(255, 130, 140));
        if (boss.vulnerable)
            CText("OPENING - KEEP PRESSURE!", 118, 16, C(255, 220, 90));
        else if (boss.guardBroken > 0)
            CText("REGENERATION SUPPRESSED", 118, 16, C(210, 200, 185));
    } else if (state == GState::Playing || state == GState::Paused || state == GState::Upgrade) {
        CText(TextFormat("WAVE %d / %d", wave, cfg::WAVE_KOKU), 16, 22, C(220, 210, 225));
        int remaining = toSpawn + (int)enemies.size();
        if (remaining > 0)
            CText(TextFormat("demons left: %d", remaining), 42, 15, C(150, 140, 160));
    }

    // a fallen Upper Moon's epitaph
    if (moonFallT > 0 && state == GState::Playing) {
        float a = Clampf(moonFallT / 1.0f, 0, 1);
        CText(moonFallText, 150, 42, Fade(C(255, 215, 120), a));
        CText("the horde presses on...", 205, 17, Fade(C(210, 200, 210), a));
    }
}

void Game::DrawStyleSelect() const {
    DrawRectangle(0, 0, cfg::SCREEN_W, cfg::SCREEN_H, Fade(BLACK, 0.82f));
    CText("CHOOSE  YOUR  BREATHING  STYLE", 40, 40, C(235, 225, 235));
    CText("You carry one Breathing Style into the night.", 92, 18, C(190, 185, 200));

    const int y0 = 150, rh = 66;
    const float cx = 220, cw = 840;

    for (int i = 0; i < STYLE_COUNT; i++) {
        int y = y0 + i * rh;
        const StyleInfo& si = STYLE_INFO[i];
        bool unlocked = unlocks.IsUnlocked(i);
        bool sel = (styleSelCursor == i);

        Rectangle card = { cx, (float)y, cw, (float)(rh - 12) };
        DrawRectangleRounded(card, 0.16f, 6, sel ? C(46, 40, 56) : C(26, 22, 32));

        // color chip (drained to grey while locked)
        DrawRectangleRounded({ card.x + 14, card.y + 10, 12, card.height - 20 }, 0.4f, 4,
                             unlocked ? si.col : C(70, 66, 78));

        Color nameC = unlocked ? C(232, 226, 236) : C(120, 116, 130);
        DrawText(si.name, (int)card.x + 42, y + 8, 26, nameC);
        DrawText(TextFormat("[%s]", si.key), (int)card.x + 42, y + 36, 14,
                 Fade(unlocked ? si.col : C(120, 116, 130), 0.85f));
        DrawText(STYLE_DESC[i], (int)card.x + 168, y + 20, 16,
                 unlocked ? C(202, 198, 212) : C(112, 108, 122));

        // right-aligned status tag
        const char* tag = !unlocked ? "LOCKED" : (sel ? "ENTER TO EQUIP" : "READY");
        Color tagC = !unlocked ? C(196, 96, 104)
                   : sel        ? C(255, 215, 120) : Fade(si.col, 0.9f);
        int tw = MeasureText(tag, 18);
        DrawText(tag, (int)(card.x + card.width - tw - 22), y + 16, 18, tagC);

        if (sel)
            DrawRectangleLinesEx(card, 2, unlocked ? C(255, 215, 120) : C(196, 96, 104));
    }

    int fy = y0 + STYLE_COUNT * rh + 16;
    CText("UP / DOWN - browse       ENTER - equip & begin       ESC - back", fy, 18,
          C(210, 205, 220));
    if (devUnlockAll)
        CText("DEV: --unlock-all active - every style unlocked", fy + 28, 15,
              C(255, 215, 120));
    else
        CText(TextFormat("styles unlocked: %d / %d", unlocks.Count(), STYLE_COUNT),
              fy + 28, 15, C(150, 146, 162));
}

void Game::DrawSettings() const {
    DrawRectangle(0, 0, cfg::SCREEN_W, cfg::SCREEN_H, Fade(BLACK, 0.86f));
    CText("SETTINGS", 44, 44, C(235, 225, 235));

    // ---- left column: adjustable options ----
    int order[3], n = 0;
    order[n++] = 0;                        // volume
    if (devUnlockAll) order[n++] = 1;      // change style (dev)
    order[n++] = 2;                        // back

    const int lx = 120, ry = 158, rh = 78, bw = 520;
    for (int i = 0; i < n; i++) {
        int row = order[i];
        bool sel = (settingsSel == i);
        int y = ry + i * rh;
        Rectangle box = { (float)lx, (float)y, (float)bw, 62 };
        DrawRectangleRounded(box, 0.16f, 6, sel ? C(46, 40, 56) : C(26, 22, 32));
        if (sel) DrawRectangleLinesEx(box, 2, C(255, 215, 120));

        if (row == 0) {
            DrawText("MASTER VOLUME", lx + 18, y + 8, 20, C(226, 221, 233));
            float v = AudioGetMasterVolume();
            int bx = lx + 18, by = y + 38, barW = 340, barH = 12;
            DrawRectangleRounded({ (float)bx, (float)by, (float)barW, (float)barH }, 0.5f, 4,
                                 C(50, 46, 60));
            DrawRectangleRounded({ (float)bx, (float)by, fmaxf(barW * v, 6.0f), (float)barH },
                                 0.5f, 4, C(120, 200, 235));
            DrawText(TextFormat("%d%%", (int)(v * 100 + 0.5f)), bx + barW + 14, y + 32, 20,
                     C(210, 230, 240));
            DrawText("< >", lx + bw - 52, y + 8, 18, sel ? C(255, 215, 120) : C(120, 116, 132));
        } else if (row == 1) {
            DrawText("BREATHING STYLE", lx + 18, y + 8, 20, C(226, 221, 233));
            const StyleInfo& si = STYLE_INFO[player.equipped];
            DrawText(si.name, lx + 18, y + 34, 20, si.col);
            DrawText("dev: swap styles mid-run", lx + 190, y + 37, 15, C(150, 146, 162));
            DrawText("< >", lx + bw - 52, y + 8, 18, sel ? C(255, 215, 120) : C(120, 116, 132));
        } else {
            DrawText("BACK", lx + 18, y + 17, 22, sel ? C(255, 225, 150) : C(210, 205, 218));
        }
    }

    // ---- right column: keybinds reference ----
    int kx = 720, ky = 158;
    DrawText("CONTROLS", kx, ky, 22, C(235, 225, 235));
    ky += 34;
    struct KB { const char* a; const char* b; };
    static const KB binds[] = {
        { "Move",              "A / D" },
        { "Jump",              "W / SPACE" },
        { "Crouch",            "SHIFT" },
        { "Attack combo",      "J" },
        { "Launcher / Plunge", "UP+J / DOWN+J" },
        { "Breathing Style",   "1" },
        { "Summon Hashira",    "G / B / R / Y / T" },
        { "Upgrades",          "TAB" },
        { "Pause menu",        "ESC / P" },
        { "Fullscreen",        "F11" },
    };
    for (const KB& kb : binds) {
        DrawText(kb.a, kx, ky, 16, C(190, 186, 202));
        int tw = MeasureText(kb.b, 16);
        DrawText(kb.b, 1176 - tw, ky, 16, C(226, 221, 233));
        ky += 23;
    }

    ky += 12;
    DrawText("Whichever Breathing Style you equip fires on 1 -", kx, ky, 15,
             C(160, 156, 172));
    DrawText("you only ever carry one into a run.", kx, ky + 20, 15, C(160, 156, 172));

    CText("UP / DOWN - select      LEFT / RIGHT - adjust      ESC - back", 666, 18,
          C(175, 170, 188));
}

void Game::DrawUpgradeMenu() const {
    DrawRectangle(0, 0, cfg::SCREEN_W, cfg::SCREEN_H, Fade(BLACK, 0.82f));
    CText("BREATHING  MASTERY", 40, 40, C(235, 225, 235));
    DrawText(TextFormat("POINTS  %d", prog.points), cfg::SCREEN_W - 230, 42, 28, C(255, 215, 120));
    CText("LEFT / RIGHT - choose upgrade      ENTER - purchase      TAB - return to battle",
          92, 16, C(170, 165, 185));

    // only the equipped style is upgradable — the menu shows it alone
    const int s = player.equipped;
    const StyleInfo& si = STYLE_INFO[s];
    CText(TextFormat("%s  BREATHING", si.name), 150, 34, si.col);
    CText(TextFormat("[ %s ]  -  the only style you carry this run", si.key),
          196, 15, Fade(si.col, 0.85f));

    const int cellW = 260, cellH = 96, gap = 20;
    const int total = 4 * cellW + 3 * gap;
    const int x0 = (cfg::SCREEN_W - total) / 2;
    const int cy = 258;
    for (int c = 0; c < 4; c++) {
        bool sel = (selCol == c);
        Rectangle cell = { (float)(x0 + c * (cellW + gap)), (float)cy, (float)cellW, (float)cellH };
        DrawRectangleRounded(cell, 0.16f, 6, sel ? C(46, 38, 56) : C(28, 24, 36));

        int lv = prog.TrackLevel(s, c);
        bool can = prog.CanBuy(s, c);
        Color labelC = can ? C(230, 225, 235) : C(130, 125, 145);

        if (c < 3) {
            const char* names[3] = { "POWER", "FLOW", "REACH" };
            DrawText(names[c], (int)cell.x + 20, cy + 16, 22, labelC);
            for (int p = 0; p < 3; p++) {
                Color pc = p < lv ? si.col : C(52, 46, 62);
                DrawRectangle((int)cell.x + 20 + p * 28, cy + 50, 22, 15, pc);
            }
            DrawText(lv >= 3 ? "MAX" : "1 pt", (int)cell.x + 20, cy + 72, 16,
                     lv >= 3 ? Fade(si.col, 0.9f) : labelC);
        } else {
            DrawText("MASTERY", (int)cell.x + 20, cy + 16, 22,
                     lv > 0 ? C(255, 215, 120) : labelC);
            if (lv > 0)
                DrawText("UNLOCKED", (int)cell.x + 20, cy + 54, 16, C(255, 215, 120));
            else
                DrawText("2 pts", (int)cell.x + 20, cy + 72, 16, labelC);
        }
        if (sel)
            DrawRectangleLinesEx(cell, 2, C(255, 215, 120));
    }

    // footer: what does the selected cell do?
    int fy = cy + cellH + 34;
    const char* desc = (selCol < 3) ? TRACK_DESC[selCol] : MASTERY_DESC[s];
    CText(desc, fy, 18, C(215, 210, 225));

    // Hashira masteries grow on their own, through battles fought together
    int nxt = giyu.mastery.NextThreshold();
    if (nxt < 0)
        CText(TextFormat("GIYU MASTERY  LV %d  -  %d xp  (MAX - an elite Hashira)",
              giyu.mastery.Level(), giyu.mastery.xp), fy + 40, 14, C(120, 190, 255));
    else
        CText(TextFormat("GIYU MASTERY  LV %d  -  %d xp  (next level at %d - fight beside him)",
              giyu.mastery.Level(), giyu.mastery.xp, nxt), fy + 40, 14, C(120, 190, 255));
    int snxt = shinobu.mastery.NextThreshold();
    if (snxt < 0)
        CText(TextFormat("SHINOBU MASTERY  LV %d  -  %d xp  (MAX - wisteria bloom)",
              shinobu.mastery.Level(), shinobu.mastery.xp), fy + 60, 14, C(190, 150, 255));
    else
        CText(TextFormat("SHINOBU MASTERY  LV %d  -  %d xp  (next level at %d - poison demons)",
              shinobu.mastery.Level(), shinobu.mastery.xp, snxt), fy + 60, 14, C(190, 150, 255));
    int rnxt = rengoku.mastery.NextThreshold();
    if (rnxt < 0)
        CText(TextFormat("RENGOKU MASTERY  LV %d  -  %d xp  (MAX - Ninth Form)",
              rengoku.mastery.Level(), rengoku.mastery.xp), fy + 80, 14, C(255, 150, 55));
    else
        CText(TextFormat("RENGOKU MASTERY  LV %d  -  %d xp  (next level at %d - burn bright)",
              rengoku.mastery.Level(), rengoku.mastery.xp, rnxt), fy + 80, 14, C(255, 150, 55));
    int ynxt = gyomei.mastery.NextThreshold();
    if (ynxt < 0)
        CText(TextFormat("GYOMEI MASTERY  LV %d  -  %d xp  (MAX - immovable fortress)",
              gyomei.mastery.Level(), gyomei.mastery.xp), fy + 100, 14, C(188, 178, 158));
    else
        CText(TextFormat("GYOMEI MASTERY  LV %d  -  %d xp  (next level at %d - shatter barrages)",
              gyomei.mastery.Level(), gyomei.mastery.xp, ynxt), fy + 100, 14, C(188, 178, 158));
    int tnxt = tengen.mastery.NextThreshold();
    if (tnxt < 0)
        CText(TextFormat("TENGEN MASTERY  LV %d  -  %d xp  (MAX - flashy finale)",
              tengen.mastery.Level(), tengen.mastery.xp), fy + 120, 14, C(255, 212, 88));
    else
        CText(TextFormat("TENGEN MASTERY  LV %d  -  %d xp  (next level at %d - keep pressure)",
              tengen.mastery.Level(), tengen.mastery.xp, tnxt), fy + 120, 14, C(255, 212, 88));
}

void Game::DrawOverlays() const {
    float gt = (float)GetTime();

    if (state == GState::Title) {
        DrawRectangle(0, 0, cfg::SCREEN_W, cfg::SCREEN_H, Fade(BLACK, 0.55f));
        CText("DEMON  SLAYER", 96, 74, C(235, 225, 235));
        CText("Night of the Demon King", 180, 26, C(180, 60, 70));
        CText("Survive the waves. Grow stronger. Hold Muzan until sunrise.", 234, 18, C(200, 195, 210));

        CText("A / D - move    W / SPACE - jump    SHIFT - crouch    J - combo    UP+J launcher    DOWN+J plunge", 300, 15, C(210, 220, 235));
        CText("WATER - flowing multi-hit dash        FIRE - explosive burst", 342, 15, C(120, 190, 255));
        CText("STONE - guard-crushing slam           LOVE - healing dance", 368, 15, C(255, 150, 205));
        CText("SERPENT - venomous weaving flurry     WIND - sweeping tornadoes", 394, 15, C(140, 220, 120));
        CText("MIST - vanish, blink, ambush from the fog   (your one style fires on 1)", 420, 15, C(185, 195, 215));
        CText("G - summon GIYU TOMIOKA, the Water Hashira. if he falls, he is gone for the run", 446, 15, C(120, 190, 255));
        CText("B - summon SHINOBU KOCHO, the Insect Hashira. poison, triage, and wisteria", 472, 15, C(190, 150, 255));
        CText("R - summon KYOJURO RENGOKU, the Flame Hashira. burst damage and openings", 498, 15, C(255, 150, 55));
        CText("Y - summon GYOMEI HIMEJIMA, the Stone Hashira. fortress defense and crushing power", 524, 15, C(188, 178, 158));
        CText("T - summon TENGEN UZUI, the Sound Hashira. flashy speed, chains, and explosions", 550, 15, C(255, 212, 88));
        CText("Clear waves to earn points - press TAB in battle to upgrade your styles", 578, 16, C(255, 215, 120));
        CText("S - settings (keybinds & volume)      P / ESC - pause      F8 - dev invincible      F11 - fullscreen", 606, 14, C(150, 150, 165));

        if (fmodf(gt, 1.2f) < 0.75f)
            CText("PRESS  ENTER  TO  CHOOSE  YOUR  BREATHING  STYLE", 648, 24, C(240, 210, 130));
    }
    else if (state == GState::StyleSelect) {
        DrawStyleSelect();
    }
    else if (state == GState::Playing && bannerT > 0 && !boss.active) {
        float a = Clampf(bannerT / 0.5f, 0, 1);
        CText(TextFormat("WAVE  %d", wave), 240, 60, Fade(C(235, 220, 235), a));
        if (wave > 1)
            CText(TextFormat("+%d upgrade points - TAB to spend", cfg::PTS_PER_WAVE), 315, 18,
                  Fade(C(255, 215, 120), a));
        if (wave == cfg::BOSS_WAVE || wave == cfg::WAVE_DOUMA || wave == cfg::WAVE_KOKU)
            CText("something stirs in the dark...", 348, 20, Fade(C(220, 80, 90), a));
    }
    else if (state == GState::Playing && sunriseFinale) {
        float a = Clampf(sunriseOutroT / 1.2f, 0, 1);
        DrawRectangle(0, 0, cfg::SCREEN_W, cfg::SCREEN_H, Fade(C(255, 210, 130), 0.16f));
        CText("DAWN  BREAKS", 205, 64, Fade(C(255, 235, 170), a));
        CText("Muzan claws for the shadows as sunlight burns him away",
              294, 20, Fade(C(255, 230, 205), a));
    }
    else if (state == GState::BossIntro) {
        if (introTarget == 0) {
            // the Upper Moon descends into a cold arena
            float p = 1.0f - introT / 2.8f;
            DrawRectangle(0, 0, cfg::SCREEN_W, cfg::SCREEN_H, Fade(C(6, 14, 34), 0.35f + 0.28f * p));
            int size = (int)(38 + 30 * p);
            CText("AN  UPPER  MOON  DESCENDS", 280, size, Fade(C(150, 190, 255), Clampf(p * 2, 0, 1)));
            CText("Akaza worships strength - his fists never stop moving",
                  400, 17, Fade(C(220, 228, 245), p));
            CText("Survive the barrage. Punish his recovery. The night is long.",
                  428, 17, Fade(C(220, 228, 245), p));
        } else if (introTarget == 1) {
            float p = 1.0f - introT / 3.0f;
            DrawRectangle(0, 0, cfg::SCREEN_W, cfg::SCREEN_H, Fade(C(14, 22, 40), 0.35f + 0.3f * p));
            int size = (int)(38 + 30 * p);
            CText("UPPER  MOON  TWO  -  DOUMA", 280, size, Fade(C(180, 230, 250), Clampf(p * 2, 0, 1)));
            CText("his ice blooms wherever he smiles - never stand still",
                  400, 17, Fade(C(225, 238, 250), p));
            CText("frost slows your steps: shatter the lotus before it opens",
                  428, 17, Fade(C(225, 238, 250), p));
        } else if (introTarget == 2) {
            float p = 1.0f - introT / 4.2f;
            // the arena is swallowed in near-black; only his six eyes remain
            DrawRectangle(0, 0, cfg::SCREEN_W, cfg::SCREEN_H, Fade(C(6, 1, 12), 0.5f + 0.45f * p));
            float eyeA = Clampf(p * 1.6f, 0, 1);
            float cx = cfg::SCREEN_W * 0.5f;
            for (int r = 0; r < 2; r++)
                for (int c = 0; c < 3; c++) {
                    Vector2 e = { cx + (c - 1) * 66.0f, 234.0f + r * 40.0f };
                    float tw = 0.6f + 0.4f * sinf(gt * 4.0f + r * 3 + c);
                    DrawCircleV(e, 16, Fade(C(255, 40, 40), 0.18f * eyeA * tw));
                    DrawCircleV(e, 7, Fade(C(255, 60, 60), eyeA));
                    DrawCircleV(e, 3, Fade(C(255, 210, 210), eyeA));
                }
            CText("UPPER  MOON  ONE", 348, (int)(34 + 24 * p), Fade(C(210, 160, 245), Clampf(p * 2, 0, 1)));
            CText("K O K U S H I B O", 402, 30, Fade(C(234, 202, 250), Clampf(p * 2, 0, 1)));
            CText("the strongest of the Twelve Kizuki. six eyes see everything.",
                  456, 17, Fade(C(224, 210, 240), p));
            CText("his moon-blade sweeps the whole field - CROUCH beneath the long slashes",
                  482, 17, Fade(C(224, 210, 240), p));
        } else {
            float p = 1.0f - introT / 4.6f;
            DrawRectangle(0, 0, cfg::SCREEN_W, cfg::SCREEN_H, Fade(C(40, 0, 10), 0.35f + 0.25f * p));
            if (introT > 2.6f) {
                CText("THE  LAST  UPPER  MOON  FALLS", 300, 46, C(255, 215, 120));
                CText("...and the night holds its breath", 370, 20, C(220, 190, 190));
            } else {
                float q = 1.0f - introT / 2.6f;
                int size = (int)(40 + 34 * q);
                CText("THE  DEMON  KING  APPROACHES", 280, size, Fade(C(230, 40, 60), Clampf(q * 2, 0, 1)));
                CText("Muzan cannot be slain by your blade - survive five minutes until sunrise",
                      400, 17, Fade(C(230, 200, 200), q));
                CText("Pressure slows his regeneration. Hashira buy seconds. Do not stand still.",
                      428, 17, Fade(C(230, 200, 200), q));
            }
        }
    }
    else if (state == GState::Paused) {
        DrawRectangle(0, 0, cfg::SCREEN_W, cfg::SCREEN_H, Fade(BLACK, 0.72f));
        CText("PAUSED", 190, 56, C(235, 225, 235));

        const char* opts[3] = { "RESUME", "SETTINGS", "QUIT TO MAIN MENU" };
        for (int i = 0; i < 3; i++) {
            bool sel = (pauseSel == i);
            float bw = 460, bh = 58;
            Rectangle b = { cfg::SCREEN_W * 0.5f - bw * 0.5f, 300.0f + i * 78.0f, bw, bh };
            DrawRectangleRounded(b, 0.28f, 6, sel ? C(48, 40, 58) : C(26, 22, 32));
            if (sel) DrawRectangleLinesEx(b, 2, C(255, 215, 120));
            int tw = MeasureText(opts[i], 26);
            DrawText(opts[i], (int)(cfg::SCREEN_W * 0.5f - tw / 2), (int)b.y + 16, 26,
                     sel ? C(255, 225, 150) : C(210, 205, 218));
        }
        CText("UP / DOWN - choose      ENTER - select      ESC / P - resume", 566, 18,
              C(170, 165, 185));
    }
    else if (state == GState::Settings) {
        DrawSettings();
    }
    else if (state == GState::Upgrade) {
        DrawUpgradeMenu();
    }
    else if (state == GState::Victory) {
        DrawRectangle(0, 0, cfg::SCREEN_W, cfg::SCREEN_H, Fade(C(20, 8, 4), 0.6f));
        CText("SUNRISE", 210, 80, C(255, 220, 120));
        CText("Muzan burns beneath the morning sun. The night is over.", 310, 22, C(235, 220, 210));
        int m = (int)victoryTime / 60, s = (int)victoryTime % 60;
        CText(TextFormat("time %02d:%02d      demons slain %d", m, s, kills), 370, 20, C(200, 195, 205));
        if (fmodf(gt, 1.2f) < 0.75f)
            CText("ENTER - fight again", 470, 22, C(220, 200, 150));
    }
    else if (state == GState::GameOver) {
        DrawRectangle(0, 0, cfg::SCREEN_W, cfg::SCREEN_H, Fade(C(30, 0, 5), 0.65f));
        CText("YOU  DIED", 230, 76, C(200, 30, 40));
        CText(TextFormat("fell on wave %d      demons slain %d", wave, kills), 340, 20, C(210, 190, 195));
        if (fmodf(gt, 1.2f) < 0.75f)
            CText("ENTER - rise again", 440, 22, C(220, 200, 150));
    }

    // Kokushibo's transformation declaration — a cinematic that outlasts the flash
    if (kokushibo.active && kokushibo.declareT > 0 && state == GState::Playing) {
        float d = kokushibo.declareT;
        float a = Clampf(d > 2.4f ? (2.8f - d) / 0.4f : d / 1.0f, 0, 1);   // in, hold, out
        DrawRectangle(0, 0, cfg::SCREEN_W, cfg::SCREEN_H, Fade(C(10, 0, 16), 0.4f * a));
        CText("I  WILL  NOT  DIE", 248, 66, Fade(C(232, 58, 82), a));
        CText("MOON BREATHING  -  FOURTEENTH FORM: CATASTROPHE, TENMAN CRESCENT MOONS",
              332, 18, Fade(C(214, 165, 248), a));
    }
}

void Game::Draw() {
    ClearBackground(C(6, 5, 14));

    Camera2D cam{};
    cam.zoom = 1.0f;
    cam.offset = fx.ShakeOffset();

    BeginMode2D(cam);
    DrawBackground();

    // pickups
    for (const auto& p : pickups) {
        float blink = p.life < 2.0f ? (fmodf(p.life * 6, 2.0f) < 1 ? 0.4f : 1.0f) : 1.0f;
        DrawCircleV(p.pos, 9, Fade(C(60, 200, 100), 0.35f * blink));
        DrawCircleV(p.pos, 5.5f, Fade(C(110, 235, 130), blink));
        DrawRectangle((int)p.pos.x - 1, (int)p.pos.y - 4, 2, 8, Fade(WHITE, blink));
        DrawRectangle((int)p.pos.x - 4, (int)p.pos.y - 1, 8, 2, Fade(WHITE, blink));
    }

    for (const auto& e : enemies) e.Draw();
    boss.Draw();
    akaza.Draw();
    douma.Draw();
    kokushibo.Draw();
    giyu.Draw();
    shinobu.Draw();
    rengoku.Draw();
    gyomei.Draw();
    tengen.Draw();
    bool inMenu = state == GState::Title || state == GState::StyleSelect ||
                  state == GState::Settings;
    if (!inMenu) player.Draw();
    fx.DrawWorld();
    fx.DrawTexts();
    EndMode2D();

    DrawVignette();
    fx.DrawScreen();        // dark flashes / crescent bursts, over the world
    if (!inMenu) DrawUI();
    DrawOverlays();
}

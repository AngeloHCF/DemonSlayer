#pragma once
// ---------------------------------------------------------------
// game.h — game loop: states, waves, spawning, combat resolution,
// progression (upgrade points + menu), UI
// ---------------------------------------------------------------
#include "raylib.h"
#include "player.h"
#include "enemy.h"
#include "boss.h"
#include "akaza.h"
#include "moons.h"
#include "combat.h"
#include "effects.h"
#include "styles.h"
#include "companion.h"
#include "shinobu.h"
#include "rengoku.h"
#include "gyomei.h"
#include "tengen.h"
#include "sanemi.h"
#include <vector>

enum class GState { Title, StyleSelect, Playing, Upgrade, BossIntro, Paused, Settings, Victory, GameOver };

struct Pickup {              // healing orb dropped by demons
    Vector2 pos{}, vel{};
    float life = 8.0f;
};

class Game {
public:
    void Init();
    void Update(float rawDt);
    void Draw();
    // jump: 0 waves (--demo), 1 Akaza, 2 Muzan, 3 Douma, 4 Kokushibo
    void DebugStart(int jump);
    void UnlockAllForTesting();     // --unlock-all: every Breathing Style selectable

    bool quit = false;       // set when the player asks to leave

private:
    void StartRun();
    void ReturnToTitle();          // abandon the run and go back to the main menu
    void StartWave(int n);
    void SpawnEnemy();
    void SpawnDemonAtEdge(int wave);
    void UpdatePlaying(float dt);
    void UpdateStyleSelect();
    void UpdateSettings();
    void CycleEquippedStyle(int dir);   // dev: switch equipped style (--unlock-all)
    void UpdateUpgradeMenu();
    void ResolveCombat();
    void SeparateEnemies(float dt);
    int  HashiraLimit() const;
    int  ActiveHashiraCount() const;
    int  HashiraSlotsUsed() const;
    bool TrySummonHashira(int which);
    void AutoSummonMuzanHashira();
    void EndHashiraEncounter();
    void UpdateHashiraWithdrawals(float dt);
    void StartMuzanSurvival();
    void BeginSunriseFinale();

    void DrawBackground() const;
    void DrawUI() const;
    void DrawStyleSelect() const;
    void DrawSettings() const;
    void DrawUpgradeMenu() const;
    void DrawOverlays() const;

    GState state = GState::Title;
    Player player;
    std::vector<Enemy> enemies;
    Boss boss;
    Akaza akaza;
    UpperMoon douma{ MOON_DOUMA };
    UpperMoon kokushibo{ MOON_KOKU };
    Effects fx;
    CombatSystem combat;
    std::vector<Pickup> pickups;
    Progression prog;
    StyleUnlocks unlocks;              // which styles may be equipped (persists across runs)
    int   selectedStyle = STYLE_WATER; // the style carried into the next run
    int   styleSelCursor = 0;          // selection-menu cursor
    bool  devUnlockAll = false;        // --unlock-all: max every style and form
    Giyu giyu;
    Shinobu shinobu;
    Rengoku rengoku;
    Gyomei gyomei;
    Tengen tengen;
    Sanemi sanemi;
    bool  giyuCommitted = false, shinobuCommitted = false, rengokuCommitted = false,
          gyomeiCommitted = false, tengenCommitted = false, sanemiCommitted = false;

    int   selRow = 0, selCol = 0;   // upgrade menu cursor
    int   pauseSel = 0;             // escape-menu cursor (0 resume, 1 settings, 2 quit)
    int   settingsSel = 0;          // settings-menu cursor
    GState settingsFrom = GState::Title;  // where to return when leaving settings
    // the night's gauntlet: waves -> Akaza -> waves -> Douma -> waves
    // -> Kokushibo -> Muzan. introTarget routes the next BossIntro.
    int   introTarget = 0;          // 0 akaza, 1 douma, 2 kokushibo, 3 muzan
    bool  akazaHandled = false, doumaHandled = false, kokuHandled = false;
    float moonFallT = 0;            // golden "upper moon falls" banner
    const char* moonFallText = "";
    UpperMoon* ActiveMoon();        // the living Upper Moon (Douma/Koku) or null
    int   wave = 0;
    int   kills = 0;
    float elapsed = 0;          // run time
    float spawnTimer = 0;
    int   toSpawn = 0;
    float bannerT = 0;          // wave banner countdown
    float introT = 0;           // boss intro countdown
    float deathT = 0;           // delay before game-over screen
    bool  bossDefeatHandled = false;
    float victoryTime = 0;
    bool  devInvincible = false;
    float muzanTimer = 300.0f;
    bool  muzanSurvival = false;
    bool  sunriseFinale = false;
    float sunriseOutroT = 0;
};

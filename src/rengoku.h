#pragma once
// ---------------------------------------------------------------
// rengoku.h - Kyojuro Rengoku, the Flame Hashira
//
// Summon with R. Rengoku is a committed burst ally: high
// knockback, explosive Flame Breathing forms, and strong opening
// pressure, but he still cannot finish the great demons for you.
// ---------------------------------------------------------------
#include "raylib.h"
#include "combat.h"
#include <vector>

class Effects;
class Player;
class Boss;
class Akaza;
class UpperMoon;
class Enemy;

struct RengokuMastery {
    int xp = 0, summons = 0, kills = 0;

    int   Level() const;
    int   NextThreshold() const;
    float MoveSpeed() const   { return 380.0f + 14.0f * Level(); }
    float SummonCd() const    { return 48.0f - 4.0f * Level(); }    // 48 -> 28
    float MaxHp() const       { return 185.0f + 24.0f * Level(); }
    float Duration() const    { return 19.0f + 2.0f * Level(); }
    float DodgeChance() const { return 0.24f + 0.05f * Level(); }
    float DmgTaken() const    { return 0.96f - 0.04f * Level(); }
    float DmgMult() const     { return 1.05f + 0.12f * Level(); }
    float Cadence() const     { return 1.25f - 0.07f * Level(); }
    float FlamingWallShield() const { return 20.0f + 12.0f * Level(); }
    bool  HasNinthForm() const { return Level() >= 5; }

    void Load();
    void Save() const;
};

enum class RengokuState {
    Inactive, Arrive, Follow,
    FormUnknowing,     // First Form: Unknowing Fire
    FormRisingSun,     // Second Form: Rising Scorching Sun
    FormBlazing,       // Fifth Form: Flame Tiger / Blazing Universe hit
    FormTiger,
    FlamingWall,       // ultimate guard: blocks projectiles in front
    NinthForm,         // Ninth Form: Rengoku
    Withdraw, Fallen
};

class Rengoku {
public:
    void ResetRun();
    bool CanSummon() const;
    void Summon(Vector2 playerPos, Effects& fx);
    void BeginWithdraw(Effects& fx);
    void Update(float dt, Player& player, std::vector<Enemy>& enemies,
                Boss& boss, Akaza& akaza, UpperMoon* moon,
                CombatSystem& cs, Effects& fx);
    void Draw() const;
    Rectangle Rect() const;
    void TakeDamage(float dmg, float kbx, HitKind kind, Effects& fx);
    bool Active() const {
        return state != RengokuState::Inactive && state != RengokuState::Withdraw &&
               state != RengokuState::Fallen;
    }

    RengokuMastery mastery;
    bool  fallen = false;
    bool  summonedThisRun = false;
    float summonCd = 0;
    float activeT = 0;
    float hp = 0, maxHp = 0;
    Vector2 pos{}, vel{};
    int   facing = 1;
    HitMemory hitMem;
    float w = 38, h = 62;
    RengokuState state = RengokuState::Inactive;

private:
    void PickAction(Player& player, std::vector<Enemy>& enemies,
                    Boss& boss, Akaza& akaza, UpperMoon* moon,
                    CombatSystem& cs, Effects& fx);
    float stateTimer = 0;
    float attackTimer = 0;
    RengokuState lastForm = RengokuState::Inactive;
    float blazingCd = 0, tigerCd = 0, ninthCd = 0;
    float tickT = 0;
    int   curId = -1;
    int   formHits = 0;
    float hitFlash = 0, iframes = 0;
    float staggerT = 0;
    float flameWallShield = 0, flameWallShieldMax = 0;
    bool  ultDangerLast = false;
    Vector2 targetPos{};
    bool  targetIsBoss = false;
    bool  onGround = false;
    int   exitDir = -1;
};

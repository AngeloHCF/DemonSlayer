#pragma once
// ---------------------------------------------------------------
// sanemi.h - Sanemi Shinazugawa, the Wind Hashira
//
// Summon with N. Sanemi is the most aggressive Hashira: relentless
// pursuit, rapid sword pressure, wide Wind Breathing crowd control,
// and an offensive tornado barrier that shreds incoming attacks.
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

struct SanemiMastery {
    int xp = 0, summons = 0, kills = 0;

    int   Level() const;
    int   NextThreshold() const;
    float MoveSpeed() const   { return 470.0f + 22.0f * Level(); }
    float SummonCd() const    { return 44.0f - 3.4f * Level(); }
    float MaxHp() const       { return 170.0f + 20.0f * Level(); }
    float Duration() const    { return 20.0f + 2.0f * Level(); }
    float DodgeChance() const { return 0.38f + 0.055f * Level(); }
    float DmgTaken() const    { return 0.94f - 0.035f * Level(); }
    float DmgMult() const     { return 1.04f + 0.12f * Level(); }
    float Cadence() const     { return 0.62f - 0.04f * Level(); }
    float BarrierShield() const { return 34.0f + 12.0f * Level(); }
    float BarrierCd() const   { return 18.0f - 1.0f * Level(); }
    bool  HasIdatenTyphoon() const { return Level() >= 5; }

    void Load();
    void Save() const;
};

enum class SanemiState {
    Inactive, Arrive, Follow,
    LightCombo,          // rapid close pressure
    DustWhirlwind,       // First Form: Dust Whirlwind Cutter
    GaleSlash,           // Seventh Form: Gale - Sudden Gusts
    RisingDustStorm,     // Fourth Form: Rising Dust Storm
    ColdMountainWind,    // aerial spinning cut
    IdatenTyphoon,       // mastery ultimate
    WindBarrier,         // offensive tornado defense
    Withdraw, Fallen
};

class Sanemi {
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
        return state != SanemiState::Inactive && state != SanemiState::Withdraw &&
               state != SanemiState::Fallen;
    }

    SanemiMastery mastery;
    bool  fallen = false;
    bool  summonedThisRun = false;
    float summonCd = 0;
    float activeT = 0;
    float hp = 0, maxHp = 0;
    Vector2 pos{}, vel{};
    int   facing = 1;
    HitMemory hitMem;
    float w = 38, h = 62;
    SanemiState state = SanemiState::Inactive;

private:
    void PickAction(Player& player, std::vector<Enemy>& enemies,
                    Boss& boss, Akaza& akaza, UpperMoon* moon,
                    CombatSystem& cs, Effects& fx);

    float stateTimer = 0;
    float attackTimer = 0;
    SanemiState lastForm = SanemiState::Inactive;
    float gustCd = 0, stormCd = 0, airCd = 0, typhoonCd = 0, barrierCd = 0;
    float tickT = 0;
    int   curId = -1;
    int   formHits = 0;
    float hitFlash = 0, iframes = 0, afterimageT = 0;
    float staggerT = 0;
    float barrierShield = 0, barrierShieldMax = 0;
    bool  ultDangerLast = false;
    Vector2 targetPos{};
    bool  targetIsBoss = false;
    bool  onGround = false;
    int   exitDir = -1;
};

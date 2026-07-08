#pragma once
// ---------------------------------------------------------------
// tengen.h - Tengen Uzui, the Sound Hashira
//
// Summon with T. Tengen is the flashy pressure Hashira: high
// speed, chain-linked dual cleavers, explosive Sound Breathing,
// relentless combo flow, and aggressive projectile deflection.
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

struct TengenMastery {
    int xp = 0, summons = 0, kills = 0;

    int   Level() const;
    int   NextThreshold() const;
    float MoveSpeed() const   { return 435.0f + 18.0f * Level(); }
    float SummonCd() const    { return 46.0f - 3.5f * Level(); }
    float MaxHp() const       { return 178.0f + 22.0f * Level(); }
    float Duration() const    { return 20.0f + 2.0f * Level(); }
    float DodgeChance() const { return 0.34f + 0.055f * Level(); }
    float DmgTaken() const    { return 0.94f - 0.04f * Level(); }
    float DmgMult() const     { return 1.00f + 0.12f * Level(); }
    float Cadence() const     { return 0.78f - 0.045f * Level(); }
    float DeflectShield() const { return 30.0f + 11.0f * Level(); }
    float DeflectCd() const   { return 21.0f - 1.1f * Level(); }

    void Load();
    void Save() const;
};

enum class TengenState {
    Inactive, Arrive, Follow,
    LightCombo,          // quick chained cleaver string
    ChainSweep,          // long-range chained blade sweep
    ExplosiveRush,       // dashing multi-hit pressure
    RisingBeat,          // aerial spinning cleaver attack
    Roar,                // heavy explosive sweep
    ScoreUltimate,       // Musical Score: explosive finale
    ExplosiveDeflection, // spinning chained blades destroy projectiles
    Withdraw, Fallen
};

class Tengen {
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
        return state != TengenState::Inactive && state != TengenState::Withdraw &&
               state != TengenState::Fallen;
    }

    TengenMastery mastery;
    bool  fallen = false;
    bool  summonedThisRun = false;
    float summonCd = 0;
    float activeT = 0;
    float hp = 0, maxHp = 0;
    Vector2 pos{}, vel{};
    int   facing = 1;
    HitMemory hitMem;
    float w = 40, h = 64;
    TengenState state = TengenState::Inactive;

private:
    void PickAction(Player& player, std::vector<Enemy>& enemies,
                    Boss& boss, Akaza& akaza, UpperMoon* moon,
                    CombatSystem& cs, Effects& fx);

    float stateTimer = 0;
    float attackTimer = 0;
    TengenState lastForm = TengenState::Inactive;
    float chainCd = 0, rushCd = 0, airCd = 0, roarCd = 0, scoreCd = 0, deflectCd = 0;
    float tickT = 0;
    int   curId = -1;
    int   formHits = 0;
    float hitFlash = 0, iframes = 0, dodgeT = 0, flashStepT = 0;
    float staggerT = 0;
    float deflectShield = 0, deflectShieldMax = 0;
    bool  ultDangerLast = false;
    Vector2 targetPos{};
    bool  targetIsBoss = false;
    bool  onGround = false;
    int   exitDir = -1;
};

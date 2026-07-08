#pragma once
// ---------------------------------------------------------------
// gyomei.h - Gyomei Himejima, the Stone Hashira
//
// Summon with Y. Gyomei is the immovable frontline Hashira:
// enormous health, crushing damage, deliberate chained axe/flail
// attacks, and Stone Guard to intercept boss projectile barrages.
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

struct GyomeiMastery {
    int xp = 0, summons = 0, kills = 0;

    int   Level() const;
    int   NextThreshold() const;
    float MoveSpeed() const   { return 250.0f + 10.0f * Level(); }
    float SummonCd() const    { return 58.0f - 4.0f * Level(); }
    float MaxHp() const       { return 270.0f + 38.0f * Level(); }
    float Duration() const    { return 22.0f + 1.5f * Level(); }
    float DodgeChance() const { return 0.12f + 0.025f * Level(); }
    float DmgTaken() const    { return 0.72f - 0.035f * Level(); }
    float DmgMult() const     { return 1.25f + 0.14f * Level(); }
    float Cadence() const     { return 1.72f - 0.055f * Level(); }
    float StoneGuardShield() const { return 54.0f + 18.0f * Level(); }
    float StoneGuardCd() const { return 24.0f - 1.3f * Level(); }

    void Load();
    void Save() const;
};

enum class GyomeiState {
    Inactive, Arrive, Follow,
    Combo,             // axe, flail, then a crushing chain slam
    Serpentinite,      // First Form: Serpentinite Bipolar
    UpperSmash,        // Second Form: Upper Smash
    RapidConquest,     // Fourth Form: Volcanic Rock, Rapid Conquest
    ArcsJustice,       // Fifth Form: Arcs of Justice
    StoneGuard,        // defensive stance: shatters incoming projectiles
    Withdraw, Fallen
};

class Gyomei {
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
        return state != GyomeiState::Inactive && state != GyomeiState::Withdraw &&
               state != GyomeiState::Fallen;
    }

    GyomeiMastery mastery;
    bool  fallen = false;
    bool  summonedThisRun = false;
    float summonCd = 0;
    float activeT = 0;
    float hp = 0, maxHp = 0;
    Vector2 pos{}, vel{};
    int   facing = 1;
    HitMemory hitMem;
    float w = 44, h = 72;
    GyomeiState state = GyomeiState::Inactive;

private:
    void PickAction(Player& player, std::vector<Enemy>& enemies,
                    Boss& boss, Akaza& akaza, UpperMoon* moon,
                    CombatSystem& cs, Effects& fx);
    Rectangle GuardRect(const Player& player) const;

    float stateTimer = 0;
    float attackTimer = 0;
    GyomeiState lastForm = GyomeiState::Inactive;
    float serpentCd = 0, upperCd = 0, conquestCd = 0, justiceCd = 0, stoneGuardCd = 0;
    float tickT = 0;
    int   curId = -1;
    int   formHits = 0;
    float hitFlash = 0, iframes = 0, dodgeT = 0;
    float staggerT = 0;
    float stoneGuardShield = 0, stoneGuardShieldMax = 0;
    bool  ultDangerLast = false;
    Vector2 targetPos{};
    bool  targetIsBoss = false;
    bool  onGround = false;
    int   exitDir = -1;
};

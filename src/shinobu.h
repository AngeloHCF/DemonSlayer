#pragma once
// ---------------------------------------------------------------
// shinobu.h - Shinobu Kocho, the Insect Hashira
//
// Summon with B. She is fragile compared with Giyu, but her speed,
// medical support, and wisteria poison make her excellent at thinning
// swarms and forcing short openings against Upper Moons.
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

struct ShinobuMastery {
    int xp = 0, summons = 0, kills = 0;

    int   Level() const;              // 0..5
    int   NextThreshold() const;      // xp needed for next level (-1 at max)
    float MoveSpeed() const   { return 430.0f + 20.0f * Level(); }
    float SummonCd() const    { return 38.0f - 3.0f * Level(); }    // 38 -> 23
    float MaxHp() const       { return 115.0f + 15.0f * Level(); }
    float Duration() const    { return 18.0f + 2.0f * Level(); }
    float DodgeChance() const { return 0.42f + 0.08f * Level(); }
    float DmgTaken() const    { return 1.05f - 0.04f * Level(); }
    float DmgMult() const     { return 0.85f + 0.08f * Level(); }
    float Cadence() const     { return 0.95f - 0.06f * Level(); }
    float HealAmount() const  { return 18.0f + 4.0f * Level(); }
    bool  HasWisteriaBloom() const { return Level() >= 5; }

    void Load();
    void Save() const;
};

enum class ShinobuState {
    Inactive, Arrive, Follow,
    FormCaprice,       // Dance of the Butterfly: Caprice - close rapid thrusts
    FormFlutter,       // Dance of the Bee Sting: True Flutter - piercing dash
    FormHexagon,       // Dance of the Dragonfly: Compound Eye Hexagon
    FormZigzag,        // Dance of the Centipede: Hundred-Legged Zigzag
    WisteriaBloom,     // max mastery: poison cloud + light triage
    Heal,
    Withdraw, Fallen
};

class Shinobu {
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
        return state != ShinobuState::Inactive && state != ShinobuState::Withdraw &&
               state != ShinobuState::Fallen;
    }

    ShinobuMastery mastery;
    bool  fallen = false;
    bool  summonedThisRun = false;
    float summonCd = 0;
    float activeT = 0;
    float hp = 0, maxHp = 0;
    Vector2 pos{}, vel{};
    int   facing = 1;
    HitMemory hitMem;
    float w = 30, h = 56;
    ShinobuState state = ShinobuState::Inactive;

private:
    void PickAction(Player& player, std::vector<Enemy>& enemies,
                    Boss& boss, Akaza& akaza, UpperMoon* moon,
                    CombatSystem& cs, Effects& fx);
    float stateTimer = 0;
    float attackTimer = 0;
    ShinobuState lastForm = ShinobuState::Inactive;
    float hexCd = 0, zigzagCd = 0, wisteriaCd = 0, healCd = 0;
    float tickT = 0;
    int   curId = -1;
    int   formHits = 0;
    float hitFlash = 0, iframes = 0;
    float staggerT = 0;
    Vector2 targetPos{};
    bool  targetIsBoss = false;
    bool  onGround = false;
    int   exitDir = -1;
};

#pragma once
// ---------------------------------------------------------------
// companion.h — Giyu Tomioka, the Water Hashira
//
// Summon with G. He fights beside you until the encounter ends, then
// withdraws. If his health reaches zero HE FALLS FOR THE RUN —
// no more summons until a new game. Mastery persists ACROSS runs
// (giyu_mastery.txt): using him well levels him from a wary ally
// into an elite Hashira with Dead Calm at maximum mastery.
//
// Balance: his blade shreds ordinary demons, but Muzan takes only
// a fraction of his damage — Giyu creates openings; you finish it.
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

struct GiyuMastery {
    int xp = 0, summons = 0, kills = 0;

    int   Level() const;              // 0..5
    int   NextThreshold() const;      // xp needed for next level (-1 at max)
    float MoveSpeed() const   { return 365.0f + 15.0f * Level(); }
    float SummonCd() const    { return 45.0f - 4.0f * Level(); }     // 45 -> 25
    float MaxHp() const       { return 160.0f + 25.0f * Level(); }
    float Duration() const    { return 20.0f + 2.0f * Level(); }
    float DodgeChance() const { return 0.30f + 0.07f * Level(); }    // improved AI
    float DmgMult() const     { return 1.0f + 0.10f * Level(); }
    float DmgTaken() const    { return 1.0f - 0.05f * Level(); }
    float Cadence() const     { return 1.15f - 0.07f * Level(); }    // time between forms
    float DeadCalmShield() const { return 18.0f + 10.0f * Level(); }
    bool  HasDeadCalm() const { return Level() >= 5; }               // Eleventh Form

    void Load();
    void Save() const;
};

enum class GiyuState {
    Inactive, Arrive, Follow,
    FormSlash,      // First Form: Water Surface Slash — gap-closing dash cut
    FormTide,       // Fourth Form: Striking Tide — rapid chained slashes
    FormWhirl,      // Sixth Form: Whirlpool — spinning AoE when surrounded
    FormWheel,      // Second Form: Water Wheel — leaping rolling arc (mastery 4)
    DeadCalm,       // Eleventh Form: Dead Calm — nullifies attacks (mastery 5)
    Withdraw, Fallen
};

class Giyu {
public:
    void ResetRun();                  // new game: he may be summoned again
    bool CanSummon() const;
    void Summon(Vector2 playerPos, Effects& fx);
    void BeginWithdraw(Effects& fx);
    // moon: the currently active Upper Moon (Douma/Kokushibo), or null
    void Update(float dt, Player& player, std::vector<Enemy>& enemies,
                Boss& boss, Akaza& akaza, UpperMoon* moon,
                CombatSystem& cs, Effects& fx);
    void Draw() const;
    Rectangle Rect() const;
    // he is powerful — but human. Muzan's blows are harder to dodge,
    // pierce even Dead Calm, and send him flying.
    void TakeDamage(float dmg, float kbx, HitKind kind, Effects& fx);
    bool Active() const {
        return state != GiyuState::Inactive && state != GiyuState::Withdraw &&
               state != GiyuState::Fallen;
    }

    GiyuMastery mastery;
    bool  fallen = false;             // permanent for this run
    bool  summonedThisRun = false;
    float summonCd = 0;
    float activeT = 0;
    float hp = 0, maxHp = 0;
    Vector2 pos{}, vel{};
    int   facing = 1;
    HitMemory hitMem;
    float w = 36, h = 60;
    GiyuState state = GiyuState::Inactive;

private:
    void PickAction(Player& player, std::vector<Enemy>& enemies,
                    Boss& boss, Akaza& akaza, UpperMoon* moon,
                    CombatSystem& cs, Effects& fx);
    float stateTimer = 0;
    float attackTimer = 0;            // cadence between forms
    GiyuState lastForm = GiyuState::Inactive;   // never the same cut twice
    float whirlCd = 0, wheelCd = 0, deadCalmCd = 0;
    float tickT = 0;
    int   curId = -1;
    int   tideHits = 0;
    float hitFlash = 0, iframes = 0;
    float staggerT = 0;               // knocked flying by the Demon King
    float deadCalmShield = 0, deadCalmShieldMax = 0;
    bool  ultDangerLast = false;
    Vector2 targetPos{};
    bool  targetIsBoss = false;
    bool  onGround = false;
    int   exitDir = -1;
};

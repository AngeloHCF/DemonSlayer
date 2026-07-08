#pragma once
// ---------------------------------------------------------------
// boss.h — Muzan, the Demon King
//
// Patterns: dash slash, teleport strike, claw shockwaves (area),
// demon summon, blood crescents (aimed), blade burst (radial blade
// manifestation). After most attacks he enters a Recover window
// where he is VULNERABLE: full damage, Fire Breathing deals 1.5x.
// Water Breathing slows him. Stone Breathing breaks his guard for
// a few seconds (full damage even while active).
// ---------------------------------------------------------------
#include "raylib.h"
#include "combat.h"
#include <vector>

class Effects;
class Player;
class Giyu;
class Shinobu;
class Rengoku;

enum class BState {
    Inactive, Intro, Stalk,
    TeleDash, Dash,
    TeleClaws, Claws,
    Summon,
    TeleCrescent,
    TeleVanish, TeleStrike,       // teleport behind the player, slash
    TeleBlades,                   // blades erupt from his body, radial burst
    WhipStorm,                    // multi-direction black blood whips
    Arena,                        // wide-area arena denial
    PhaseShift,                   // invulnerable transformation between phases
    Desperation,                  // violent blood-whip ultimate during the survival fight
    Recover,
    Dying, Dead
};

struct Crescent {
    Vector2 pos{}, vel{};
    float spin = 0;
    bool alive = true;
};

struct BossRing {          // expanding claw shockwave
    Vector2 center{};
    float r = 30;
    bool hitDone = false;
};

class Boss {
public:
    void Reset();
    void Activate(Vector2 p);
    // summonRequest: set to the number of demons the game should spawn this frame
    // ally: Giyu, whom Muzan will also hunt and strike (may be null/inactive)
    void Update(float dt, Player& player, Giyu* ally, Shinobu* shinobu,
                Rengoku* rengoku, CombatSystem& cs, Effects& fx, int& summonRequest);
    void Draw() const;
    Rectangle Rect() const;
    void TakeDamage(float dmg, float kbx, HitKind kind, Effects& fx);
    void BeginSunriseDeath(Effects& fx);

    // companion support: Giyu can pry open Muzan's defense (internal cooldown)
    bool ForceOpening(Effects& fx);
    // Dead Calm: erase blood crescents near a point; returns how many were cut
    int  NullifyCrescents(Vector2 c, float r);
    int  NullifyCrescentsInRect(Rectangle r);
    int  NullifyRings(Vector2 c, float r);
    int  NullifyRingsInRect(Rectangle r);
    int  CrescentsNear(Vector2 c, float r) const;

    bool Alive() const { return active && state != BState::Dead && state != BState::Dying; }
    bool Defeated() const { return active && state == BState::Dead; }

    bool  active = false;
    BState state = BState::Inactive;
    Vector2 pos{}, vel{};
    int   facing = -1;
    float w = 46, h = 74;
    float hp = 0, maxHp = 0;
    int   phase = 1;
    bool  vulnerable = false;
    float guardBroken = 0;        // Stone Breathing debuff timer
    float fightT = 0;             // seconds survived against Muzan
    HitMemory hitMem;

private:
    void ChooseAttack(const Player& player, const Giyu* ally, const Shinobu* shinobu,
                      const Rengoku* rengoku);
    void EnterRecover(float t);
    bool preyAlly = false;        // current attack aimed at Giyu instead of the player
    bool preyShinobu = false;     // current attack aimed at Shinobu instead of the player
    bool preyRengoku = false;     // current attack aimed at Rengoku instead of the player

    float stateTimer = 0;
    float decideTimer = 0;
    float tickT = 0;
    int   comboLeft = 0;
    int   dashesLeft = 0;
    int   dashAttackId = -1;
    float slowTimer = 0;
    float openingCd = 0;          // limits how often Giyu can force an opening
    float pressureLock = 0;       // recent damage suppresses regeneration
    float ultimateCd = 0;
    float poisonT = 0;            // serpent venom (cannot finish the king)
    float poisonTick = 0;
    float hitFlash = 0;
    float auraTimer = 0;
    float vanishDur = 0.32f;
    bool  despBlasted = false;    // desperation eruption fired
    bool  sunriseDeath = false;
    std::vector<Crescent> crescents;
    std::vector<BossRing> ringsAtk;
};

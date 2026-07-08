#pragma once
// ---------------------------------------------------------------
// akaza.h — Akaza, Upper Moon Three
//
// The martial-arts demon who bars the way to Muzan. Incredible
// footwork, rapid fist combos, air-splitting shockwaves and a
// crater-leaving leap. He worships strength: he hunts whichever
// opponent is stronger — and he will happily break Giyu alone.
//
// Power scale: Giyu < Akaza < Muzan.
// ---------------------------------------------------------------
#include "raylib.h"
#include "combat.h"
#include <vector>

class Effects;
class Player;
class Giyu;

enum class AkState {
    Inactive, Intro, Stalk,
    TeleCombo, Combo,          // Destructive Death: rapid fist barrage
    TeleDash, DashBlow,        // lunging elbow — heavy launcher
    TeleShock, Shockwave,      // ground wave + air-splitting fist orbs
    TeleLeap, Leap,            // crater slam from above
    Desperation,               // at 40%: blind omnidirectional barrage
    Recover,
    Dying, Dead
};

struct FistOrb {               // compressed-air fist projectile
    Vector2 pos{}, vel{};
    bool alive = true;
};

struct AkWave {                // expanding ground shockwave
    Vector2 center{};
    float r = 20;
    bool hitPlayer = false, hitAlly = false;
};

class Akaza {
public:
    void Reset();
    void Activate(Vector2 p);
    void Update(float dt, Player& player, Giyu* ally, CombatSystem& cs, Effects& fx);
    void Draw() const;
    Rectangle Rect() const;
    void TakeDamage(float dmg, float kbx, HitKind kind, Effects& fx);

    bool Alive() const { return active && state != AkState::Dead && state != AkState::Dying; }
    bool Defeated() const { return active && state == AkState::Dead; }

    // Giyu support hooks (mirrors Boss)
    bool ForceOpening(Effects& fx);
    int  NullifyOrbs(Vector2 c, float r);
    int  OrbsNear(Vector2 c, float r) const;

    bool  active = false;
    AkState state = AkState::Inactive;
    Vector2 pos{}, vel{};
    int   facing = -1;
    float w = 42, h = 66;
    float hp = 0, maxHp = 0;
    int   phase = 1;               // 2 below 40%: he rejoices
    bool  vulnerable = false;
    float guardBroken = 0;
    HitMemory hitMem;

private:
    void ChooseAttack(const Player& player, const Giyu* ally);
    void EnterRecover(float t);

    float stateTimer = 0;
    float decideTimer = 0;
    int   comboHits = 0;
    float comboTick = 0;
    int   dashAttackId = -1;
    float slowTimer = 0;
    float openingCd = 0;
    float poisonT = 0, poisonTick = 0;
    float hitFlash = 0;
    bool  preyAlly = false;
    float leapVx = 0;
    float despSpin = 0;            // desperation barrage spiral angle
    std::vector<FistOrb> orbs;
    std::vector<AkWave> waves;
};

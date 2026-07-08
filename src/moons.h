#pragma once
// ---------------------------------------------------------------
// moons.h — Douma (Upper Moon Two) and Kokushibo (Upper Moon One)
//
// One class, two demons. Each has a distinct kit:
//   DOUMA     — serene ice sorcerer: shard fans, frozen lotus
//               eruptions, a freezing breath that chills the blood.
//   KOKUSHIBO — six-eyed swordsman: moon-arc barrages, arena-wide
//               long slashes (CROUCH beneath them!), flash cross.
//
// Power scale: Giyu < Akaza < Douma < Kokushibo < Muzan.
// ---------------------------------------------------------------
#include "raylib.h"
#include "combat.h"
#include <vector>

class Effects;
class Player;
class Giyu;

enum MoonKind { MOON_DOUMA = 0, MOON_KOKU = 1 };

enum class MState {
    Inactive, Intro, Stalk,
    TeleA, AtkA,       // Douma: ice shard fans      | Koku: moon-arc barrage
    TeleB, AtkB,       // Douma: frozen lotus        | Koku: long slash (duckable)
    TeleC, AtkC,       // Douma: freezing breath     | Koku: flash cross
    Desperation,       // Koku at 40%: "I WILL NOT DIE" ground-crescent storm
    Recover,
    Dying, Dead
};

struct Shard {                 // ice shard / moon crescent
    Vector2 pos{}, vel{};
    float spin = 0;
    bool alive = true;
};

struct Lotus {                 // Douma's delayed ground eruption
    Vector2 pos{};
    float fuse = 1.0f;
    bool done = false;
};

class UpperMoon {
public:
    explicit UpperMoon(int k) : kind(k) {}

    void Reset();
    void Activate(Vector2 p);
    void Update(float dt, Player& player, Giyu* ally, CombatSystem& cs, Effects& fx);
    void Draw() const;
    Rectangle Rect() const;
    void TakeDamage(float dmg, float kbx, HitKind kindHit, Effects& fx);

    bool Alive() const { return active && state != MState::Dead && state != MState::Dying; }
    bool Defeated() const { return active && state == MState::Dead; }

    // Giyu support hooks
    bool ForceOpening(Effects& fx);
    int  NullifyShards(Vector2 c, float r);
    int  ShardsNear(Vector2 c, float r) const;
    bool Menacing(Vector2 a, Vector2 b) const;   // Dead Calm trigger

    int   kind = MOON_DOUMA;
    bool  active = false;
    MState state = MState::Inactive;
    Vector2 pos{}, vel{};
    int   facing = -1;
    float w = 44, h = 70;
    float hp = 0, maxHp = 0;
    int   phase = 1;
    bool  vulnerable = false;
    float guardBroken = 0;
    HitMemory hitMem;

private:
    void ChooseAttack(const Player& player, const Giyu* ally);
    void EnterRecover(float t);

    float stateTimer = 0;
    float decideTimer = 0;
    float tickT = 0;
    int   volley = 0;
    float slowTimer = 0;
    float openingCd = 0;
    float poisonT = 0, poisonTick = 0;
    float hitFlash = 0;
    float ghostA = 1.0f;           // kokushibo flash-step fade
    bool  preyAlly = false;
    std::vector<Shard> shards;
    std::vector<Lotus> lotus;
    Rectangle slashBand{};         // koku long-slash telegraph
    bool  slashArmed = false;
};

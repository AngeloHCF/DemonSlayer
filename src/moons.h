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
class Shinobu;
class Rengoku;
class Gyomei;
class Tengen;

enum MoonKind { MOON_DOUMA = 0, MOON_KOKU = 1 };

enum class MState {
    Inactive, Intro, Stalk,
    TeleA, AtkA,       // Douma: ice shard fans      | Koku: layered crescent barrage
    TeleB, AtkB,       // Douma: frozen lotus        | Koku: long slash (duckable)
    TeleC, AtkC,       // Douma: freezing breath     | Koku: flash cross
    Stare, AtkFlash,   // Koku only: silent glare, then vanish + instant slash
    Combo,             // Koku only: close-range Moon-Dragon sword combo
    Storm,             // Koku phase 3: repeatable sky-and-earth crescent storm
    Desperation,       // Koku at 33%: "I WILL NOT DIE" transformation storm
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
    void Update(float dt, Player& player, Giyu* ally, Shinobu* shinobu,
                Rengoku* rengoku, Gyomei* gyomei, Tengen* tengen,
                CombatSystem& cs, Effects& fx);
    void Draw() const;
    Rectangle Rect() const;
    void TakeDamage(float dmg, float kbx, HitKind kindHit, Effects& fx);

    bool Alive() const { return active && state != MState::Dead && state != MState::Dying; }
    bool Defeated() const { return active && state == MState::Dead; }

    // Giyu support hooks
    bool ForceOpening(Effects& fx);
    int  NullifyShards(Vector2 c, float r);
    int  NullifyShardsInRect(Rectangle r);
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
    float declareT = 0;            // Kokushibo's "I WILL NOT DIE" banner timer
    HitMemory hitMem;

private:
    void ChooseAttack(const Player& player, const Giyu* ally, const Shinobu* shinobu,
                      const Rengoku* rengoku, const Gyomei* gyomei, const Tengen* tengen);
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
    bool  preyShinobu = false;
    bool  preyRengoku = false;
    bool  preyGyomei = false;
    bool  preyTengen = false;
    std::vector<Shard> shards;
    std::vector<Lotus> lotus;
    Rectangle slashBand{};         // koku long-slash / flash-slash telegraph
    Rectangle slashBand2{};        // koku second stacked band (phase 2+)
    bool  slashArmed = false;
    bool  slashArmed2 = false;

    // --- Kokushibo: presence, escalation, and suspense --------------
    float auraPulse = 0;           // ever-present menace glow
    float walkBob = 0;             // slow confident stride
    bool  transformed = false;     // has unleashed the 33% transformation
    int   comboHit = 0;            // Moon-Dragon combo step
    float subT = 0;                // sub-phase timer (combos, double slashes)
    int   subStep = 0;             // sub-phase index
    float stareGlare = 0;          // 0..1 intensity while staring the player down
    float stormCd = 0;             // gate on phase-3 crescent storms
    float stareCd = 0;             // gate on the silent-stare ambush
    Vector2 markPos{};             // remembered prey spot for delayed punishes
};

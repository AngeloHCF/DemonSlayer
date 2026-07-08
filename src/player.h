#pragma once
// ---------------------------------------------------------------
// player.h — the swordsman
//   J          : 4-hit sword combo
//   UP + J     : upward launcher slash
//   DOWN + J   : plunging strike (airborne)
//   K L I O U H M (or 1-7) : Water, Fire, Stone, Love,
//                            Serpent, Wind, Mist Breathing
//
// All Breathing Styles scale with the Progression upgrade trees
// (damage / cooldown / reach) and gain Mastery variants.
// ---------------------------------------------------------------
#include "raylib.h"
#include "styles.h"
#include "combat.h"
#include <vector>

class Effects;

enum class PState {
    Normal, Attack, UpSlash, Plunge,
    Water, FireWindup, FireRecover, Stone, Love,
    Serpent, Wind, Mist,
    Hurt, Dead
};

struct AirTech {                 // traveling techniques
    Vector2 pos{};
    int dir = 1;
    float life = 0;
    float tickT = 0;
    int curId = -1;
    bool isQuake = false;        // stone ground-splitter vs wind tornado
};

struct FieldZone {               // lingering ground effects
    Vector2 pos{};
    float life = 0;
    float tickT = 0;
    bool isFire = true;          // fire = burning patch, else slowing mist
};

struct Ghost {                   // mist afterimage decoys
    Vector2 pos{};
    int facing = 1;
    float life = 0;
};

class Player {
public:
    void Reset(Vector2 spawn);
    void Update(float dt, CombatSystem& cs, Effects& fx);
    void Draw() const;
    Rectangle Rect() const;

    // returns false if the hit was ignored (i-frames / dead)
    // heavy = a Demon King's blow: launched flying with a longer stun
    bool TakeDamage(float dmg, float kbx, Effects& fx, bool heavy = false);
    void Heal(float amount, Effects& fx);

    Vector2 pos{}, vel{};        // pos = center of body
    int facing = 1;
    float hp = 0, maxHp = 0;
    float cd[STYLE_COUNT] = {};  // per-style cooldown timers
    float iframes = 0;
    HitMemory hitMem;            // enemy attack ids that already connected
    PState state = PState::Normal;
    bool onGround = false;
    float w = 34, h = 56;

    Progression* prog = nullptr; // set by Game

    float crouchT = 0;           // 0 standing .. 1 fully crouched (Left Shift)
    float chillT = 0;            // Douma's frost: slowed steps
    float hiddenT = 0;           // mist invisibility remaining
    // hint set by Game each frame: x of nearest living target (homing styles)
    float huntX = 0;
    bool  hasHunt = false;

private:
    void StartCombo(int stage);
    void UpdateAttack(float dt, CombatSystem& cs, Effects& fx);
    void UpdateTechs(float dt, CombatSystem& cs, Effects& fx);
    // central hitbox spawner: applies style damage upgrades + mist ambush
    void AddHit(CombatSystem& cs, Effects& fx, Rectangle r, float dmg,
                float kbx, float kby, float life, HitKind kind, int id, int style);

    std::vector<AirTech> techs;
    std::vector<FieldZone> zones;
    std::vector<Ghost> ghosts;

    float stateTimer = 0;
    int   comboStage = 0;        // 1..4 while attacking
    bool  comboQueued = false;
    bool  didHitFrame = false;
    int   multiAttackId = -1;    // shared id for multi-frame attacks
    float waterTick = 0;
    int   waterPass = 0;         // mastery: returning flux
    int   loveSeg = 0;
    float serpentTick = 0;
    float mistAmbushT = 0;       // next strike from the mist deals double
    float hurtLen = 0.28f;       // current hitstun duration
    float hurtFlash = 0;
    float runPhase = 0;
    bool  wasAirborne = false;
};

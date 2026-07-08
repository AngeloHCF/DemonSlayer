#pragma once
// ---------------------------------------------------------------
// enemy.h — standard demons: three tiers, simple chase AI
// ---------------------------------------------------------------
#include "raylib.h"
#include "combat.h"

class Effects;

enum class EType { Basic, Fast, Brute };

class Enemy {
public:
    Enemy(EType t, Vector2 spawn, int wave);

    // playerHidden: Mist Breathing — demons lose track of the player
    void Update(float dt, Vector2 playerPos, CombatSystem& cs, Effects& fx,
                bool playerHidden);
    void Draw() const;
    Rectangle Rect() const;
    void TakeDamage(float dmg, float kbx, HitKind kind, Effects& fx);

    bool  alive = true;
    bool  counted = false;       // kill accounting (poison can kill outside combat)
    EType type = EType::Basic;
    Vector2 pos{}, vel{};        // pos = center
    int   facing = 1;
    float w = 34, h = 50;
    float hp = 1, maxHp = 1;
    float speed = 100, dmg = 10;
    int   scoreValue = 10;
    HitMemory hitMem;            // attack ids that already struck this demon
    float armorBreak = 0;        // Stone Breathing debuff: +damage, brutes lose armor
    Color col{};

    bool Busy() const { return windup > 0 || lungeTimer > 0; }

private:
    float attackCd = 0;
    float aggro = 1;             // late waves: quicker windups, shorter rests
    float standoff = 60;         // personal engagement distance (prevents stacking)
    float windup = 0;            // telegraph before striking
    float lungeTimer = 0;
    float stun = 0;
    float slowTimer = 0;         // Water Breathing slow
    float poisonT = 0;           // Serpent Breathing damage-over-time
    float poisonTick = 0;
    float hitFlash = 0;
};

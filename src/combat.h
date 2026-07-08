#pragma once
// ---------------------------------------------------------------
// combat.h — shared hitbox system
//
// Attacks register short-lived world-space hitboxes here; the Game
// resolves overlaps against entities each frame. Each attack gets a
// unique id so multi-frame attacks (dashes) hit a target only once —
// entities remember the last attack id that hit them.
// ---------------------------------------------------------------
#include "raylib.h"
#include <vector>

enum class Team    { Player, Enemy };
enum class HitKind { Basic, Water, Fire, Stone, Love, Serpent, Wind, Giyu, Shinobu, Rengoku,
                     EnemyMelee, BossDash, BossAoe, BossProjectile };

struct Hitbox {
    Rectangle rect{};
    float damage = 0;
    float kbX = 0, kbY = 0;   // knockback impulse
    float life = 0;           // seconds remaining active
    Team team = Team::Player;
    HitKind kind = HitKind::Basic;
    int attackId = -1;
};

// Remembers the last several attack ids that already struck an entity.
// A single "last id" slot ping-pongs when two multi-frame attacks overlap
// the same target (both re-hit every frame -> instant 10,000+ damage).
struct HitMemory {
    static const int N = 12;
    int ids[N];
    int idx = 0;
    HitMemory() { Clear(); }
    bool Seen(int id) const {
        for (int i = 0; i < N; i++)
            if (ids[i] == id) return true;
        return false;
    }
    void Remember(int id) { ids[idx] = id; idx = (idx + 1) % N; }
    void Clear() { for (int i = 0; i < N; i++) ids[i] = -1; idx = 0; }
};

class CombatSystem {
public:
    int NewId() { return ++idCounter; }

    void Add(Rectangle r, float dmg, float kbx, float kby, float life,
             Team team, HitKind kind, int attackId) {
        Hitbox h;
        h.rect = r; h.damage = dmg; h.kbX = kbx; h.kbY = kby;
        h.life = life; h.team = team; h.kind = kind; h.attackId = attackId;
        boxes.push_back(h);
    }

    void Update(float dt);
    void Clear() { boxes.clear(); }
    std::vector<Hitbox>& Boxes() { return boxes; }

private:
    std::vector<Hitbox> boxes;
    int idCounter = 0;
};

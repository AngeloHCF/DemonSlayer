#include "combat.h"
#include <algorithm>

void CombatSystem::Update(float dt) {
    for (auto& b : boxes) b.life -= dt;
    boxes.erase(std::remove_if(boxes.begin(), boxes.end(),
                [](const Hitbox& b) { return b.life <= 0; }), boxes.end());
}

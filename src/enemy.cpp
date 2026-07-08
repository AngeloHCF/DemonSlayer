#include "enemy.h"
#include "effects.h"
#include "config.h"
#include "audio.h"
#include <cmath>

Enemy::Enemy(EType t, Vector2 spawn, int wave) {
    type = t;
    pos = spawn;
    switch (t) {
        case EType::Basic:
            maxHp = 42;  speed = 125; dmg = 10; w = 34; h = 50;
            scoreValue = 10; col = C(150, 42, 52);
            break;
        case EType::Fast:
            maxHp = 26;  speed = 235; dmg = 8;  w = 26; h = 40;
            scoreValue = 15; col = C(125, 62, 165);
            break;
        case EType::Brute:
            maxHp = 130; speed = 72;  dmg = 18; w = 52; h = 68;
            scoreValue = 30; col = C(78, 96, 58);
            break;
    }
    // difficulty scaling per wave (the night only gets crueler)
    float hpScale  = 1.0f + 0.13f * (wave - 1);
    float spdScale = 1.0f + Clampf(0.05f * (wave - 1), 0, 0.55f);
    maxHp *= hpScale;
    hp = maxHp;
    speed *= spdScale;
    dmg *= 1.0f + 0.06f * (wave - 1);
    aggro = 1.0f + Clampf(0.03f * (wave - 1), 0, 0.45f);
    attackCd = frnd(0.4f, 1.2f);
    // each demon keeps its own distance so packs form a loose arc, not a stack
    standoff = (t == EType::Fast) ? frnd(36, 64) : frnd(42, 78);
}

Rectangle Enemy::Rect() const {
    return { pos.x - w * 0.5f, pos.y - h * 0.5f, w, h };
}

void Enemy::Update(float dt, Vector2 playerPos, CombatSystem& cs, Effects& fx,
                   bool playerHidden) {
    if (!alive) return;

    hitFlash  = fmaxf(hitFlash - dt, 0);
    attackCd  = fmaxf(attackCd - dt, 0);
    slowTimer = fmaxf(slowTimer - dt, 0);
    armorBreak = fmaxf(armorBreak - dt, 0);

    // serpent venom: damage over time
    if (poisonT > 0) {
        poisonT -= dt;
        poisonTick -= dt;
        if (poisonTick <= 0) {
            poisonTick = 0.5f;
            hp -= 2.0f;
            fx.Text({ pos.x, pos.y - h * 0.5f - 8 }, C(140, 220, 90), 0.75f, "2");
            if (hp <= 0) {
                alive = false;
                fx.DeathBurst(pos, C(120, 200, 80), 1.0f);
                fx.Blood(pos, facing);
                fx.Text({ pos.x, pos.y - h }, C(140, 220, 90), 1.0f, "VENOM");
                PlaySfx(SFX_DEATH, 0.6f, 1.2f);
                return;
            }
        }
    }

    float spd = speed * (slowTimer > 0 ? 0.45f : 1.0f);
    if (playerHidden) spd *= 0.5f;      // lost in the mist

    if (stun > 0) {
        stun -= dt;
        vel.x *= 1.0f - Clampf(8.0f * dt, 0, 1);
    }
    else if (lungeTimer > 0) {
        lungeTimer -= dt;   // keep lunge momentum
    }
    else if (windup > 0) {
        windup -= dt;
        vel.x = 0;
        if (windup <= 0) {
            // strike!
            lungeTimer = 0.18f;
            vel.x = facing * spd * 3.4f;
            float reach = 50.0f + w * 0.35f;
            Rectangle r = {
                facing > 0 ? pos.x : pos.x - reach,
                pos.y - 30, reach, h * 0.9f
            };
            cs.Add(r, dmg, facing * 250.0f, -150, 0.12f,
                   Team::Enemy, HitKind::EnemyMelee, cs.NewId());
            fx.Sparks({ pos.x + facing * w * 0.6f, pos.y - 6 },
                      facing > 0 ? 0.0f : 180.0f, 50, 5, C(255, 235, 235), 300, 2.5f);
            attackCd = frnd(1.0f, 1.7f) / aggro;
        }
    }
    else {
        float dx = playerPos.x - pos.x;
        facing = dx > 0 ? 1 : -1;
        if (fabsf(dx) > standoff) vel.x = facing * spd;
        else vel.x *= 1.0f - Clampf(8.0f * dt, 0, 1);

        // can't line up a strike on a hidden swordsman
        if (!playerHidden &&
            fabsf(dx) < standoff + 26 && fabsf(playerPos.y - pos.y) < 64 && attackCd <= 0)
            windup = ((type == EType::Brute) ? 0.5f : 0.35f) / aggro;
    }

    vel.y += cfg::GRAVITY * dt;
    pos.x += vel.x * dt;
    pos.y += vel.y * dt;
    pos.x = Clampf(pos.x, -90.0f, (float)cfg::SCREEN_W + 90.0f);
    GroundClamp(pos, vel, h * 0.5f);
}

void Enemy::TakeDamage(float damage, float kbx, HitKind kind, Effects& fx) {
    if (!alive) return;

    if (damage <= 0) {                      // status-only field (mist cloud)
        if (kind == HitKind::Water) slowTimer = fmaxf(slowTimer, 1.0f);
        if (kind == HitKind::Shinobu) poisonT = fmaxf(poisonT, 2.5f);
        return;
    }

    if (armorBreak > 0) damage *= 1.25f;    // Stone Breathing debuff
    hp -= damage;
    hitFlash = 0.12f;

    Color hitCol = C(255, 245, 220);
    if (kind == HitKind::Water)   { hitCol = C(110, 190, 255); slowTimer = 2.0f; }
    if (kind == HitKind::Fire)    { hitCol = C(255, 170, 60); }
    if (kind == HitKind::Stone)   { hitCol = C(210, 200, 185); armorBreak = 4.0f; }
    if (kind == HitKind::Love)    { hitCol = C(255, 150, 205); }
    if (kind == HitKind::Serpent) { hitCol = C(140, 220, 90); poisonT = 3.0f; }
    if (kind == HitKind::Wind)    { hitCol = C(215, 245, 230); }
    if (kind == HitKind::Giyu)    { hitCol = C(120, 190, 255); slowTimer = fmaxf(slowTimer, 1.2f); }
    if (kind == HitKind::Shinobu) { hitCol = C(190, 150, 255); poisonT = fmaxf(poisonT, 4.0f); }
    if (kind == HitKind::Rengoku) { hitCol = C(255, 150, 55); }
    if (kind == HitKind::Gyomei)  { hitCol = C(188, 178, 158); armorBreak = fmaxf(armorBreak, 4.5f); }
    if (kind == HitKind::Tengen)  { hitCol = C(255, 212, 88); }
    if (kind == HitKind::Sanemi)  { hitCol = C(205, 245, 226); slowTimer = fmaxf(slowTimer, 0.65f); }

    if (type != EType::Brute || armorBreak > 0) {
        // light demons get interrupted and knocked back (stone breaks brute armor)
        stun = 0.18f;
        windup = 0;
        lungeTimer = 0;
        vel.x = kbx;
        vel.y = (kind == HitKind::Wind || kind == HitKind::Rengoku ||
                 kind == HitKind::Gyomei || kind == HitKind::Tengen ||
                 kind == HitKind::Sanemi)
                ? -320.0f : -170.0f;   // gales and Hashira burst attacks launch
    } else {
        vel.x = kbx * 0.25f;    // brutes have knockback armor
    }

    fx.HitSparks({ pos.x, pos.y - 8 }, kbx >= 0 ? 1 : -1, hitCol);
    fx.Blood({ pos.x, pos.y - 6 }, kbx >= 0 ? 1 : -1);      // demons bleed too
    fx.Text({ pos.x, pos.y - h * 0.5f - 14 }, hitCol, 1.0f, "%.0f", damage);

    if (hp <= 0) {
        alive = false;
        float sc = (type == EType::Brute) ? 1.5f : 1.0f;
        fx.DeathBurst(pos, col, sc);
        fx.BloodSpray(pos, kbx >= 0 ? 1 : -1, sc * 1.2f);   // torn apart
        fx.AddShake(type == EType::Brute ? 0.3f : 0.14f);
        PlaySfx(SFX_DEATH, 0.7f);
    }
}

void Enemy::Draw() const {
    if (!alive) return;
    float gt = (float)GetTime();

    // shadow
    DrawEllipse((int)pos.x, (int)(cfg::GROUND_Y + 6), (int)(w * 0.55f), 6, Fade(BLACK, 0.3f));

    float lean = 0;
    Color bodyC = col;
    if (windup > 0) {
        lean = -facing * 6.0f;                       // rear back = telegraph
        if (fmodf(gt * 10.0f, 1.0f) < 0.5f) bodyC = C(235, 110, 100);
    }
    if (hitFlash > 0) bodyC = C(255, 255, 255);

    Rectangle body = { pos.x - w * 0.5f + lean, pos.y - h * 0.5f, w, h };
    DrawRectangleRounded(body, 0.4f, 6, bodyC);
    // darker under-shade
    DrawRectangleRounded({ body.x + 3, body.y + h * 0.55f, w - 6, h * 0.4f }, 0.5f, 6,
                         Fade(BLACK, 0.25f));

    // horns
    float hx = pos.x + lean, hy = pos.y - h * 0.5f;
    Color hornC = C(40, 26, 32);
    DrawTriangle({ hx - w * 0.22f, hy - 10 }, { hx - w * 0.34f, hy + 2 }, { hx - w * 0.1f, hy + 2 }, hornC);
    DrawTriangle({ hx + w * 0.22f, hy - 10 }, { hx + w * 0.1f, hy + 2 }, { hx + w * 0.34f, hy + 2 }, hornC);

    // glowing eyes
    float ey = pos.y - h * 0.22f;
    Color eyeC = (type == EType::Fast) ? C(255, 120, 255) : C(255, 210, 60);
    DrawCircleV({ hx + facing * w * 0.16f, ey }, 3.2f, eyeC);
    DrawCircleV({ hx + facing * w * 0.34f, ey }, 3.2f, eyeC);

    // claws in front
    for (int i = 0; i < 3; i++) {
        float cy = pos.y - 4 + i * 6.0f;
        Vector2 a = { pos.x + facing * (w * 0.5f - 2) + lean, cy };
        Vector2 b = { a.x + facing * 10.0f, cy + 3 };
        DrawLineEx(a, b, 2, C(220, 220, 210));
    }

    // status tints
    if (slowTimer > 0)
        DrawRectangleRounded(body, 0.4f, 6, Fade(C(90, 170, 255), 0.32f));
    if (poisonT > 0)
        DrawRectangleRounded(body, 0.4f, 6, Fade(C(120, 210, 80), 0.28f));
    // armor break: cracked gray outline
    if (armorBreak > 0)
        DrawRectangleLinesEx({ body.x - 2, body.y - 2, body.width + 4, body.height + 4 },
                             2, Fade(C(210, 200, 185), 0.5f + 0.3f * sinf(gt * 12)));

    // hp bar
    if (hp < maxHp) {
        float bw = w + 8;
        float f = Clampf(hp / maxHp, 0, 1);
        DrawRectangle((int)(pos.x - bw * 0.5f), (int)(pos.y - h * 0.5f - 14), (int)bw, 5, C(20, 14, 18));
        DrawRectangle((int)(pos.x - bw * 0.5f + 1), (int)(pos.y - h * 0.5f - 13),
                      (int)((bw - 2) * f), 3, C(220, 60, 60));
    }
}

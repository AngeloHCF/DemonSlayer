#pragma once
// ---------------------------------------------------------------
// styles.h — Breathing Style progression (header-only)
//
// Clearing a wave grants upgrade points. Each style has three
// leveled tracks plus a unique Mastery unlock:
//   POWER  : +30% damage per level            (max 3, 1 pt each)
//   FLOW   : -18% cooldown per level          (max 3, 1 pt each)
//   REACH  : +20% range/duration/technique    (max 3, 1 pt each)
//            speed per level
//   MASTERY: stronger / alternate move        (2 pts)
// ---------------------------------------------------------------

enum StyleId {
    STYLE_WATER, STYLE_FIRE, STYLE_STONE, STYLE_LOVE,
    STYLE_SERPENT, STYLE_WIND, STYLE_MIST,
    STYLE_COUNT
};

enum UpTrack { TRACK_POWER, TRACK_FLOW, TRACK_REACH, TRACK_MASTERY };

// The number key that activates the equipped Breathing Style. Only one style is
// ever equipped, so every style uses the same key: 1. Keep STYLE_INFO[].key
// (game.cpp) in sync with this.
inline int StyleKeyNumber(int /*s*/) { return 1; }

// ---------------------------------------------------------------
// StyleUnlocks — which Breathing Styles the player is allowed to
// equip. This is account-level state (it must survive between runs),
// so it lives OUTSIDE Progression, which resets at the start of every
// run. Water is the first form every slayer learns; the rest start
// locked until unlocked (e.g. the --unlock-all developer flag).
// ---------------------------------------------------------------
class StyleUnlocks {
public:
    // every Breathing Style is available from the start; the lock machinery
    // stays in place so specific styles can be gated later if desired.
    StyleUnlocks() { UnlockAll(); }

    bool IsUnlocked(int s) const {
        return s >= 0 && s < STYLE_COUNT && unlocked[s];
    }
    void Unlock(int s)  { if (s >= 0 && s < STYLE_COUNT) unlocked[s] = true; }
    void UnlockAll()    { for (int i = 0; i < STYLE_COUNT; i++) unlocked[i] = true; }

    // the first unlocked style — a safe fallback when the desired one is locked
    int FirstUnlocked() const {
        for (int i = 0; i < STYLE_COUNT; i++) if (unlocked[i]) return i;
        return STYLE_WATER;
    }
    int Count() const {
        int n = 0;
        for (int i = 0; i < STYLE_COUNT; i++) if (unlocked[i]) n++;
        return n;
    }

private:
    bool unlocked[STYLE_COUNT] = {};
};

struct StyleUpgrades {
    int power = 0, flow = 0, reach = 0;   // 0..3
    bool mastery = false;
};

class Progression {
public:
    int points = 0;
    StyleUpgrades up[STYLE_COUNT];

    void Reset() {
        points = 0;
        for (int i = 0; i < STYLE_COUNT; i++) up[i] = StyleUpgrades{};
    }

    void UnlockAll() {
        points = 100;
        for (int i = 0; i < STYLE_COUNT; i++) {
            up[i].power = 3;
            up[i].flow = 3;
            up[i].reach = 3;
            up[i].mastery = true;
        }
    }

    float DmgMult(int s) const   { return 1.0f + 0.30f * up[s].power; }
    float CdMult(int s) const    { return 1.0f - 0.18f * up[s].flow; }
    float ReachMult(int s) const { return 1.0f + 0.20f * up[s].reach; }
    bool  Mastery(int s) const   { return up[s].mastery; }

    int TrackLevel(int s, int track) const {
        switch (track) {
            case TRACK_POWER: return up[s].power;
            case TRACK_FLOW:  return up[s].flow;
            case TRACK_REACH: return up[s].reach;
            default:          return up[s].mastery ? 1 : 0;
        }
    }
    int Cost(int track) const { return track == TRACK_MASTERY ? 2 : 1; }

    bool CanBuy(int s, int track) const {
        if (points < Cost(track)) return false;
        if (track == TRACK_MASTERY) return !up[s].mastery;
        return TrackLevel(s, track) < 3;
    }

    bool Buy(int s, int track) {
        if (!CanBuy(s, track)) return false;
        points -= Cost(track);
        switch (track) {
            case TRACK_POWER: up[s].power++; break;
            case TRACK_FLOW:  up[s].flow++;  break;
            case TRACK_REACH: up[s].reach++; break;
            default:          up[s].mastery = true; break;
        }
        return true;
    }
};

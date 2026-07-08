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

struct StyleUpgrades {
    int power = 0, flow = 0, reach = 0;   // 0..3
    bool mastery = false;
};

class Progression {
public:
    int points = 100;
    StyleUpgrades up[STYLE_COUNT];

    void Reset() {
        points = 100;
        for (int i = 0; i < STYLE_COUNT; i++) up[i] = StyleUpgrades{};
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

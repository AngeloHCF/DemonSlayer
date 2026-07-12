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

// The number key that activates a simple equipped Breathing Style. Water and
// Flame override this with per-form number keys.
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

// ---------------------------------------------------------------
// Water Breathing — eleven forms, each an independent ability that
// levels 1..5 on its own. Players start every run at Level 1 in all
// forms and spend upgrade points to specialise the techniques they
// use most. Level 5 is full mastery (peak damage/reach, min cooldown,
// enhanced visuals, and a signature bonus property per form).
// ---------------------------------------------------------------
enum WaterForm {
    WF_SURFACE_SLASH,   // 1  Water Surface Slash
    WF_WATER_WHEEL,     // 2  Water Wheel
    WF_FLOWING_DANCE,   // 3  Flowing Dance
    WF_STRIKING_TIDE,   // 4  Striking Tide
    WF_BLESSED_RAIN,    // 5  Blessed Rain After the Drought
    WF_WHIRLPOOL,       // 6  Whirlpool
    WF_DROP_RIPPLE,     // 7  Drop Ripple Thrust
    WF_WATERFALL_BASIN, // 8  Waterfall Basin
    WF_SPLASHING_FLOW,  // 9  Splashing Water Flow, Turbulent
    WF_CONSTANT_FLUX,   // 10 Constant Flux
    WF_DEAD_CALM,       // 11 Dead Calm
    WATER_FORM_COUNT
};

enum FlameForm {
    FF_UNKNOWING_FIRE,       // 1  Unknowing Fire
    FF_RISING_SUN,           // 2  Rising Scorching Sun
    FF_BLAZING_UNIVERSE,     // 3  Blazing Universe
    FF_BLOOMING_UNDULATION,  // 4  Blooming Flame Undulation
    FF_FLAME_TIGER,          // 5  Flame Tiger
    FF_SOLAR_HEAT_HAZE,      // 6  Solar Heat Haze (original)
    FF_INFERNO_WHEEL,        // 7  Inferno Wheel (original)
    FF_CRIMSON_LOTUS,        // 8  Crimson Lotus Crest (original)
    FF_RENGOKU,              // 9  Rengoku
    FLAME_FORM_COUNT
};

enum StoneForm {
    SF_SERPENTINITE_BIPOLAR, // 1 First Form: Serpentinite Bipolar
    SF_UPPER_SMASH,          // 2 Second Form: Upper Smash
    SF_STONE_SKIN,           // 3 Third Form: Stone Skin
    SF_RAPID_CONQUEST,       // 4 Fourth Form: Volcanic Rock, Rapid Conquest
    SF_ARCS_OF_JUSTICE,      // 5 Fifth Form: Arcs of Justice
    STONE_FORM_COUNT
};

enum LoveForm {
    LF_SHIVERS_FIRST_LOVE,       // 1 First Form: Shivers of First Love
    LF_LOVE_PANGS,               // 2 Second Form: Love Pangs
    LF_CATLOVE_SHOWER,           // 3 Third Form: Catlove Shower
    LF_SWAYING_WILDCLAW,         // 4 Fifth Form: Swaying Love, Wildclaw
    LF_CAT_LEGGED_WINDS,         // 5 Sixth Form: Cat-Legged Winds of Love
    LF_RIPPING_KITTY_TEMPEST,    // 6 Final Form: Ripping Kitty Tempest of Love
    LOVE_FORM_COUNT
};

// Flame Fighting Styles are passive loadouts layered on top of Flame
// Breathing's per-form mastery. Only one can be equipped at a time.
enum FlameFightingStyle {
    FLAME_FS_OFFENSIVE,
    FLAME_FS_DEFENSIVE,
    FLAME_FS_SWIFT,
    FLAME_FS_ENDURANCE,
    FLAME_FS_MASTERY,
    FLAME_FS_COUNT
};

// base cooldown (seconds) per form, before the per-level reduction.
// shared by the player (execution) and game (HUD ring / menu display).
inline float WaterFormBaseCd(int f) {
    static const float cd[WATER_FORM_COUNT] = {
        1.2f, 3.0f, 4.0f, 2.6f, 5.0f, 3.6f, 1.8f, 3.2f, 4.6f, 6.5f, 9.0f
    };
    return (f >= 0 && f < WATER_FORM_COUNT) ? cd[f] : 1.0f;
}

inline float FlameFormBaseCd(int f) {
    static const float cd[FLAME_FORM_COUNT] = {
        1.4f, 2.8f, 4.2f, 6.5f, 5.2f, 3.4f, 4.8f, 5.8f, 10.5f
    };
    return (f >= 0 && f < FLAME_FORM_COUNT) ? cd[f] : 1.0f;
}

inline float StoneFormBaseCd(int f) {
    static const float cd[STONE_FORM_COUNT] = {
        3.8f, 5.6f, 8.5f, 6.8f, 14.0f
    };
    return (f >= 0 && f < STONE_FORM_COUNT) ? cd[f] : 1.0f;
}

inline float LoveFormBaseCd(int f) {
    static const float cd[LOVE_FORM_COUNT] = {
        1.35f, 3.25f, 4.4f, 5.7f, 6.4f, 11.5f
    };
    return (f >= 0 && f < LOVE_FORM_COUNT) ? cd[f] : 1.0f;
}

struct WaterForms {
    int level[WATER_FORM_COUNT];   // 1..5 (never 0)

    WaterForms() { Reset(); }
    void Reset()   { for (int i = 0; i < WATER_FORM_COUNT; i++) level[i] = 1; }
    void MaxAll()  { for (int i = 0; i < WATER_FORM_COUNT; i++) level[i] = 5; }

    int  Level(int f) const { return (f >= 0 && f < WATER_FORM_COUNT) ? level[f] : 1; }
    bool Maxed(int f) const { return Level(f) >= 5; }

    // raising a form from level L costs L points (1,2,3,4 -> 10 to fully master)
    int  Cost(int f) const { return Level(f); }
    bool CanUpgrade(int f, int points) const { return !Maxed(f) && points >= Cost(f); }

    // per-level scaling knobs (Level 1 = baseline, Level 5 = mastery)
    float DmgMult(int f)   const { return 1.0f + 0.28f * (Level(f) - 1); } // -> 2.12x
    float RangeMult(int f) const { return 1.0f + 0.11f * (Level(f) - 1); } // -> 1.44x
    float CdMult(int f)    const { return 1.0f - 0.10f * (Level(f) - 1); } // -> 0.60x
    float SpeedMult(int f) const { return 1.0f + 0.09f * (Level(f) - 1); } // -> 1.36x
};

struct FlameForms {
    int level[FLAME_FORM_COUNT];   // 1..5 (never 0)

    FlameForms() { Reset(); }
    void Reset()   { for (int i = 0; i < FLAME_FORM_COUNT; i++) level[i] = 1; }
    void MaxAll()  { for (int i = 0; i < FLAME_FORM_COUNT; i++) level[i] = 5; }

    int  Level(int f) const { return (f >= 0 && f < FLAME_FORM_COUNT) ? level[f] : 1; }
    bool Maxed(int f) const { return Level(f) >= 5; }

    int  Cost(int f) const { return Level(f); }
    bool CanUpgrade(int f, int points) const { return !Maxed(f) && points >= Cost(f); }

    float DmgMult(int f)   const { return 1.0f + 0.30f * (Level(f) - 1); } // -> 2.20x
    float RangeMult(int f) const { return 1.0f + 0.12f * (Level(f) - 1); } // -> 1.48x
    float CdMult(int f)    const { return 1.0f - 0.09f * (Level(f) - 1); } // -> 0.64x
    float SpeedMult(int f) const { return 1.0f + 0.08f * (Level(f) - 1); } // -> 1.32x
};

struct StoneForms {
    int level[STONE_FORM_COUNT];   // 1..5

    StoneForms() { Reset(); }
    void Reset()   { for (int i = 0; i < STONE_FORM_COUNT; i++) level[i] = 1; }
    void MaxAll()  { for (int i = 0; i < STONE_FORM_COUNT; i++) level[i] = 5; }

    int  Level(int f) const { return (f >= 0 && f < STONE_FORM_COUNT) ? level[f] : 1; }
    bool Maxed(int f) const { return Level(f) >= 5; }

    int  Cost(int f) const { return Level(f); }
    bool CanUpgrade(int f, int points) const { return !Maxed(f) && points >= Cost(f); }

    float DmgMult(int f)    const { return 1.0f + 0.26f * (Level(f) - 1); } // -> 2.04x
    float RangeMult(int f)  const { return 1.0f + 0.12f * (Level(f) - 1); } // -> 1.48x
    float CdMult(int f)     const { return 1.0f - 0.08f * (Level(f) - 1); } // -> 0.68x
    float SpeedMult(int f)  const { return 1.0f + 0.045f * (Level(f) - 1); } // -> 1.18x
    float GuardMult(int f)  const { return 1.0f + 0.24f * (Level(f) - 1); } // -> 1.96x
};

struct LoveForms {
    int level[LOVE_FORM_COUNT];    // 1..5

    LoveForms() { Reset(); }
    void Reset()   { for (int i = 0; i < LOVE_FORM_COUNT; i++) level[i] = 1; }
    void MaxAll()  { for (int i = 0; i < LOVE_FORM_COUNT; i++) level[i] = 5; }

    int  Level(int f) const { return (f >= 0 && f < LOVE_FORM_COUNT) ? level[f] : 1; }
    bool Maxed(int f) const { return Level(f) >= 5; }

    int  Cost(int f) const { return Level(f); }
    bool CanUpgrade(int f, int points) const { return !Maxed(f) && points >= Cost(f); }

    float DmgMult(int f)   const { return 1.0f + 0.27f * (Level(f) - 1); } // -> 2.08x
    float RangeMult(int f) const { return 1.0f + 0.12f * (Level(f) - 1); } // -> 1.48x
    float CdMult(int f)    const { return 1.0f - 0.09f * (Level(f) - 1); } // -> 0.64x
    float SpeedMult(int f) const { return 1.0f + 0.09f * (Level(f) - 1); } // -> 1.36x
    float MendMult(int f)  const { return 1.0f + 0.12f * (Level(f) - 1); } // -> 1.48x
};

struct FlameFightingStyles {
    int level[FLAME_FS_COUNT];       // 0 locked, then 1..5
    int equipped = FLAME_FS_OFFENSIVE;

    FlameFightingStyles() { Reset(); }
    void Reset() {
        for (int i = 0; i < FLAME_FS_COUNT; i++) level[i] = 0;
        equipped = FLAME_FS_OFFENSIVE;
    }
    void MaxAll() {
        for (int i = 0; i < FLAME_FS_COUNT; i++) level[i] = 5;
        equipped = FLAME_FS_MASTERY;
    }

    int  Level(int s) const { return (s >= 0 && s < FLAME_FS_COUNT) ? level[s] : 0; }
    bool Unlocked(int s) const { return Level(s) > 0; }
    bool Maxed(int s) const { return Level(s) >= 5; }
    bool Equipped(int s) const { return s >= 0 && s < FLAME_FS_COUNT && equipped == s; }
    int  ActiveLevel(int s) const { return Equipped(s) ? Level(s) : 0; }

    int  Cost(int s) const { return Level(s) + 1; } // 1,2,3,4,5 points
    bool CanUpgrade(int s, int points) const {
        return s >= 0 && s < FLAME_FS_COUNT && !Maxed(s) && points >= Cost(s);
    }
    bool Upgrade(int s, int& points) {
        if (!CanUpgrade(s, points)) return false;
        points -= Cost(s);
        level[s]++;
        equipped = s;
        return true;
    }
    bool Equip(int s) {
        if (!Unlocked(s)) return false;
        equipped = s;
        return true;
    }

    float BasicDamageMult() const {
        int l = ActiveLevel(FLAME_FS_OFFENSIVE);
        return l ? 1.0f + 0.07f * l + (l >= 5 ? 0.05f : 0.0f) : 1.0f;
    }
    float FlameDamageMult() const {
        int off = ActiveLevel(FLAME_FS_OFFENSIVE);
        if (off) return 1.0f + 0.07f * off + (off >= 5 ? 0.05f : 0.0f);
        int mas = ActiveLevel(FLAME_FS_MASTERY);
        return mas ? 1.0f + 0.035f * mas + (mas >= 5 ? 0.04f : 0.0f) : 1.0f;
    }
    float BasicSpeedMult() const {
        int off = ActiveLevel(FLAME_FS_OFFENSIVE);
        if (off) return 1.0f + 0.05f * off + (off >= 5 ? 0.03f : 0.0f);
        int sw = ActiveLevel(FLAME_FS_SWIFT);
        return sw ? 1.0f + 0.04f * sw + (sw >= 5 ? 0.04f : 0.0f) : 1.0f;
    }
    float CritChance() const {
        int l = ActiveLevel(FLAME_FS_OFFENSIVE);
        return l ? 0.035f * l + (l >= 5 ? 0.075f : 0.0f) : 0.0f;
    }
    float CritMult() const { return ActiveLevel(FLAME_FS_OFFENSIVE) >= 5 ? 1.9f : 1.55f; }

    float DamageTakenMult() const {
        static const float m[6] = { 1.0f, 0.94f, 0.90f, 0.86f, 0.82f, 0.74f };
        return m[ActiveLevel(FLAME_FS_DEFENSIVE)];
    }
    float KnockbackTakenMult() const {
        static const float m[6] = { 1.0f, 0.96f, 0.92f, 0.88f, 0.84f, 0.76f };
        return m[ActiveLevel(FLAME_FS_DEFENSIVE)];
    }
    float BarrierDurabilityMult() const {
        int l = ActiveLevel(FLAME_FS_DEFENSIVE);
        return l ? 1.0f + 0.18f * l + (l >= 5 ? 0.35f : 0.0f) : 1.0f;
    }
    float BarrierRadiusMult() const {
        int l = ActiveLevel(FLAME_FS_DEFENSIVE);
        return l ? 1.0f + 0.025f * l + (l >= 5 ? 0.05f : 0.0f) : 1.0f;
    }

    float MoveSpeedMult() const {
        int l = ActiveLevel(FLAME_FS_SWIFT);
        return l ? 1.0f + 0.045f * l + (l >= 5 ? 0.05f : 0.0f) : 1.0f;
    }
    float DashDistanceMult() const {
        int l = ActiveLevel(FLAME_FS_SWIFT);
        return l ? 1.0f + 0.055f * l + (l >= 5 ? 0.09f : 0.0f) : 1.0f;
    }
    float FlameSpeedMult() const {
        int sw = ActiveLevel(FLAME_FS_SWIFT);
        if (sw) return 1.0f + 0.045f * sw + (sw >= 5 ? 0.055f : 0.0f);
        int mas = ActiveLevel(FLAME_FS_MASTERY);
        return mas ? 1.0f + 0.018f * mas : 1.0f;
    }
    float DodgeRecoveryMult() const {
        int l = ActiveLevel(FLAME_FS_SWIFT);
        return l ? 1.0f - 0.05f * l - (l >= 5 ? 0.07f : 0.0f) : 1.0f;
    }

    float MaxHealthMult() const {
        int l = ActiveLevel(FLAME_FS_ENDURANCE);
        return l ? 1.0f + 0.06f * l + (l >= 5 ? 0.12f : 0.0f) : 1.0f;
    }
    float MaxStaminaMult() const {
        int l = ActiveLevel(FLAME_FS_ENDURANCE);
        return l ? 1.0f + 0.10f * l + (l >= 5 ? 0.15f : 0.0f) : 1.0f;
    }
    float CrowdControlMult() const {
        int end = ActiveLevel(FLAME_FS_ENDURANCE);
        if (end) return 1.0f - 0.055f * end - (end >= 5 ? 0.10f : 0.0f);
        return DodgeRecoveryMult();
    }

    float CooldownMult() const {
        int l = ActiveLevel(FLAME_FS_MASTERY);
        return l ? 1.0f - 0.055f * l - (l >= 5 ? 0.045f : 0.0f) : 1.0f;
    }
    float GaugeCostMult() const {
        int l = ActiveLevel(FLAME_FS_MASTERY);
        return l ? 1.0f - 0.055f * l - (l >= 5 ? 0.08f : 0.0f) : 1.0f;
    }
    float FormRangeMult() const {
        int l = ActiveLevel(FLAME_FS_MASTERY);
        return l ? 1.0f + 0.025f * l + (l >= 5 ? 0.04f : 0.0f) : 1.0f;
    }
};

class Progression {
public:
    int points = 0;
    StyleUpgrades up[STYLE_COUNT];
    WaterForms water;                 // Water Breathing's eleven per-form levels
    FlameForms flame;                 // Flame Breathing's nine per-form levels
    StoneForms stone;                 // Stone Breathing's five per-form levels
    LoveForms love;                   // Love Breathing's six per-form levels
    FlameFightingStyles flameStyle;    // Flame-only passive Fighting Style loadout

    void Reset() {
        points = 0;
        for (int i = 0; i < STYLE_COUNT; i++) up[i] = StyleUpgrades{};
        water.Reset();                // every run begins at Level 1 in each form
        flame.Reset();
        stone.Reset();
        love.Reset();
        flameStyle.Reset();
    }

    void UnlockAll() {
        points = 100;
        for (int i = 0; i < STYLE_COUNT; i++) {
            up[i].power = 3;
            up[i].flow = 3;
            up[i].reach = 3;
            up[i].mastery = true;
        }
        water.MaxAll();
        flame.MaxAll();
        stone.MaxAll();
        love.MaxAll();
        flameStyle.MaxAll();
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

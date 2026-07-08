# Demon Slayer — Night of the Demon King

2D side-view action game in pure **C++ / raylib**. Endless night, drifting fog, swarming demons — survive 6 waves, grow stronger between them, then slay the Demon King **Muzan**. All sound effects are synthesized in code at startup; there are no asset files.

## Controls

| Key | Action |
|---|---|
| **G** | Summon **Giyu Tomioka**, the Water Hashira |
| **B** | Summon **Shinobu Kocho**, the Insect Hashira |
| **R** | Summon **Kyojuro Rengoku**, the Flame Hashira |
| A / D or ← / → | Move |
| W / ↑ / Space | Jump |
| **Left Shift** (hold) | **Crouch** — slower steps, lowered hurtbox (duck under crescents and fist orbs) |
| **J** (or X) | Sword combo — chain up to **4 hits** |
| **↑ + J** | Upward launcher slash |
| **↓ + J** (airborne) | Plunging strike with impact shockwave |
| **TAB** | **Upgrade menu** — spend points earned from waves |
| P | Pause · **F11** fullscreen · Enter confirm/restart |

## Breathing Styles

| Key | Style | Feel |
|---|---|---|
| **K** / 1 | **Water** | Flowing multi-hit dash, evasive, slows enemies (3.5s) |
| **L** / 2 | **Fire** | Explosive burst, heavy knockback, screen shake (8s) |
| **I** / 3 | **Stone** | Slow crushing slam; breaks brute armor & Muzan's guard (10s) |
| **O** / 4 | **Love** | Agile homing dash-dance that mends your wounds (6s) |
| **U** / 5 | **Serpent** | Weaving venomous flurry — poison ticks after the cuts (5s) |
| **H** / 6 | **Wind** | 360° launching sweep that unleashes a traveling tornado (9s) |
| **M** / 7 | **Mist** | Blink + brief invisibility; enemies lose you; next strike deals **2x** (7s) |

## Giyu Tomioka — the Water Hashira

Press **G** to summon Giyu as emergency support. He remains until the current wave or boss encounter ends, then withdraws. He hunts the demons threatening you first, dodges like a Hashira, announces and cycles real Water Breathing Forms — never the same one twice in a row: **First Form: Water Surface Slash**, **Second Form: Water Wheel**, **Fourth Form: Striking Tide**, **Sixth Form: Whirlpool**, and at maximum mastery the **Eleventh Form: Dead Calm**, which nullifies every attack and blood crescent around him.

- **He cannot slay Muzan for you** — the Demon King takes a fraction of his damage. Instead, Giyu's heavy hits **force openings** (Muzan drops into a vulnerable recover) and Dead Calm shields you through burst phases.
- **Ultimate guard:** when Akaza, Kokushibo, or Muzan enters a desperation ultimate, active Giyu immediately uses **Dead Calm: Ultimate Guard**. The stillness zone erases incoming hitboxes, projectiles, crescents, and blast rings until its shield health is spent.
- **If Giyu falls, he is gone for the rest of the run.** Choose his moments carefully.
- **Mastery persists across runs** (saved to `giyu_mastery.txt` next to the exe): he earns XP for kills, safe withdrawals, openings, and victories. Levels 1–5 grant better dodge AI, speed, more HP, stronger and faster forms, and stronger Dead Calm shield health.

## Shinobu Kocho — the Insect Hashira

Press **B** to summon Shinobu as fast support. She is more fragile than Giyu, but her Insect Breathing stings apply wisteria poison, interrupt swarms, and can force brief openings against Upper Moons. She uses **Butterfly: Caprice**, **Bee Sting**, **Dragonfly: Hexagon**, and **Centipede: Zigzag**; at maximum mastery she gains **Wisteria Bloom**, a poison cloud that lightly mends nearby wounds.

- **She cannot carry the boss fights alone** — Upper Moons and Muzan heavily resist ally damage, but her poison keeps pressure on while you dodge.
- **If Shinobu falls, she is gone for the rest of the run.**
- **Mastery persists across runs** (saved to `shinobu_mastery.txt` next to the exe): she earns XP from kills, openings, safe withdrawals, and victories.

## Kyojuro Rengoku — the Flame Hashira

Press **R** to summon Rengoku as explosive support. He is the burst-damage ally: **Unknowing Fire**, **Rising Scorching Sun**, **Blazing Universe**, and **Flame Tiger** launch demons and create openings through raw pressure. At maximum mastery he unlocks **Ninth Form: Rengoku**, a high-commitment charge that burns through the field.

- **He cannot slay the final bosses alone** — his Flame Hashira damage is resisted by Upper Moons and Muzan, but heavy forms can still force openings.
- **Ultimate guard:** when Akaza, Kokushibo, or Muzan enters a desperation ultimate, active Rengoku raises **Flaming Wall** in front of him. The wall burns away projectiles, hostile hitboxes, and blast rings until its shield health is spent.
- **If Rengoku falls, he is gone for the rest of the run.**
- **Mastery persists across runs** (saved to `rengoku_mastery.txt` next to the exe): he earns XP from kills, openings, safe withdrawals, and victories. Each level increases Flaming Wall shield health.

## Hashira summon rules

Hashira are strategic allies, not timed abilities. Once summoned, a Hashira stays until the current wave or boss encounter ends. Their HP persists across summons; at the end of each wave or encounter, every surviving Hashira recovers **20% of max HP** and active Hashira run off-screen before leaving.

If a Hashira falls during an encounter, that summon slot stays committed until the encounter ends. You cannot replace a fallen Hashira mid-wave or mid-boss fight.

| Encounter | Hashira slot limit |
|---|---:|
| Normal waves | 1 |
| Akaza | 1 |
| Douma | 2 |
| Kokushibo | 3 |
| Muzan | all surviving Hashira auto-join |

## Progression

Clearing a wave grants **+2 upgrade points** (and a heal). Press **TAB** any time in battle to open the upgrade menu. Every style has its own tree:

- **POWER** — +30% damage per level (3 levels, 1 pt each)
- **FLOW** — −18% cooldown per level (3 levels, 1 pt each)
- **REACH** — +20% range, duration & technique speed per level (3 levels, 1 pt each)
- **MASTERY** (2 pts) — a unique stronger/alternate move per style:
  Water dashes back through the line · Fire leaves burning ground · Stone launches a ground-splitting quake · Love gains five dashes + double lifesteal · Serpent ends in a venomous twin-fang bite · Wind releases twin cyclones · Mist leaves a lingering slowing cloud

## The gauntlet — one long night
The campaign is now **17 waves with three Upper Moons standing between you and Muzan**:

- **Waves 1–6 → Akaza (Upper Moon Three)** — falls from the sky into a cold, snow-swept arena. Fist barrages, heavy dash blow, ground shockwaves with fanned fist orbs, crater leaps. Rejoices (speeds up) below 40%.
- **Waves 7–11 → Douma (Upper Moon Two)** — a blizzard arena. Ice shard fans that **chill** you (slowed steps), **frozen lotus** eruptions blooming under your feet, and a freezing breath cone. He glides away when pressed — never stand still.
- **Waves 12–17 → Kokushibo (Upper Moon One)** — a violet moonfield. Moon-arc crescent barrages, flash-step cross slashes, and arena-wide **long slashes at chest height — CROUCH under them**. Six eyes see everything.
- **Then Muzan.** The Demon King, with his true form waiting below 27%.

Each fallen Moon grants points (+4/+5/+6) and a heal, then the horde resumes, harder: more demons per wave, faster spawns, tougher mixes, higher HP/damage, and quicker, more aggressive attacks the deeper you go.

**Desperation phases** — at their breaking point, the great demons refuse to lose (deliberately overwhelming; survive them however you can):

- **Akaza (40%)** — *"AKAZA CAN NO LONGER SENSE YOU"*: a blind spiral storm of air blasts fired in every direction for three full seconds.
- **Kokushibo (40%)** — *"I WILL NOT DIE"*: hundreds of moon crescents erupt from the ground across the entire battlefield.
- **Muzan (27%)** — *"THE DEMON KING REFUSES TO DIE. RUN."*: his flesh drinks the night, then erupts in a triple blast wave — sprint beyond its reach or be swallowed, and meet his true form in the aftermath.

Power scale is faithful to the anime: **Giyu < Akaza < Douma < Kokushibo < Muzan**. Giyu creates openings against all of them, but every Upper Moon actively hunts him and will win a prolonged duel.

## Muzan tips
Muzan takes **half damage** while active. He dashes, **teleports behind you**, hurls blood crescents, erupts **radial blade bursts**, and summons demons — and below 27% HP he tears away his shirt into his **true form**: veins pulsing, triple dashes, denser projectiles, no time to breathe. Every clean hit he lands sends you **flying**. After most attacks he is **VULNERABLE** (golden outline): full damage, **Fire deals 1.5×**. **Stone** shatters his guard for 4s. **Water** slows his dash chains. **Serpent** venom keeps ticking while you dodge. **Mist** makes even the Demon King hesitate.

## Build

### Windows — easiest (MSYS2)
```
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-raylib
g++ src/*.cpp -o demonslayer.exe -std=c++17 -O2 -lraylib -lglfw3 -lopengl32 -lgdi32 -lwinmm
```
(Or open the folder in VS Code and press Ctrl+Shift+B.)

### Any platform — CMake (downloads raylib automatically)
```
cmake -B build
cmake --build build --config Release
```

### Linux
```
g++ src/*.cpp -o demonslayer -std=c++17 -O2 -lraylib -lGL -lm -lpthread -ldl
```

### macOS
```
brew install raylib
c++ src/*.cpp -o demonslayer -std=c++17 -O2 $(pkg-config --libs --cflags raylib)
```

## Dev flags
`./demonslayer --demo` skips the title screen; `--akaza`, `--douma`, `--koku`, and `--boss` jump straight to Akaza / Douma / Kokushibo / Muzan. Add `--unlock-all` to start with every style track and mastery unlocked. During a run, press **F8** to unlock everything immediately.

## Code layout
```
src/config.h    tuning constants, shared helpers
src/styles.h    Breathing Style progression: points, tracks, mastery
src/effects.*   particles, screen shake, hitstop, damage numbers
src/combat.*    hitbox system (teams, attack ids)
src/audio.*     procedural SFX synth (hits, roars, gales, chimes, night wind)
src/player.*    movement, combos, 7 breathing styles, tornadoes/zones/afterimages
src/companion.* Giyu Tomioka: AI, Water Forms, permadeath, cross-run mastery
src/shinobu.*   Shinobu Kocho: Insect Forms, poison support, triage, mastery
src/rengoku.*   Kyojuro Rengoku: Flame Forms, burst support, mastery
src/enemy.*     demon tiers, swarm AI, armor break, poison, mist confusion
src/akaza.*     Akaza (Upper Moon Three): fist combos, shockwaves, leap craters
src/moons.*     Douma (ice, lotus, chill) and Kokushibo (moon arcs, long slashes)
src/boss.*      Muzan: dash, teleport, blade bursts, claws, summons, true form
src/game.*      states, waves, upgrade menu, atmosphere, HUD, combat resolution
src/main.cpp    entry point, render-texture scaling, fullscreen
```

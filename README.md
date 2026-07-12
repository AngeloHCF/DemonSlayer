# Demon Slayer — Night of the Demon King

2D side-view action game in pure **C++ / raylib**. Endless night, drifting fog, swarming demons — survive the gauntlet, grow stronger between waves, then hold the Demon King **Muzan** until sunrise. All sound effects are synthesized in code at startup; there are no asset files.

## Controls

| Key | Action |
|---|---|
| **G** | Summon **Giyu Tomioka**, the Water Hashira |
| **B** | Summon **Shinobu Kocho**, the Insect Hashira |
| **R** | Summon **Kyojuro Rengoku**, the Flame Hashira |
| **Y** | Summon **Gyomei Himejima**, the Stone Hashira |
| **T** | Summon **Tengen Uzui**, the Sound Hashira |
| **N** | Summon **Sanemi Shinazugawa**, the Wind Hashira |
| A / D or ← / → | Move |
| W / ↑ / Space | Jump |
| **Left Shift** (hold) | **Crouch** — slower steps, lowered hurtbox (duck under crescents and fist orbs) |
| **J** (or X) | Sword combo — chain up to **4 hits** |
| **↑ + J** | Upward launcher slash |
| **↓ + J** (airborne) | Plunging strike with impact shockwave |
| **TAB** | **Upgrade menu** — spend points earned from waves |
| P | Pause · **F8** dev invincible · **F11** fullscreen · Enter confirm/restart |

## Breathing Styles

| Key | Style | Feel |
|---|---|---|
| **K** / 1 | **Water** | Flowing multi-hit dash, evasive, slows enemies (3.5s) |
| **1-9** | **Flame** | Nine Flame Breathing forms with individual cooldowns, hitboxes, mastery levels, and passive Fighting Styles |
| **1-5** | **Stone** | Five heavy Stone Breathing forms built for strength, defense, and area control |
| **1-6** | **Love** | Six Love Breathing forms with flexible sword arcs, aerial pressure, and mending hits |
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

Press **R** to summon Rengoku as explosive support. He now uses the full Flame Breathing kit: **Unknowing Fire**, **Rising Scorching Sun**, **Blazing Universe**, **Blooming Flame Undulation**, **Flame Tiger**, **Solar Heat Haze**, **Inferno Wheel**, **Crimson Lotus Crest**, and at maximum mastery **Ninth Form: Rengoku**.

- **He cannot slay the final bosses alone** — his Flame Hashira damage is resisted by Upper Moons and Muzan, but heavy forms can still force openings.
- **Ultimate guard:** when Akaza, Kokushibo, or Muzan enters a desperation ultimate, active Rengoku uses **Blooming Flame Undulation**. The rotating fire guard burns away projectiles, hostile hitboxes, and blast rings until its shield health is spent.
- **If Rengoku falls, he is gone for the rest of the run.**
- **Mastery persists across runs** (saved to `rengoku_mastery.txt` next to the exe): he earns XP from kills, openings, safe withdrawals, and victories. Each level increases Blooming Flame Undulation guard durability.

## Gyomei Himejima — the Stone Hashira

Press **Y** to summon Gyomei as the frontline defender. He is the slowest Hashira, but has the highest health, strongest defense, and the heaviest raw hits. His chained axe and spiked flail cover close and medium range through **Serpentinite Bipolar**, **Upper Smash**, **Volcanic Rock: Rapid Conquest**, and the area-clearing ultimate **Arcs of Justice**.

- **Stone Guard:** when a major boss barrage threatens the player, Gyomei plants himself and whips the axe and flail through incoming projectiles. The guard shatters blood crescents, fist orbs, moon shards, hostile hitboxes, and blast rings until its shield durability is spent.
- **He is not fast.** He walks and attacks deliberately, with long cooldowns and commitment windows, but every impact creates shockwaves, dust, rock debris, heavy hitstop, and screen shake.
- **He cannot solo the final bosses** despite being the strongest Hashira in raw force. Upper Moons and Muzan resist ally damage, but Gyomei's blows break guard and force openings.
- **If Gyomei falls, he is gone for the rest of the run.**
- **Mastery persists across runs** (saved to `gyomei_mastery.txt` next to the exe): he earns XP from kills, openings, safe withdrawals, and victories. Each level improves his health, defense, damage, and Stone Guard shield.

## Tengen Uzui — the Sound Hashira

Press **T** to summon Tengen as the flashy pressure Hashira. He is one of the fastest allies, constantly dashing, weaving, and chaining attacks with dual Nichirin cleavers linked by a chain. His Sound Breathing kit includes a fast light combo, **Chain Cleave**, **First Form: Roar Rush**, **Rising Beat**, **Fourth Form: Constant Resounding Slashes**, and the large-area ultimate **Musical Score: Flashy Finale**.

- **Explosive Deflection:** when a major boss attack threatens the player, Tengen spins his chained blades into an aggressive defensive zone. Incoming crescents, fist orbs, moon shards, hostile hitboxes, and blast rings detonate on contact until the deflection durability is depleted.
- **He is built for speed, not stillness.** His AI prioritizes clustered enemies, keeps moving around bosses, dodges heavy attacks, and uses wide sweeps and explosions to control space.
- **He cannot solo the final bosses.** Upper Moons and Muzan resist ally damage, but Tengen's fast pressure can still force openings and clear swarms around the player.
- **If Tengen falls, he is gone for the rest of the run.**
- **Mastery persists across runs** (saved to `tengen_mastery.txt` next to the exe): he earns XP from kills, openings, safe withdrawals, and victories. Each level improves speed, health, damage, dodge reliability, and Explosive Deflection durability.

## Sanemi Shinazugawa â€” the Wind Hashira

Press **N** to summon Sanemi as the most aggressive Hashira. He stays close to targets, dashes into danger, and chains fast sword pressure into wide Wind Breathing techniques: **Wind Hashira Combo**, **First Form: Dust Whirlwind Cutter**, **Seventh Form: Gale, Sudden Gusts**, **Fourth Form: Rising Dust Storm**, **Fifth Form: Cold Mountain Wind**, and at maximum mastery **Ninth Form: Idaten Typhoon**.

- **Wind Barrier:** when a major boss attack threatens the field, Sanemi rapidly spins his sword into a violent tornado around himself. The barrier shreds blood crescents, fist orbs, moon shards, hostile hitboxes, and blast rings while also cutting enemies caught inside until its durability is depleted.
- **He is pure pressure.** His AI prioritizes staying close, pursuing bosses, interrupting windups, and sweeping crowds with traveling wind blades, dust clouds, slash trails, and tornado hits.
- **He cannot solo the final bosses.** Upper Moons and Muzan resist ally damage, but Sanemi's relentless hits slow them, force openings, and erase demon swarms.
- **If Sanemi falls, he is gone for the rest of the run.**
- **Mastery persists across runs** (saved to `sanemi_mastery.txt` next to the exe): he earns XP from kills, openings, safe withdrawals, and victories. Each level improves speed, health, damage, dodge reliability, and Wind Barrier durability.

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

Clearing a wave grants **+2 upgrade points** (and a heal). Press **TAB** any time in battle to open the upgrade menu. Water, Flame, Stone, and Love use form shops where each form levels 1-5 independently; Flame also has a second **Fighting Styles** tab for passive loadouts. Other styles use a shared tree:

### Love Breathing Forms

Love uses numbered forms 1-6. Every form has its own cooldown, hitboxes, visual effects, upgrade level, and mending strength.

- **First Form: Shivers of First Love** - a fast, sweeping slash that strikes with tremendous speed.
- **Second Form: Love Pangs** - multiple whip-like slashes unleashed rapidly across a wide area.
- **Third Form: Catlove Shower** - curved attacks that strike enemies while protecting through the flurry.
- **Fifth Form: Swaying Love, Wildclaw** - a forward dash with a chaotic barrage of long-range slashes.
- **Sixth Form: Cat-Legged Winds of Love** - an acrobatic spinning whirlwind of flexible sword strikes.
- **Final Form: Ripping Kitty Tempest of Love** - a high-speed aerial storm of slashes from every direction.

### Stone Breathing Forms

Stone uses numbered forms 1-5. Every form has its own cooldown, hitboxes, visual effects, and upgrade level.

- **First Form: Serpentinite Bipolar** - chained front-and-back pressure for midrange control.
- **Second Form: Upper Smash** - a slow, crushing launcher that punishes brutes and bosses.
- **Third Form: Stone Skin** - the primary defensive form. The player plants their stance, gains shield durability, shatters projectiles, and blocks or reduces incoming damage while the shield holds.
- **Fourth Form: Volcanic Rock, Rapid Conquest** - repeated advancing impacts for crowd lockdown.
- **Fifth Form: Arcs of Justice** - a slow ultimate sequence of sweeping arcs with a massive finishing shockwave.

### Flame Fighting Styles

Flame players can unlock and upgrade five passive Fighting Styles. Only one Fighting Style can be equipped at a time; upgrading a style also equips it. Each style has 5 levels, costs 1/2/3/4/5 points to raise, and gains a unique specialization bonus at Level 5.

- **Offensive Style** - higher attack and Flame damage, faster combo speed, and critical strike chance. Level 5: **Blazing Criticals** hit harder and happen more often.
- **Defensive Style** - lower incoming damage, stronger Blooming Flame Undulation guard durability, and reduced knockback. Level 5: **Phoenix Guard** rekindles the flame guard once instead of breaking immediately.
- **Swift Style** - faster movement, longer Flame dash travel, faster attacks/forms, and shorter recovery. Level 5: **Afterimage Step** adds extra safety during Flame form startups.
- **Endurance Style** - higher max health, higher Flame stamina, faster stamina recovery, and stronger crowd-control/chill resistance. Level 5: **Unyielding** consumes stamina to survive a lethal hit at 1 HP.
- **Mastery Style** - lower Flame form cooldowns, cheaper stamina costs, and slight form speed/range/damage improvements. Level 5: **Form Chain** shaves cooldown time from the other Flame forms whenever one is cast.

- **POWER** — +30% damage per level (3 levels, 1 pt each)
- **FLOW** — −18% cooldown per level (3 levels, 1 pt each)
- **REACH** — +20% range, duration & technique speed per level (3 levels, 1 pt each)
- **MASTERY** (2 pts) — a unique stronger/alternate move per style:
  Serpent ends in a venomous twin-fang bite · Wind releases twin cyclones · Mist leaves a lingering slowing cloud

Flame's Fourth Form, **Blooming Flame Undulation**, is the defensive form built to burn away Akaza, Kokushibo, and Muzan attacks.

## The gauntlet — one long night
The campaign is now **17 waves with three Upper Moons standing between you and Muzan**:

- **Waves 1–6 → Akaza (Upper Moon Three)** — falls from the sky into a cold, snow-swept arena. Fist barrages, heavy dash blow, ground shockwaves with fanned fist orbs, crater leaps. Rejoices (speeds up) below 40%.
- **Waves 7–11 → Douma (Upper Moon Two)** — a blizzard arena. Ice shard fans that **chill** you (slowed steps), **frozen lotus** eruptions blooming under your feet, and a freezing breath cone. He glides away when pressed — never stand still.
- **Waves 12–17 → Kokushibo (Upper Moon One)** — a violet moonfield that darkens the instant he stirs, beneath a deep, dissonant drone. The strongest Upper Moon walks toward you — slow, silent, unbothered — then *explodes* into layered crescent curtains (weave the seams), chained arena-wide **long slashes: CROUCH the chest cut, JUMP the low sweep**, flash-step cross slashes, and point-blank sword combos. He will occasionally **stop, stare you down, then vanish and reappear at your side with an instant slash** — the vanish is your only warning, so don't panic-dodge early. Six eyes see everything; his recovery is your only door.
- **Then Muzan.** The Demon King becomes a five-minute survival battle: stall him until sunrise, because blade damage cannot finish him.

Each fallen Moon grants points (+4/+5/+6) and a heal, then the horde resumes, harder: more demons per wave, faster spawns, tougher mixes, higher HP/damage, and quicker, more aggressive attacks the deeper you go.

**Desperation phases** — at their breaking point, the great demons refuse to lose (deliberately overwhelming; survive them however you can):

- **Akaza (40%)** — *"AKAZA CAN NO LONGER SENSE YOU"*: a blind spiral storm of air blasts fired in every direction for three full seconds.
- **Kokushibo — three phases** — the blade quickens at **66%**. At **33%** he refuses the grave: *"I WILL NOT DIE"* — a cinematic transformation with an enormous eruption of moon crescents, then he returns **faster and more aggressive**, periodically flooding the arena with sky-and-earth crescent storms (**Fourteenth Form: Catastrophe, Tenman Crescent Moons**).
- **Muzan** — his desperation is no longer a single threshold. As dawn approaches he repeatedly triggers violent blood-whip eruptions, arena-wide blasts, and invulnerable transformations.

Power scale is faithful to the anime: **Giyu < Akaza < Douma < Kokushibo < Muzan**. Giyu creates openings against all of them, but every Upper Moon actively hunts him and will win a prolonged duel.

## Muzan tips
Muzan is now a **sunrise survival fight**. The top timer counts down from **5:00**; victory happens only when dawn arrives. Damage does not kill him, but constant pressure suppresses his regeneration, slows his tempo, and can force brief openings. As time passes he escalates through phases with faster movement, longer combos, multi-direction blood whips, falling crescents, arena-wide attacks, repeated ultimates, and heavy knockback that can send you flying across the arena. Hashira automatically join this fight, but Muzan actively targets them too.

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
`./demonslayer --demo` skips the title screen; `--akaza`, `--douma`, `--koku`, and `--boss` jump straight to Akaza / Douma / Kokushibo / Muzan. Add `--unlock-all` to start with every style track, Water form, Flame form, Stone form, Flame Fighting Style, and mastery unlocked. In the upgrade shop, **Q / E** swaps the dev shop style. During a run, press **F8** to toggle developer invincibility.

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
src/gyomei.*    Gyomei Himejima: Stone Forms, chained axe/flail, Stone Guard
src/tengen.*    Tengen Uzui: Sound Forms, chained cleavers, Explosive Deflection
src/sanemi.*    Sanemi Shinazugawa: Wind Forms, typhoons, Wind Barrier
src/enemy.*     demon tiers, swarm AI, armor break, poison, mist confusion
src/akaza.*     Akaza (Upper Moon Three): fist combos, shockwaves, leap craters
src/moons.*     Douma (ice, lotus, chill) and Kokushibo (moon arcs, long slashes)
src/boss.*      Muzan: survival timer pressure, blood whips, arena attacks, sunrise finale
src/game.*      states, waves, upgrade menu, atmosphere, HUD, combat resolution
src/main.cpp    entry point, render-texture scaling, fullscreen
```

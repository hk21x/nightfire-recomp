# 007: NightFire - achievements

Defined in `src/common/achievement_set.cpp` (RetroAchievements syntax on default.xbe's memory), kept in
`achievements.toml` beside the exe. The memory map and how it was found: PROGRESS.md (2026-10-29) and FINDINGS.md.

| Id | Title | Points | When |
|---|---|---|---|
| paris_prelude ... equinox | each mission by name | 10 (Equinox 25) | the mission completed (on foot or driving) |
| gold_standard | Gold Standard | 10 | a gold medal on any mission |
| platinum_standard | Platinum Standard | 25 | a platinum medal on any mission |
| golden_service | Golden Service | 50 | a completion with every mission's best at gold |
| platinum_service | Platinum Service | 100 | the same at platinum |
| double_o | Double-O Status | 10 | any mission completed on 00 Agent |
| licensed_to_thrill | Licensed to Thrill | 50 | Equinox completed on 00 Agent |
| bond_moves | Shaken, Not Stirred | 10 | every Bond Move found in a mission (those with any) |

How they are told: every completion, driving ones too, is scored in default.xbe on its Results page (page
0x40000036 at 0x25F388, mission mode 6 at 0x17E544, the level done at 0x1F65C8). A codename's profile is restored all at
once and never makes that state, so loading a finished save awards nothing; an achievement for best medals over all
missions comes at the next completed mission. Driving.xbe shows the same list and awards none.

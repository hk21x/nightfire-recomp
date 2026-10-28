# 007: NightFire (original Xbox) - static recompilation

Goal: a native Windows port built with the XboxXBE-Recomp toolkit in `sdk/` (a git submodule on the fork's `main`).
Read `sdk/README.md`, `sdk/harness/README.md`, `LESSONS.md`, `PROGRESS.md` and `FINDINGS.md` at the start of every
session. LESSONS.md is what the PGR2, EoN, PGR1 and Forza ports learned; check it before chasing a new problem.

## Environment
- Headless Linux VM: build the Linux tools, regenerate code, run the game in the headless harness. The real Windows
  exe is built and play-tested on Windows separately (Harry).
- Game files (your own disc) are in `assets/`. Never commit, upload or paste them, or generated code.
  `assets/`, `generated/`, `build/`, `.local/` and `captures/` are git-ignored; keep it that way.
- Signatures in `sdk/xdk/*/signatures.toml` are hashes and lengths only. Never store library bytes.

## Commands
`scripts/*.sh` are wrappers for the shared scripts in `sdk/scripts`; this game's values are in `scripts/game.env`.
- `scripts/linux-tools.sh`   configure for Linux, build `xbe_symbols` and `xbe_recomp`
- `build/linux/xbe_symbols assets/default.xbe sdk/xdk --out config/game.toml`   library versions, XDK matches,
  rewrites config/game.toml (scan output only, never edited; hand entries go in config/nightfire.toml, which includes it)
- `build/linux/xbe_symbols assets/Driving.xbe sdk/xdk --out config/driving_game.toml --name driving`   the same for
  the driving levels' xbe; its hand entries go in config/driving.toml
- Signing: `build/linux/xbe_symbols --sign config/nightfire.toml config/driving.toml --out sdk/xdk/4831/signatures.toml`,
  then both rescans must reproduce their scan files. 4831 is this game's XDK alone so far, but a change to
  xbe_symbols itself must leave every other game's scan unchanged (PGR2, PGR1, EoN's two, Forza: rescan each with its
  own `sdk/xdk` and `cmp` against its committed scan file)
- `scripts/recompile.sh`   regenerate `generated/game`; `cmake --build build/linux --target recompile_NightfireDriving`
  regenerates `generated/driving`
- `scripts/harness.sh [secs] [pad] [ENV=..]`   build + run default.xbe headless (`REF=N` CPU frames into `captures/`,
  `KEEP_HDD=1` keeps the emulated disk, `snap`/`memdump`/`poke` files in the run folder). Driving.xbe runs headless
  only from launch data default.xbe wrote (EoN's way, LESSONS.md "Two xbes")
- `scripts/smoke.sh`   default.xbe's title -> START -> Driving.xbe's Paris mission headless (about 4 minutes, two
  runs: scripts/pad/smoke.txt then scripts/pad/paris.txt); must print SMOKE OK before every fast-forward of `main`
- `scripts/wincheck.sh`   Windows-only sources against mingw headers; `scripts/shadercheck.sh`   generated HLSL with DXC
- Ghidra: `python3 sdk/tools/symbols/xbe2pe.py config/nightfire.toml .local/game.exe` (and `config/driving.toml
  .local/driving.exe`), both imported into `~/ghidra-projects/nightfire`; then
  `scripts/ghidra.sh Decomp|Disasm|FindOperand|Callers|DumpRange|PushBufferScan|DeviceRefs ...` reads game.exe,
  `GHIDRA_PROGRAM=driving.exe scripts/ghidra.sh ...` reads Driving.xbe's

## The game
- PAL disc, title 0x45410026 (EA), version 2. Two executables: `default.xbe` (the on-foot levels; target and exe
  `Nightfire`, hooks in `src/game`) and `Driving.xbe` (the driving levels, data in `driving/`; target
  `NightfireDriving`, hooks in `src/driving`). On-foot data is in `eurocom/filesys.d00`-`d10` (Eurocom's engine).
  The game switches between the two with XLaunchNewImage("driving.xbe" / "default.xbe"); the runtime relaunches into
  the other exe (`GameInfo::images`, matched lower case). On disc the second xbe is `Driving.xbe`: Linux paths are case
  sensitive, so CMake and the launcher name it that way.
- XDK 4831 for every library (D3D8, D3DX8, XGRAPHC, DSOUND, XAPILIB, LIBCMT; XMV in default.xbe, LIBCPMT in
  Driving.xbe). No XNet/XOnline libraries. The SDK had no 4831 signatures: `sdk/xdk/4831` was seeded from 5659 and is
  the first task (Plan 1).

## Plan
1. XDK 4831 (SDK branch `nightfire`): name the XDK functions both xbes call and sign them into `sdk/xdk/4831`; re-derive
   every `layout.toml` key from this game's code, citing the NightFire instruction (the seeded file still has 5659's
   values and comments). 4831 sits between 3948 and 5659: for each behaviour 3948 does differently (state numbering,
   Present vs Swap, frame surfaces inside the device, GetBackBuffer out-pointers, vertex constant register + 96, the
   present parameters' end, see LESSONS.md) find which side 4831 is on and set the layout key.
2. Check push buffer use (CreatePushBuffer / Begin/EndPushBuffer / RunPushBuffer and fixups, hand-written commands):
   search for the functions' callers, not only NV2A headers, and report it to Harry at once.
3. Boot: resolve trapping stubs one at a time. Then menus, the first mission, a driving mission through XLaunchNewImage,
   smoke test.
4. Windows parity, then PC features (game hooks), achievements, the launcher and a release.

## Working rules
- One problem at a time. After each meaningful step, a dated entry at the top of PROGRESS.md's log (what changed,
  evidence, next step), its Status block updated, and a commit.
- Game fixes go in `src/game` / `src/driving` (X86_FUNC overrides, [[hook]]/[[function]]/[[switch]] in the hand
  configs), not in generated code. SDK changes go in `sdk/` as their own commits: the task branch `nightfire` of the
  fork, merged into its `main` only after every game's smoke test passes (PGR2, PGR1 plus its frame check, EoN, and
  this one once it has one); fetch + merge (never rebase), publish the SDK first, then this repo's pointer.
- Game code registers from `GameInfo::init`, never a static initializer (one never ran in a Windows exe).
- Diagnostics a player's log must show are report level (LOGR) or errors: release logs drop info lines.
- If the same problem survives 3 genuinely different attempts, stop: write up what was tried in PROGRESS.md, commit,
  and say so.
- One branch per task; when it ends and everything builds, fast-forward `main` (`git merge --ff-only`), push both.
- Never force-push. Never rewrite history.

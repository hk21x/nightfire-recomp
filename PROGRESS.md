# 007: NightFire recomp - progress log

Newest first. Each entry: date, what changed, what was learned, what's next.

## Status
- Milestone: 0 - set up. default.xbe recompiles and starts headless; it stops in XAPI's USB init (XInitDevices
  unnamed). Driving.xbe recompiles, not yet run
- XDK: 4831 for every library, new to the SDK (`sdk/xdk/4831`, seeded from 5659). default.xbe 53 of 251 functions,
  3 of 13 data; Driving.xbe 52 of 251, 2 of 13. Layout: 5659's values, none verified for 4831 yet
- Push buffers: preliminary, no NV2A command stores in game code; CreatePushBuffer / RunPushBuffer not yet identified
  (to settle once the D3D functions are named)
- SDK: task branch `nightfire` of the fork (from main 0ade542)
- Smoke: none yet (game.env's pad and thresholds are placeholders)

## Disc
- PAL, title 0x45410026 (EA), certificate region 0x4, version 2. Ghidra: `~/ghidra-projects/nightfire` (game.exe,
  driving.exe)
- default.xbe SHA-1 796222e048be3595c323b4c6d9795107132dc4ca, built 2002-10-20. Libraries XAPILIB, D3D8, D3DX8,
  XGRAPHC, DSOUND, XBOXKRNL, LIBCMT, XMV, all 1.0.4831. Sections .text 0x11000-0x10046C, D3D, D3DX, XGRPH, DSOUND, XMV,
  XPP 0x157200-0x15D150, .rdata, .data, DOLBY
- Driving.xbe SHA-1 e0c41209d8979fcd03ac3d656be78759006023cb, built 2002-10-18. Libraries XAPILIB, D3DX8, D3D8,
  XGRAPHC, XBOXKRNL, LIBCPMT, LIBCMT, DSOUND, all 1.0.4831 (no XMV)
- Data: `eurocom/filesys.d00`-`d10` and `eurocom/25_fps` (on foot), `driving/*.viv`, `.mus`, `.spe` (driving)

## Log

### 2026-10-27 - Repository set up; first scans and first boot attempt (main; sdk nightfire)

Chosen from the two discs dumped today: NightFire is fully extracted and has the same two-xbe structure as EoN, and
its console game differs from the Gearbox PC port. (True Crime: New York City's extract stopped before default.xbe;
its default.xbe from the .iso is XDK 5849 with LTCG D3D8, 116 of 278 functions on a first scan: a later candidate.)

Made from `sdk/templates/game` with EoN's two-xbe wiring: targets `Nightfire` (default.xbe, src/game, config/
nightfire.toml) and `NightfireDriving` (Driving.xbe, src/driving, config/driving.toml), `GameInfo::images`, both exes
in launcher/game.json and release.bat, the workflow title quoted. LESSONS.md collects what PGR2, EoN, PGR1 and Forza
learned.

SDK (branch `nightfire`):
- `xdk/4831`: no signatures existed. Trial scans seeded from each build: 3948 23 functions, 5659 54, 5849 55 (5849
  has more names, the same hits); seeded from 5659 as PGR1 was. layout.toml is 5659's, marked unverified.
- xbe_symbols: default.xbe's scan named 0x15B5D5 XInputGetCapabilities by constants (0.60) though it pops 4 bytes,
  not 8, and wrote an `[abi]` placeholder the recompiler refuses. 0x15B5D5 is an XPP device type's callback (the
  descriptor 0x15731C's +8), not XInputGetCapabilities. Now, in an image without LTCG, a constants match whose ret
  size or entry registers differ is not taken. PGR2, PGR1, EoN (both) and Forza rescans are byte-identical.
- ghidra.sh: `GHIDRA_PROGRAM=driving.exe` reads a second xbe's program.

First headless runs (default.xbe, none.txt):
1. 0.024 s: indirect call to 0x157334 from 0x15757B, which walks XPP's device type table 0x157214..0x157220
   (descriptors 0x1572D8, 0x1572F0, 0x15731C; +4 init, +8 and +0xC callbacks). Only 0x157334 had no function:
   `[functions]` in config/nightfire.toml.
2. Segfault at guest 0xFED00000, the USB host controller's registers: XInitDevices is 0x15753D (allocates the 0xB4
   byte host object, runs the table's inits, starts the controller; stdcall, 2 arguments) and was unnamed, so the
   library's driver ran. The host's XInitDevices needs XPP_GamepadType and the XPP_Current/Changed/Previous layout
   keys, which are still 5659's: naming it belongs to the 4831 pass.

Push buffers (sdk/scripts/ghidra PushBufferScan): all 226 NV2A method stores are in library code, none in the game's
.text, so the game writes no commands by hand. The 0x20001 store in D3D 0x109790 is the lazy state flush's method
value, not a push buffer resource. Whether the game records or runs push buffers stays open until D3D is named.

Recompile: default.xbe 4640 functions (357 direct, 740 by gap fill, 3925 overlapping decodes); the 31 "code
addresses not made functions" at 0x40290-0x403A0 are NV2A method headers in D3D immediates, not code.

Next: Plan 1, the 4831 pass. Name D3D, DSOUND, XPP (XInitDevices 0x15753D first) in default.xbe against PGR1's 3948
and PGR2's 5659 in Ghidra, re-derive the layout, sign, rescan both xbes; then settle push buffers and boot.

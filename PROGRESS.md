# 007: NightFire recomp - progress log

Newest first. Each entry: date, what changed, what was learned, what's next.

## Status
- Milestone: 2 - gameplay headless. default.xbe: language select, the XMV intro movies, the title; START hands over to
  Driving.xbe (EA's front end and driving levels), which plays the Paris intro movie and the Paris mission (Bond
  sniping from the van; with no input the mission fails to its menu): 200 s, 8280 frames, no traps
- XDK: 4831 named in the SDK (branch nightfire): default.xbe 137 of 252 functions (the rest not linked), Driving.xbe
  112 (4 of them Driving.xbe's own, by hand); layout proven from NightFire's code (5659 behaviour, own offsets)
- Push buffers: none (no push buffer function linked, game code never touches the push pointer)
- Region: PAL (GameInfo::pal_console); the launch data carries the video standard into Driving.xbe
- SDK: task branch `nightfire` (pushed; the build takes it from there). On it: PGR1 and PGR2 smoke OK (2026-10-28).
  EoN's smoke fails the same with and without it, in EoN's own tree at its own SDK (stuck on the intro movie
  in1_palw.xmv): not merged into fork main until EoN's gate works again; PGR1's frame check then too
- GitHub: remote origin git@github.com:hk21x/nightfire-recomp.git set; the private repository is to be made by Harry
- Smoke: scripts/smoke.sh: SMOKE OK (2026-10-28): title, START, relaunch, Driving.xbe's Paris mission (146 draws a
  frame at the end), about 4 minutes; it fails without START (no relaunch)
- Open: the Mission Failed menu panel's texture is noise in the reference renderer (to check on Windows first)

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

### 2026-10-28 - default.xbe trapped at 0x11234A: an instruction past the raw data (main; sdk nightfire)

Harry's next run (with his saves, so default.xbe stays in its menus) trapped after 39 s: "undecodable instruction"
at 0x11234A, from the game's 0xE83A8 (D3DX 0x111FC0, a matrix inverse). The D3DX section's raw data ends at
0x11234C, its virtual size at 0x112350: the function's last instruction, ret 0xC (C2 0C 00), has its last byte in
the zero filled tail the loader maps. sdk xbe_recomp: Decode pads with the tail's zeros, and an instruction that
starts in code may run into its own section's tail. Forza's FZFULLUI had the same trap (0x3A9AEA); PGR2, PGR1 and
EoN had none, so their code does not change. Smoke OK.

### 2026-10-28 - Menu panels: Driving.xbe's SetPalette (main; sdk nightfire)

Harry's rebuild: no crash; the scope shows the view inside it now (dim) but not yet as it should, and the menu
panels (Paused, Mission Failed) were black and white noise, also headless. Their textures are P8 (format 0x0B).
0x1669E0 stores a palette in the device's slot [+0xB48 + stage * 4] and pushes 0x41B20 + stage * 0x40 itself (EoN's
QFE 4 SetPalette does the same): D3DDevice_SetPalette, unnamed, so the host never had a palette (and logged
"MakeSpace from 00166A4E"). Named: the Mission Failed panel draws right headless. Smoke OK.

Next: the scope on Windows after this build (the readback of 2026-10-28 made the view show; brightness and the
noise pattern still to compare with the console).

### 2026-10-28 - Leaving Paris crashed; the sniper scope black (main; sdk nightfire)

Harry's Windows run reached the end of the first mission, then crashed: an access violation in the heap's free
(0x110BA6 from RtlFreeHeap) through D3DResource_Release 0x169230 from the game's teardown 0xE8600. Reproduced headless
by quitting a failed mission to the menu (render thread on or off). gdb on the releases: the object is the host's
depth buffer (header 0x00050001: one reference, no internal one), and library code at 0x16D23D had dropped the
device's hold: the frame buffer teardown 0x16D180 inside Driving.xbe's D3DDevice_Reset 0x166230, unnamed, so the
library's Reset ran on the host's buffers. Named (PGR2's 0x1C38D0 at 0.96): the host Reset runs, and the quit now
relaunches into default.xbe, which comes up on the main menu. Smoke OK.

Visual: the sniper scope is black (both renderers). Probes and traces at the scope's centre: draw 158 multiplies
the view by a texture that is near black, the 128x128 target 0x80F07000, a decaying accumulator a black quad dims
each frame and 16384 points (128 x 128) add to. The points' colour stream is the memory of the other 128x128
target 0x80F17000, which the pass before drew from the back buffer: the console reads those texels as vertex
colours; here the GPU's pixels never reached guest memory, the points added zeros and the accumulator stayed black.
sdk: before a draw each vertex stream's data is read back when the GPU drew it, and the D3D12/D3D11 read-backs
write 32 bit swizzled targets in Morton order (they skipped every swizzled one). Headless cannot show it (the null
backend has no GPU targets); wincheck and smoke OK. The icon-atlas frame in Harry's captures runs the same passes.
Ruled out on the way: linear texture coordinates as 0..1 (experiment: no change, the HUD text broke).

Next: Harry rebuilds and checks the scope, the atlas frame and the end of the mission.

### 2026-10-28 - Windows: the first mission was black; the gamma ramp (main; sdk nightfire)

Harry's first Windows build (D3D12, RX 7900 XT) boots, plays the movies and the title, relaunches into Driving.xbe
and then shows black in the first mission. His F3 capture (.local/win) has the frame drawn right (final.bmp: the
van's sniper view), so the picture was lost after the draws. The device's gamma ramps in his memory dump are zeros,
and the game's own base ramp at 0x23FC08 too: Driving.xbe reads the current ramp once with the library's
D3DDevice_GetGammaRamp 0x165E70 (copies [device + 0x259C + slot * 0x300]) and every fade is that ramp scaled
(0xE7740: base * scale + 0.5, then SetGammaRamp). The host never put a ramp in the device: its SetGammaRamp goes to
the GPU only, and the console's linear ramp was never there, so every fade was black. The reference renderer has no
gamma, so headless never showed it. sdk: CreateDevice writes the linear ramp into both slots and SetGammaRamp keeps
them current. Headless the saved base is now linear (0, 32 ... 224); smoke OK.

Next: Harry rebuilds (build.bat updates the sdk) and checks the mission shows.

### 2026-10-28 - Ready for a first Windows build (main; sdk nightfire)

- Gate on the branch: PGR2 smoke OK (2009 draws a frame), PGR1 smoke OK. EoN smoke fails at 1 draw a frame (the
  intro movie in1_palw.xmv never ends) on fork main's SDK, on its own pinned SDK 9e5900b and merged with this branch
  alike, in /home/harry/eon-soak and in EoN's own tree: an EoN problem of its own, so fork main is not moved yet.
- Windows checks: scripts/wincheck.sh clean; scripts/shadercheck.sh compiles all 41 shaders the two xbes made (4 in
  default.xbe's boot, 37 in the Paris level), 0 failed.
- build.bat is EoN's (both exes); the sdk submodule is the pushed nightfire branch's commit, so a clone builds
  without fork main. The launcher builds from fork main and cannot build this game until the merge.

### 2026-10-28 - Pad scripts and a smoke test across the two xbes (main; sdk nightfire)

PAL boot (frames every 120, no input): language select 120..600 (it moves on by itself), the EA and MGM movies to
1200, the title from 1440, "Press START" from about 1560 (24 draws a frame). scripts/pad/smoke.txt: A at 300, START
at 2000; the relaunch follows at about 34 s.

Driving.xbe, Paris (scripts/pad/paris.txt, vblanks from its own start): the sniper tutorial from about 1500 passes
on RT every 40 vblanks with auto-aim; its closing movie leads (about 5100) into the Aston Martin's chase. RT
accelerates; A brakes and reverses (A+RT reversed into the camera). Unsteered, the car meets the barriers and the
mission fails to its menu at about 7200 (A there replays the tutorial). Movie decoding makes frame times vary
between runs, so presses after a movie are timed loosely.

sdk: smoke.sh takes a second xbe (SMOKE_NEXT_GEN/NAME/PAD/SECS): the first run must relaunch, the next xbe runs on
its disk and launch data and gets the drawing checks; SMOKE_SECS. NightFire: SMOKE OK (title 24 draws a frame,
Paris 146); without START it fails ("no relaunch"); PGR1's smoke unchanged on the branch.

Next: steering through the chase (the harness's cmd file takes presses live), PGR2 / EoN smoke and PGR1's frame
check, then the branch into fork main and a Windows build.

### 2026-10-27 - The Paris mission plays headless (main; sdk nightfire)

After the intro movie Driving.xbe stopped three times, each an XDK function only it links:
1. D3D 0x16B160 read the current pixel shader's definition ([[pDevice]+0x370]+8, null with hosted shaders):
   D3DDevice_SetPixelShaderConstant (its opening is PGR2's 0x1C27C0 instruction for instruction, ret 0xC).
2. DSOUND 0x17BE1B -> 0x17BAF4 copies a DSI3DL2LISTENER: IDirectSound_SetI3DL2Listener.
3. 73 s in, a 0x15000 byte contiguous allocation failed with 64156 KB in use. XBE_MEM_LOG=2 (new, sdk) summed the
   live blocks by caller: the game's own 36 MB heap (0x10E818), its D3D resources (454 blocks, 6.9 MB), its render
   targets, and 10 MB the host made: three swap buffers and the depth buffer, as a console makes, plus a copy target
   (1.2 MB) and the DrawVerticesUP ring (4 MB) no console has. sdk: physical memory is mapped to 128 MB as on a
   development kit; the title's allocator and statistics keep the console's 64 MB and the host's own buffers (copy
   target, UP ring, scratch push) come from the part past it. Then 200 s of the mission without a trap. PGR1's
   smoke passes on the branch.

Names signed into sdk/xdk/4831 from both hand configs (.local/resign.sh); both rescans reproduce every hand name.

Next: a pad script through the mission (and the front end: START at the title went straight into mission 1 with the
"Default" profile), a smoke test across the two exes, then PGR2 / EoN smoke and PGR1's frame check before the
branch goes into fork main; a Windows build for Harry.

### 2026-10-27 - XDK 4831 named; title screen; Driving.xbe reached and plays its intro movie (main; sdk nightfire)

Named with three parallel read-only passes (D3D functions and data, the D3D layout, DSOUND), each against PGR2's 5659
bodies (instruction sequences with numbers masked, callees followed) and checked on NightFire's own instructions;
XAPI/XPP by hand. Evidence is in config/nightfire.toml, sdk/xdk/4831/layout.toml and .local/match/result_*.txt.
- D3D: 51 functions by body, render state slot and push methods (byte-identical setters told apart by slot); the
  data (pDevice 0x1117C0, RenderState 0x1119D0, PresentInterval 0x111BCC, ...) found by both D3D passes alike.
- Layout: 5659's way on every 3948/5659 difference (state numbering, Swap, frame buffer pointers, present interval
  global, D3DPRESENT_PARAMETERS to +0x40, D3DVBLANKDATA); device size 0x2C30, miniport at +0x23C0. Spot-checked.
- DSOUND: 41 functions by their chains to the leaf (4831 has no .XTLID). The scan had SetMixBins/SetMixBinVolumes
  swapped. Mix bins as 5659, headroom as 3948.
- Signed into sdk/xdk/4831 from default.xbe (137 of 252); both rescans reproduce the hand names. Driving.xbe's 100
  shared library names compared with default.xbe's by call chain (all 0.97 or more).

Boot, one stop at a time:
1. XInitDevices unnamed: the USB driver touched 0xFED00000. Named (0x15753D) with XInputOpen/Close, XDEVICE_TYPE_GAMEPAD.
2. Direct3D_CreateDevice unnamed: 0xFD001804. With the D3D names: 120 s without a trap, the title screen (REF frames).
3. START launches driving.xbe: EA's front end and driving levels are that xbe, so it is on the main path. Its XPP
   table init 0x183BF4 needed `[functions]` as default.xbe's did; DirectSoundCreateBuffer 0x17C2A0 (default.xbe does
   not link it) and IDirectSoundBuffer_SetFilter 0x17B5E0 (4831's Driving.xbe DSOUND has it, default.xbe's not) named
   by hand after APU faults.
4. Driving.xbe then hung after 60 frames in a stream loop (0x14C800): EA's file layer opened
   `NTSC\eng\paris_intro.mad`, which is not on the disc, and a failed open leaves the stream busy for good. The movies
   are in driving/misc.viv under `pal\<language>\`: the video standard default.xbe passes in the launch data
   (offset 0xE1C) was NTSC. `GameInfo::pal_console` (as EoN): default.xbe then shows the language select and the
   three XMV intro movies, which needed D3DDevice_GetRasterStatus hosted (sdk: the XMV player read the CRTC
   register 0x600808 through the GPU base); its launch data now says PAL and Driving.xbe plays the Paris intro movie.
   Tried first: disc reads paced at 6 MB/s (60 -> 240 frames, not the cause), OBJECT_PATH_NOT_FOUND for a missing
   folder (no change, reverted).

Tools: .local/drv.sh (recompile and run Driving.xbe from .local/launch_title_start_pal.bin), .local/drvstack.sh
(thread stacks of a hang), .local/tools/{seq,align,ident,xmatch,chain}.py (matching).

Next: the crash after the Paris movie in D3D 0x16B160 (Driving.xbe, reading [null + 8]; from game 0xF44A0).

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

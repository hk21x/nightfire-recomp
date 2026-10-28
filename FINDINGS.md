# 007: NightFire recomp - findings

What the work has established, by topic. PROGRESS.md has the dated log with the evidence for each item. Each item
is marked **[console]** (how the Xbox, its XDK or the runtime behaves: the fix is in the SDK) or **[game]** (this
game's own code and data, with its addresses).

## The image
- **[game]** Two xbes, both XDK 4831 throughout: default.xbe (on foot, Eurocom's engine, data in `eurocom/`) and
  Driving.xbe (driving levels, data in `driving/`). default.xbe's strings name "driving.xbe" and "default.xbe" for
  XLaunchNewImage. Neither links XNet or XOnline.
- **[console]** XPP (4831) keeps its device type table at the start of the XPP section (default.xbe 0x157214..
  0x157220): pointers to descriptors whose +4 is the type's init, +8 and +0xC its callbacks. Reached only through the
  table, so a callback no decode reached needs a `[functions]` entry (0x157334).
- **[console]** An unnamed XInitDevices (default.xbe 0x15753D) runs the USB driver, which touches the host controller
  at 0xFED00000 and faults.

- **[game]** EA's front end and driving levels are Driving.xbe; default.xbe (Eurocom) is the language select, intro
  movies, title and the on-foot levels. START at the title relaunches into Driving.xbe; the launch data carries the
  video standard (offset 0xE1C, 2 = PAL), which picks `PAL\<language>\` or `NTSC\<language>\` movies inside
  driving/misc.viv. A movie EA's file layer cannot open leaves its stream busy for good (0x14AF50, 0x14AEE0): the
  game hangs after 60 frames instead of skipping it.
- **[console]** XDK 4831 follows 5659 on every 3948/5659 difference; its device is 0x2C30 bytes, miniport at
  +0x23C0 (sdk/xdk/4831/layout.toml). No .XTLID section: DSOUND names come from call chains, not export ids.
- **[console]** The two xbes link different subsets of the same 4831 libraries: Driving.xbe has
  DirectSoundCreateBuffer, IDirectSoundBuffer_SetFilter, SetPixelShaderConstant and SetI3DL2Listener, which
  default.xbe does not; byte-identical DSOUND thunks make signature picks by link order risky (default.xbe's scan
  swapped SetMixBins / SetMixBinVolumes).
- **[console]** D3DDevice_GetRasterStatus reads the CRTC register through the GPU base: hosted (the XMV player).
- **[console]** A title can fill the console's 64 MB to the last 100 KB (Driving.xbe's Paris level): the host's own
  buffers live past it (mem::AllocHostContiguous), never in the title's memory.

- **[console]** The device holds the current gamma ramp (CDevice_GammaRamps, two slots) and a console starts with a
  linear one: titles read it back with GetGammaRamp (Driving.xbe 0x165E70) as the base of their fades.
- **[game]** Driving.xbe's fades scale the ramp saved at 0x23FC08 (0xE7740, per channel base * scale + 0.5).

## Push buffers
- **[game]** None: no push buffer function is linked in either xbe's D3D and game code never touches the push
  pointer; it writes D3D's shadow state directly (D3D_DirtyFlags, deferred texture state), as the inline setters do.

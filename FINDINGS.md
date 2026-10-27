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

## Push buffers
- **[game]** No NV2A command stores in default.xbe's game code (PushBufferScan). Recorded or replayed push buffers:
  not yet known.

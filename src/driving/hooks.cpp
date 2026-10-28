// Hooks for Driving.xbe (the driving levels). A strong definition replaces the recompiled function at that address,
// as in src/game/hooks.cpp.

#include "common/nightfire.h"

namespace nf {
// which exe this is (Driving.xbe): src/common's key layouts and settings tables follow it
extern const bool kDriving = true;

// EA's menus (Paused, Mission Failed): not found yet, so the mouse plays what it is bound to there too
bool MenuUp() { return false; }
}

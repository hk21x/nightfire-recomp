#include <cctype>
#include <cstdlib>
#include <string>

#include "app/app.h"
#include "core/log.h"
#include "nightfire.h"

namespace nf {
bool skip_intro = false;
}  // namespace nf

namespace {
// after the settings load: the harness's environment overrides them; each setting in effect is logged
void Init() {
    if (const char* e = std::getenv("NF_SKIP_INTRO"); e && *e) nf::skip_intro = *e == '1';
    if (nf::skip_intro) LOGR("nightfire: the intro logos are skipped (skip_intro)");
}

// skip_intro: default.xbe's EA (07370048) and MGM (07380048) logo movies reported missing, which the game passes
// over; the language select's background (07350048) and the story's movies stay
bool HideFile(const std::string& path) {
    if (!nf::skip_intro) return false;
    std::string n;
    for (char c : path) n += char(std::tolower(static_cast<unsigned char>(c)));
    for (const char* logo : {"\\25_fps\\07370048.xmv", "\\25_fps\\07380048.xmv"})
        if (n.size() >= std::char_traits<char>::length(logo) &&
            n.compare(n.size() - std::char_traits<char>::length(logo), std::string::npos, logo) == 0)
            return true;
    return false;
}
}  // namespace

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"007: NightFire",          // window and splash
    .short_title = L"NightFire",          // message boxes
    .name = L"Nightfire",                 // window classes, crash dumps, handover event
    .log = "nightfire",                   // log file prefix
    .settings = "settings.toml",          // settings file beside the exe
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe and Driving.xbe with their folders beside them.",
    .images = {{"default.xbe", "Nightfire"}, {"driving.xbe", "NightfireDriving"}},
                                          // each recompiled xbe and its exe, for title launches
    .options = {{"skip_intro", &nf::skip_intro, nullptr, "Skip intro movies",
                 "Starts without the EA and MGM logo movies"}},
    .load = nullptr,                      // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = Init,                         // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = nullptr,                    // the game window is up
    .message = nullptr,                   // window messages first; true takes one
    .ui = nullptr,                        // ImGui drawing each frame
    .pad = nullptr,                       // changes a polled pad
    .rumble = nullptr,                    // changes a rumble before it is set
    .pal_console = true,                  // a PAL disc (region 4): the driving levels ask for NTSC\ files otherwise
    .hide_file = HideFile,                // skip_intro
    // optional, after these: .achievements (conditions on guest memory), .same_code (other releases' xbe SHA-1s with
    // the same code), .wheel (what a steering wheel's force feedback follows)
};

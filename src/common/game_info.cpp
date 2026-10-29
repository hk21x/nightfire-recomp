#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <toml++/toml.hpp>
#endif

#include "app/app.h"
#include "core/input.h"
#include "core/log.h"
#include "nightfire.h"

namespace nf {
bool skip_intro = false;
uint32_t fov_percent = 100;

float Ultrawide() {
    static const float forced = [] {  // NF_ULTRAWIDE=k headless (no settings or screen): the 3D k times wider
        const char* e = std::getenv("NF_ULTRAWIDE");
        return e && *e ? float(std::atof(e)) : 0.0f;
    }();
    if (!app::Widescreen()) return 1.0f;
    float k = forced > 0 ? forced : std::max(app::FrameAspect(), 16.0f / 9.0f) / (16.0f / 9.0f);
    static bool said = false;
    if (k > 1.0f + 1e-3f && !said) {
        said = true;
        LOGR("nightfire: 3D views widened %.3f times for the %.3f:1 frame", k, app::FrameAspect());
    }
    return k;
}

float FovScale() {
    uint32_t pct = std::clamp<uint32_t>(fov_percent, 50, 150);
    static uint32_t said = 100;
    if (said != pct) {
        said = pct;
        LOGR("nightfire: field of view %u%% of the game's", pct);
    }
    return float(pct) / 100;
}
}  // namespace nf

namespace {
// after the settings load: the harness's environment overrides them; each setting in effect is logged
void Init() {
    if (const char* e = std::getenv("NF_SKIP_INTRO"); e && *e) nf::skip_intro = *e == '1';
    if (const char* e = std::getenv("NF_FOV"); e && *e) nf::fov_percent = uint32_t(std::atoi(e));
    // every achievement's condition parses (a typo would leave it never met)
    for (const achievements::Def& d : app::kGame.achievements)
        if (std::string e = achievements::Check(d.condition); !e.empty())
            LOGE("nightfire: achievement %s: %s", d.id, e.c_str());
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

// keyboard and mouse (Windows virtual key codes: this file builds for the headless harness too)
constexpr uint16_t kMouseLeft = 0x01, kMouseRight = 0x02, kMouseMiddle = 0x04, kTab = 0x09, kEnter = 0x0D,
                   kEscape = 0x1B, kSpace = 0x20, kArrowLeft = 0x25, kArrowUp = 0x26, kArrowRight = 0x27, kArrowDown = 0x28,
                   kLeftShift = 0xA0, kLeftCtrl = 0xA2, kBackspace = 0x08;
// on foot (default.xbe): a PC shooter's layout for the game's NightFire controller style (the keyboard plays as it:
// src/game/controls.cpp), as its controls page lists it: A crouch, X reload/action, Y jump, B next weapon, White alt.
// fire, Black next gadget, L manual aim, R fire, Back vision mode, the d-pad's left and right cycle the weapon (the
// mouse wheel too: src/common/menu_mouse.cpp), up and down the gadget and the zoom; the mouse looks and aims
std::array<uint16_t, 2> ShooterKeys(int c) {
    using namespace input;
    switch (c) {
        case kLSUp: return {'W', 0};
        case kLSDown: return {'S', 0};
        case kLSLeft: return {'A', 0};
        case kLSRight: return {'D', 0};
        case kRT: return {kMouseLeft, 0};    // fire
        case kLT: return {kMouseRight, 0};   // manual aim
        case kA: return {'C', kLeftCtrl};    // crouch (Enter selects in the menus: menu_mouse.cpp)
        case kX: return {'E', 'R'};          // action, reload
        case kY: return {kSpace, 0};         // jump
        case kB: return {'Q', kBackspace};   // next weapon; back in the menus
        case kWhite: return {kMouseMiddle, 'F'};  // alt. fire
        case kBlack: return {'G', 0};        // next gadget
        case kBack: return {'V', kTab};      // vision mode
        case kUp: return {kArrowUp, '1'};    // the d-pad: gadget and zoom up and down, the weapon left and right
        case kDown: return {kArrowDown, '2'};
        case kLeft: return {kArrowLeft, '3'};
        case kRight: return {kArrowRight, '4'};
        case kStart: return {kEscape, 'P'};
        default: return {0, 0};
    }
}
// driving (Driving.xbe), the car's controls page (Driving Config): R gas, L brake/reverse, the left stick steers, A
// fire, B handbrake, Y camera, X gadget, the d-pad's left and right and Black/White cycle the weapon (the mouse wheel
// too), the right stick's click looks back. On a mounted gun (Paris, Alpine Escape) the mouse aims, its buttons fire
// and zoom and R reloads instead (src/common/menu_mouse.cpp, src/driving/mouse.cpp)
std::array<uint16_t, 2> DrivingKeys(int c) {
    using namespace input;
    switch (c) {
        case kRT: return {'W', 0};                   // gas
        case kLT: return {'S', 0};                   // brake, reverse
        case kLSLeft: return {'A', kArrowLeft};      // steer
        case kLSRight: return {'D', kArrowRight};
        case kLSUp: return {kArrowUp, 0};            // the menus
        case kLSDown: return {kArrowDown, 0};
        case kA: return {kMouseLeft, kEnter};        // fire; select in the menus
        case kB: return {kSpace, kBackspace};        // handbrake; back in the menus
        case kX: return {kMouseRight, 'E'};          // the gadget
        case kY: return {'C', 0};                    // the camera
        case kBlack: return {'Q', 0};                // the next weapon
        case kWhite: return {'Z', 0};                // the weapon before
        case kRThumb: return {'V', kMouseMiddle};    // look back
        case kLeft: return {'1', 0};                 // the d-pad: the weapon
        case kRight: return {'2', 0};
        case kUp: return {'3', 0};
        case kDown: return {'4', 0};
        case kStart: return {kEscape, 'P'};
        case kBack: return {kTab, 0};
        default: return {0, 0};
    }
}
std::array<uint16_t, 2> Keys(int c) { return nf::kDriving ? DrivingKeys(c) : ShooterKeys(c); }

#ifdef _WIN32  // the settings file (the headless harness has none)
// One settings file for both exes, each with its own keys: the runtime reads and writes [keys] as this exe's (only
// what differs from its defaults); Driving.xbe's are kept as [driving_keys] instead, and each exe writes the other's
// table back as it read it (EoN's way)
constexpr const char* kOtherKeys[2] = {"driving_keys", "keys"};  // by kDriving: the table this exe keeps unread
toml::table g_other_keys;

void LoadKeys(const void* table) {
    const toml::table& t = *static_cast<const toml::table*>(table);
    const toml::table* other = t[kOtherKeys[nf::kDriving]].as_table();
    g_other_keys = other ? *other : toml::table{};
    if (!nf::kDriving) return;
    // the runtime took [keys], default.xbe's, as these: the driving defaults and [driving_keys] instead
    app::Settings& s = app::Get();
    for (int c = 0; c < input::kControls && c < int(s.keys.size()); c++) s.keys[c] = DrivingKeys(c);
    if (const toml::table* mine = t["driving_keys"].as_table())
        for (auto& [name, v] : *mine) {
            int c = input::ControlByName(std::string(name.str()).c_str());
            const toml::array* a = v.as_array();
            if (c < 0 || c >= int(s.keys.size()) || !a) continue;
            s.keys[c] = {0, 0};
            for (size_t i = 0; i < 2 && i < a->size(); i++) s.keys[c][i] = app::KeyFromName((*a)[i].value_or(std::string()));
        }
}

void SaveKeys(void* table) {
    toml::table& t = *static_cast<toml::table*>(table);
    if (nf::kDriving) {
        // the runtime wrote this exe's as [keys]
        if (toml::table* mine = t["keys"].as_table()) {
            toml::table copy = *mine;
            t.erase("keys");
            t.insert("driving_keys", copy);
        }
    }
    if (!g_other_keys.empty()) t.insert_or_assign(kOtherKeys[nf::kDriving], g_other_keys);
}
#else
constexpr void (*LoadKeys)(const void*) = nullptr;
constexpr void (*SaveKeys)(void*) = nullptr;
#endif
}  // namespace

// GameInfo::texture_bound: the fonts (src/common/fonts.cpp), the button pictures (src/common/buttons.cpp)
bool TextureBound(uint32_t header, uint32_t palette) {
    nf::FontPicture(header);
    return nf::ButtonPictures(header, palette);
}

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
                 "Starts without the EA and MGM logo movies"},
                {"fov_percent", nullptr, &nf::fov_percent, "Field of view (%)",
                 "50 to 150; 100 is the game's. On foot, driving and in the engine's cutscenes; the scope zooms in proportion."}},
    .load = LoadKeys,                     // Driving.xbe's keys apart (DrivingKeys)
    .save = SaveKeys,
    .init = Init,                         // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = nullptr,                    // the game window is up
    .message = nullptr,                   // window messages first; true takes one
    .ui = nullptr,                        // ImGui drawing each frame
    .pad = nf::MenuMouse,                 // changes a polled pad: the mouse in the menus
    .rumble = nullptr,                    // changes a rumble before it is set
    .achievements = nf::AchievementSet(), // conditions on default.xbe's Results page (src/common/achievement_set.cpp)
    .squeeze_screen_programs = true,      // ultrawide: Driving.xbe draws its HUD and menus through vertex programs
    .sharpen_2d = true,                   // text and HUD pictures enlarged sharp (Settings::sharp_2d turns it off)
    .pal_console = true,                  // a PAL disc (region 4): the driving levels ask for NTSC\ files otherwise
    .hide_file = HideFile,                // skip_intro
    .mouse_look = true,                   // the mouse looks and aims
    .mouse_direct = true,                 // the view and the mounted guns turn by it directly (src/game/mouse.cpp,
                                          // src/driving/mouse.cpp), not as the right stick
    .default_keys = Keys,                 // a shooter's keys on foot, a driving layout in Driving.xbe
    .texture_bound = TextureBound,        // the button pictures as the keys, the fonts with smooth edges
    // optional, after these: .achievements (conditions on guest memory), .same_code (other releases' xbe SHA-1s with
    // the same code), .wheel (what a steering wheel's force feedback follows)
};

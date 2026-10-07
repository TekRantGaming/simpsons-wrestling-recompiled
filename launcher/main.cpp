// The Simpsons Wrestling Recompiled: the TRG Launcher in front of the game.
//
// Players open this program. It keeps its own settings in launcher.txt, writes
// the runtime's [video] settings into settings.toml, and on PLAY starts the
// recompiled game (SimpsonsWrestling_Recompiled) with the chosen disc. The PC
// features the game's plugin applies (simpsons_mods.c) read launcher.txt too.
//
// A release keeps the game program, its data and every settings file in a game
// folder next to this program, so the launcher is the only program a player
// sees; a development build has everything in one folder. When the launcher
// opens it can look for a newer release on GitHub and install it.
//
//   SimpsonsWrestling [--launcher] [--page N] [--ready] [--expand] [--screenshot out.ppm]
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

#include "trg/download.h"
#include "trg/launcher.h"
#include "trg/platform.h"
#include "trg/standalone.h"

namespace {

constexpr long long kDiscBinSize = 506830128;  // Simpsons Wrestling, The (USA) (Track 1).bin
const char* const kGameExeName =
#ifdef _WIN32
    "SimpsonsWrestling_Recompiled.exe";
#else
    "SimpsonsWrestling_Recompiled";
#endif

std::string ExeDir() {
  std::string path;
#ifdef _WIN32
  wchar_t buf[MAX_PATH * 2];
  const DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(sizeof buf / sizeof buf[0]));
  if (n > 0 && n < sizeof buf / sizeof buf[0]) {
    const int len = WideCharToMultiByte(CP_UTF8, 0, buf, int(n), nullptr, 0, nullptr, nullptr);
    path.resize(size_t(len));
    WideCharToMultiByte(CP_UTF8, 0, buf, int(n), path.data(), len, nullptr, nullptr);
  }
#else
  char buf[PATH_MAX];
  const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n > 0) path.assign(buf, size_t(n));
#endif
  std::replace(path.begin(), path.end(), '\\', '/');
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? std::string("./") : path.substr(0, slash + 1);
}

// The folder with the game program, its data and every settings file: game/
// next to the launcher in a release, the launcher's own folder in a
// development build. The game's plugin reads launcher.txt from the game's
// folder, so the launcher keeps its settings there too.
std::string GameDir(const std::string& root) {
  const std::string sub = root + "game/";
  return trg::FileSize(sub + kGameExeName) > 0 ? sub : root;
}

std::string Lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// ------------------------------------------------------------------ disc ---

struct DiscCheck {
  bool ok = false;
  std::string detail;
};

// A .cue for the USA disc: its first FILE must exist and have the Redump size.
DiscCheck CheckDisc(const std::string& cue) {
  if (cue.empty()) return {false, "Select your copy of the game: the .cue file of a disc image."};
  if (Lower(cue.substr(cue.size() > 4 ? cue.size() - 4 : 0)) != ".cue")
    return {false, "Pick the .cue file, not the .bin. The .bin must sit next to it."};
  FILE* f = trg::OpenFile(cue, "rb");
  if (!f) return {false, "The .cue file cannot be opened: " + cue};
  std::string text;
  char chunk[4096];
  size_t got;
  while ((got = std::fread(chunk, 1, sizeof chunk, f)) > 0 && text.size() < 65536) text.append(chunk, got);
  std::fclose(f);
  const size_t at = text.find("FILE \"");
  if (at == std::string::npos) return {false, "This .cue file names no track file."};
  const size_t end = text.find('"', at + 6);
  if (end == std::string::npos) return {false, "This .cue file names no track file."};
  const std::string bin_name = text.substr(at + 6, end - at - 6);
  std::string dir = cue;
  std::replace(dir.begin(), dir.end(), '\\', '/');
  dir = dir.substr(0, dir.rfind('/') + 1);
  const long long size = trg::FileSize(dir + bin_name);
  if (size < 0) return {false, "The track file \"" + bin_name + "\" is missing next to the .cue."};
  if (size != kDiscBinSize)
    return {false, "This is not The Simpsons Wrestling (USA). Its track is " + std::to_string(size) +
                       " bytes; the USA disc's is " + std::to_string(kDiscBinSize) + "."};
  return {true, cue};
}

// ---------------------------------------------------------- settings.toml ---

// Sets `key = value` in [section] of the runtime's settings.toml (and of its
// input.ini / keybinds.ini, which use the same `key = value` lines), keeping
// everything else in the file (the runtime writes its own keys there too).
class TomlEditor {
 public:
  explicit TomlEditor(std::string path) : path_(std::move(path)) {
    std::ifstream in(path_);
    for (std::string line; std::getline(in, line);) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      lines_.push_back(line);
    }
  }

  void Set(const std::string& section, const std::string& key, const std::string& value) {
    const std::string header = "[" + section + "]";
    int sec = -1, last = -1;
    for (int i = 0; i < int(lines_.size()); ++i) {
      const std::string t = Trim(lines_[size_t(i)]);
      if (!t.empty() && t[0] == '[') {
        if (sec >= 0) break;
        if (t == header) sec = last = i;
        continue;
      }
      if (sec < 0) continue;
      if (!t.empty() && t[0] != '#') last = i;
      const size_t eq = t.find('=');
      if (eq != std::string::npos && Trim(t.substr(0, eq)) == key) {
        lines_[size_t(i)] = key + " = " + value;
        return;
      }
    }
    if (sec < 0) {
      if (!lines_.empty() && !Trim(lines_.back()).empty()) lines_.push_back("");
      lines_.push_back(header);
      lines_.push_back(key + " = " + value);
      return;
    }
    lines_.insert(lines_.begin() + last + 1, key + " = " + value);
  }

  // The value of `key` in [section], without quotes; empty when absent.
  std::string Get(const std::string& section, const std::string& key) const {
    const std::string header = "[" + section + "]";
    bool in = false;
    for (const std::string& line : lines_) {
      const std::string t = Trim(line);
      if (!t.empty() && t[0] == '[') {
        in = t == header;
        continue;
      }
      const size_t eq = t.find('=');
      if (!in || eq == std::string::npos || Trim(t.substr(0, eq)) != key) continue;
      std::string v = Trim(t.substr(eq + 1));
      if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
      return v;
    }
    return {};
  }

  // Sections named "<prefix>N" (keybinds.ini: player1, player2...).
  std::vector<std::string> SectionsStartingWith(const std::string& prefix) const {
    std::vector<std::string> out;
    for (const std::string& line : lines_) {
      const std::string t = Trim(line);
      if (t.size() > 2 + prefix.size() && t[0] == '[' && t.compare(1, prefix.size(), prefix) == 0)
        out.push_back(t.substr(1, t.size() - 2));
    }
    return out;
  }

  bool Exists() const { return !lines_.empty(); }

  bool Save() const {
    const std::string tmp = path_ + ".tmp";
    {
      std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
      if (!out) return false;
      if (lines_.empty()) out << "# Written by the launcher. Safe to hand-edit.\n";
      for (const std::string& l : lines_) out << l << '\n';
      if (!out) return false;
    }
    return trg::MoveFileOver(tmp, path_);
  }

 private:
  static std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t") - a + 1);
  }
  std::string path_;
  std::vector<std::string> lines_;
};

std::string Quote(const std::string& s) { return "\"" + s + "\""; }

// Game controllers SDL sees right now. Opens SDL's controller subsystem when
// the launcher window has not (hidden launcher, or after PLAY closed it).
std::vector<std::string> ConnectedControllers() {
  const bool was_init = SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 0;
  const Uint32 subsystems = SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS;
  if (!was_init && SDL_InitSubSystem(subsystems) != 0) return {};
  if (!was_init) {
    // Without the launcher window nothing has pumped SDL's events yet, and
    // some controllers arrive as device-added events: give them a moment.
    for (int i = 0; i < 10 && SDL_NumJoysticks() == 0; ++i) {
      SDL_PumpEvents();
      SDL_Delay(25);
    }
  }
  std::vector<std::string> names;
  for (int i = 0; i < SDL_NumJoysticks(); ++i) {
    if (!SDL_IsGameController(i)) continue;
    const char* name = SDL_GameControllerNameForIndex(i);
    names.push_back(name ? name : "Controller");
  }
  if (!was_init) SDL_QuitSubSystem(subsystems);
  return names;
}

// ----------------------------------------------------------- input files ---

// The PlayStation buttons the Controls page remaps: their input.ini /
// keybinds.ini key, label, default controller input and default keyboard key
// (the runtime's own defaults).
struct PadButton {
  const char* key;
  const char* label;
  const char* pad;
  const char* keyboard;
};
constexpr PadButton kPadButtons[] = {
    {"cross", "Cross", "a", "X"},           {"circle", "Circle", "b", "S"},
    {"square", "Square", "x", "Z"},         {"triangle", "Triangle", "y", "A"},
    {"l1", "L1", "leftshoulder", "Q"},      {"r1", "R1", "rightshoulder", "W"},
    {"l2", "L2", "lefttrigger", "E"},       {"r2", "R2", "righttrigger", "R"},
    {"start", "Start", "start", "Return"},  {"select", "Select", "back", "Right Shift"},
    {"l3", "L3", "leftstick", "T"},         {"r3", "R3", "rightstick", "Y"},
    {"up", "Up", "dpup", "Up"},             {"down", "Down", "dpdown", "Down"},
    {"left", "Left", "dpleft", "Left"},     {"right", "Right", "dpright", "Right"},
};
constexpr int kRemappableOnController = 12;  // the directions keep the d-pad and left stick

// Controller inputs a button can be assigned to (SDL names, as input.ini uses).
struct PadInput {
  const char* name;
  const char* label;
};
constexpr PadInput kPadInputs[] = {
    {"a", "A / Cross"},           {"b", "B / Circle"},          {"x", "X / Square"},
    {"y", "Y / Triangle"},        {"leftshoulder", "LB / L1"},  {"rightshoulder", "RB / R1"},
    {"lefttrigger", "LT / L2"},   {"righttrigger", "RT / R2"},  {"back", "View / Select"},
    {"start", "Menu / Start"},    {"leftstick", "Left stick click"}, {"rightstick", "Right stick click"},
    {"dpup", "D-pad up"},         {"dpdown", "D-pad down"},     {"dpleft", "D-pad left"},
    {"dpright", "D-pad right"},   {"", "Nothing"},
};

const char* PadInputLabel(const std::string& name) {
  for (const PadInput& in : kPadInputs)
    if (name == in.name) return in.label;
  return name.empty() ? "Nothing" : name.c_str();
}

// The full default input.ini the runtime writes on its first run, for a
// launcher that runs before the game ever has.
const char* const kDefaultInputIni =
    "; PSXRecomp input mapping. PSX buttons are active when any listed source is pressed.\n"
    "; Sources use SDL/Xbox names: a,b,x,y,back,start,leftshoulder,rightshoulder,\n"
    "; lefttrigger[/+],righttrigger[/+],leftstick,rightstick (stick clicks -> L3/R3),\n"
    "; dpup,dpdown,dpleft,dpright,leftx-/leftx+/lefty-/lefty+.\n"
    "\n[controller]\nenabled = true\ndevice = 0\ndeadzone = 3277\n"
    "\n[mapping]\nup = dpup\ndown = dpdown\nleft = dpleft\nright = dpright\ncross = a\ncircle = b\n"
    "square = x\ntriangle = y\nl1 = leftshoulder\nr1 = rightshoulder\nl2 = lefttrigger\nr2 = righttrigger\n"
    "l3 = leftstick\nr3 = rightstick\nstart = start\nselect = back\nls_up = lefty-\nls_down = lefty+\n"
    "ls_left = leftx-\nls_right = leftx+\nrs_up = righty-\nrs_down = righty+\nrs_left = rightx-\n"
    "rs_right = rightx+\n";

// ImGui key -> the SDL key name keybinds.ini uses (SDL_GetScancodeName).
std::string KeyNameForImGuiKey(ImGuiKey key) {
  struct Pair {
    ImGuiKey key;
    SDL_Scancode code;
  };
  static const Pair kPairs[] = {
      {ImGuiKey_Tab, SDL_SCANCODE_TAB},           {ImGuiKey_LeftArrow, SDL_SCANCODE_LEFT},
      {ImGuiKey_RightArrow, SDL_SCANCODE_RIGHT},  {ImGuiKey_UpArrow, SDL_SCANCODE_UP},
      {ImGuiKey_DownArrow, SDL_SCANCODE_DOWN},    {ImGuiKey_PageUp, SDL_SCANCODE_PAGEUP},
      {ImGuiKey_PageDown, SDL_SCANCODE_PAGEDOWN}, {ImGuiKey_Home, SDL_SCANCODE_HOME},
      {ImGuiKey_End, SDL_SCANCODE_END},           {ImGuiKey_Insert, SDL_SCANCODE_INSERT},
      {ImGuiKey_Delete, SDL_SCANCODE_DELETE},     {ImGuiKey_Backspace, SDL_SCANCODE_BACKSPACE},
      {ImGuiKey_Space, SDL_SCANCODE_SPACE},       {ImGuiKey_Enter, SDL_SCANCODE_RETURN},
      {ImGuiKey_LeftCtrl, SDL_SCANCODE_LCTRL},    {ImGuiKey_LeftShift, SDL_SCANCODE_LSHIFT},
      {ImGuiKey_LeftAlt, SDL_SCANCODE_LALT},      {ImGuiKey_RightCtrl, SDL_SCANCODE_RCTRL},
      {ImGuiKey_RightShift, SDL_SCANCODE_RSHIFT}, {ImGuiKey_RightAlt, SDL_SCANCODE_RALT},
      {ImGuiKey_Apostrophe, SDL_SCANCODE_APOSTROPHE}, {ImGuiKey_Comma, SDL_SCANCODE_COMMA},
      {ImGuiKey_Minus, SDL_SCANCODE_MINUS},       {ImGuiKey_Period, SDL_SCANCODE_PERIOD},
      {ImGuiKey_Slash, SDL_SCANCODE_SLASH},       {ImGuiKey_Semicolon, SDL_SCANCODE_SEMICOLON},
      {ImGuiKey_Equal, SDL_SCANCODE_EQUALS},      {ImGuiKey_LeftBracket, SDL_SCANCODE_LEFTBRACKET},
      {ImGuiKey_Backslash, SDL_SCANCODE_BACKSLASH}, {ImGuiKey_RightBracket, SDL_SCANCODE_RIGHTBRACKET},
      {ImGuiKey_GraveAccent, SDL_SCANCODE_GRAVE}, {ImGuiKey_KeypadDecimal, SDL_SCANCODE_KP_PERIOD},
      {ImGuiKey_KeypadDivide, SDL_SCANCODE_KP_DIVIDE}, {ImGuiKey_KeypadMultiply, SDL_SCANCODE_KP_MULTIPLY},
      {ImGuiKey_KeypadSubtract, SDL_SCANCODE_KP_MINUS}, {ImGuiKey_KeypadAdd, SDL_SCANCODE_KP_PLUS},
      {ImGuiKey_KeypadEnter, SDL_SCANCODE_KP_ENTER},
  };
  SDL_Scancode code = SDL_SCANCODE_UNKNOWN;
  if (key >= ImGuiKey_A && key <= ImGuiKey_Z) code = SDL_Scancode(SDL_SCANCODE_A + (key - ImGuiKey_A));
  else if (key >= ImGuiKey_1 && key <= ImGuiKey_9) code = SDL_Scancode(SDL_SCANCODE_1 + (key - ImGuiKey_1));
  else if (key == ImGuiKey_0) code = SDL_SCANCODE_0;
  else if (key >= ImGuiKey_F1 && key <= ImGuiKey_F12) code = SDL_Scancode(SDL_SCANCODE_F1 + (key - ImGuiKey_F1));
  else if (key >= ImGuiKey_Keypad1 && key <= ImGuiKey_Keypad9)
    code = SDL_Scancode(SDL_SCANCODE_KP_1 + (key - ImGuiKey_Keypad1));
  else if (key == ImGuiKey_Keypad0) code = SDL_SCANCODE_KP_0;
  else
    for (const Pair& p : kPairs)
      if (p.key == key) code = p.code;
  if (code == SDL_SCANCODE_UNKNOWN) return {};
  const char* name = SDL_GetScancodeName(code);
  return name && *name ? name : std::string();
}

// Monitors SDL sees, as "N. Name (WxH)".
std::vector<trg::Option> MonitorOptions() {
  std::vector<trg::Option> out;
  const int n = SDL_GetNumVideoDisplays();
  for (int i = 0; i < n; ++i) {
    SDL_DisplayMode mode{};
    const char* name = SDL_GetDisplayName(i);
    std::string label = std::to_string(i + 1) + ". " + (name && *name ? name : "Display");
    if (SDL_GetCurrentDisplayMode(i, &mode) == 0)
      label += " (" + std::to_string(mode.w) + "\xC3\x97" + std::to_string(mode.h) + ")";
    out.push_back({std::to_string(i), label});
  }
  if (out.empty()) out.push_back({"0", "Main display"});
  return out;
}

std::string ReadFirstLine(const std::string& path) {
  std::ifstream in(path);
  std::string line;
  std::getline(in, line);
  while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
  return line;
}

// --------------------------------------------------------------- header ---

// The Simpsons' sky: a deep-to-pale blue gradient with soft clouds drifting
// across, the opening every episode starts with.
trg::HeaderPainter SpringfieldSky() {
  return [](const trg::HeaderContext& c) {
    ImDrawList* d = c.draw;
    d->AddRectFilledMultiColor(c.min, c.max, IM_COL32(56, 120, 214, 255), IM_COL32(56, 120, 214, 255),
                               IM_COL32(150, 203, 250, 255), IM_COL32(150, 203, 250, 255));
    const float w = c.max.x - c.min.x, h = c.max.y - c.min.y;
    struct Cloud {
      float x, y, size, speed;
    };
    static const Cloud kClouds[] = {{0.05f, 0.30f, 1.00f, 9.0f}, {0.32f, 0.62f, 0.75f, 13.0f},
                                    {0.58f, 0.25f, 1.20f, 7.0f}, {0.80f, 0.70f, 0.85f, 11.0f},
                                    {0.95f, 0.38f, 0.65f, 15.0f}};
    for (const Cloud& cl : kClouds) {
      const float span = w + 260.0f * c.scale;
      float x = std::fmod(cl.x * span + c.time * cl.speed * c.scale, span) - 130.0f * c.scale;
      const float y = c.min.y + cl.y * h;
      const float r = 26.0f * cl.size * c.scale;
      const ImU32 shade = IM_COL32(214, 232, 250, 255), white = IM_COL32(255, 255, 255, 255);
      const float puffs[][3] = {{-1.3f, 0.25f, 0.70f}, {-0.55f, -0.15f, 0.95f}, {0.35f, -0.30f, 1.10f},
                                {1.20f, 0.05f, 0.85f}, {1.90f, 0.30f, 0.60f}};
      for (const auto& p : puffs)
        d->AddCircleFilled(ImVec2(c.min.x + x + p[0] * r, y + p[1] * r + 3.0f * c.scale), p[2] * r, shade, 32);
      for (const auto& p : puffs)
        d->AddCircleFilled(ImVec2(c.min.x + x + p[0] * r, y + p[1] * r), p[2] * r * 0.94f, white, 32);
    }
  };
}

// A pink-frosted donut with sprinkles, drawn in code (no image files).
std::vector<uint32_t> DonutIcon(int n) {
  std::vector<uint32_t> px(size_t(n * n), 0);
  static const uint32_t kSprinkles[] = {0xFF2D9CDB, 0xFF27AE60, 0xFFF2C94C, 0xFFFFFFFF, 0xFFEB5757};
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) {
      const float fx = (x + 0.5f) / n - 0.5f, fy = (y + 0.5f) / n - 0.5f;
      const float r = std::sqrt(fx * fx + fy * fy);
      if (r > 0.47f || r < 0.15f) continue;
      uint8_t cr = 214, cg = 160, cb = 94;  // dough
      const float wobble = 0.012f * std::sin(std::atan2(fy, fx) * 9.0f);
      if (r > 0.18f + wobble && r < 0.41f + wobble) cr = 247, cg = 140, cb = 186;  // frosting
      const float shade = 1.0f - 0.35f * std::max(0.0f, fx + fy);
      cr = uint8_t(std::min(255.0f, cr * shade)), cg = uint8_t(std::min(255.0f, cg * shade)),
      cb = uint8_t(std::min(255.0f, cb * shade));
      uint32_t c = uint32_t(cr) | uint32_t(cg) << 8 | uint32_t(cb) << 16 | 0xFF000000u;
      if (cr > 200 && cg < 170) {
        const int cell = ((x / (n / 16)) * 7 + (y / (n / 16)) * 13) % 11;
        const bool dot = ((x % (n / 16)) < n / 48) && ((y % (n / 16)) < n / 24);
        if (cell < 5 && dot) c = kSprinkles[cell];
      }
      px[size_t(y * n + x)] = c;
    }
  return px;
}

void WritePpm(const char* path, int w, int h) {
  using ReadPixelsFn = void(APIENTRY*)(int, int, int, int, unsigned, unsigned, void*);
  const auto read = reinterpret_cast<ReadPixelsFn>(SDL_GL_GetProcAddress("glReadPixels"));
  if (!read) return;
  std::vector<unsigned char> px(size_t(w) * size_t(h) * 4);
  read(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  FILE* f = std::fopen(path, "wb");
  if (!f) return;
  std::fprintf(f, "P6\n%d %d\n255\n", w, h);
  std::vector<unsigned char> row(size_t(w) * 3);
  for (int y = h - 1; y >= 0; --y) {
    const unsigned char* src = &px[size_t(y) * size_t(w) * 4];
    for (int x = 0; x < w; ++x)
      for (int c = 0; c < 3; ++c) row[size_t(x) * 3 + size_t(c)] = src[x * 4 + c];
    std::fwrite(row.data(), 1, row.size(), f);
  }
  std::fclose(f);
}

// --------------------------------------------------------------- updates ---
// Updates from GitHub releases, as in the other TekRant ports: when the
// launcher opens it reads this repository's releases, and when one is newer
// than this copy it offers to download it and swap in the new program files.
// Settings, saves and caches are never replaced. Nothing is contacted when
// Updates is Off.

namespace fs = std::filesystem;

// The release list, not /releases/latest: that one skips prereleases.
constexpr const char* kReleasesApi =
    "https://api.github.com/repos/TekRantGaming/simpsons-wrestling-recompiled/releases?per_page=20";
constexpr const char* kReleasePage = "https://github.com/TekRantGaming/simpsons-wrestling-recompiled/releases/tag/";
#ifdef _WIN32
constexpr const char* kUpdateSuffix = "-windows-x64.zip";
const char* const kLauncherExeName = "SimpsonsWrestling.exe";
#else
constexpr const char* kUpdateSuffix = "-linux-x86_64.AppImage";
const char* const kLauncherExeName = "SimpsonsWrestling";
#endif

struct Release {
  std::string tag;   // "v1.0.1"
  std::string url;   // this platform's download
  std::string page;  // release page, for the player
};

fs::path U8(const std::string& utf8) { return fs::u8path(utf8); }

// "v1.2.3" / "1.2.3-beta" -> {1, 2, 3}
std::vector<int> ParseVersion(const std::string& text) {
  std::vector<int> parts;
  std::smatch m;
  std::string rest = text;
  static const std::regex number(R"((\d+))");
  while (parts.size() < 4 && std::regex_search(rest, m, number)) {
    parts.push_back(std::stoi(m[1].str()));
    rest = m.suffix().str();
    if (rest.empty() || rest[0] != '.') break;
  }
  while (parts.size() < 3) parts.push_back(0);
  return parts;
}

// The first "key": "value" string in a JSON text, from `from` on.
std::string JsonString(const std::string& json, const std::string& key, size_t from = 0, size_t* at = nullptr) {
  const std::string needle = "\"" + key + "\"";
  const size_t k = json.find(needle, from);
  if (k == std::string::npos) return {};
  const size_t colon = json.find(':', k + needle.size());
  const size_t q1 = colon == std::string::npos ? colon : json.find('"', colon + 1);
  if (q1 == std::string::npos) return {};
  std::string value;
  for (size_t i = q1 + 1; i < json.size() && json[i] != '"'; ++i) {
    if (json[i] == '\\' && i + 1 < json.size()) ++i;
    value += json[i];
  }
  if (at) *at = k;
  return value;
}

// The newest published release with this platform's download, when it is
// newer than `current` (blocking: run it on a Task). `error` gets a message
// when the check itself failed.
std::optional<Release> CheckLatest(const std::string& current, std::string* error) {
  std::string json;
  if (std::string err = trg::HttpGet(kReleasesApi, json); !err.empty()) {
    *error = err;
    return std::nullopt;
  }
  std::optional<Release> best;
  for (size_t pos = 0;;) {
    size_t tag_at = 0;
    const std::string tag = JsonString(json, "tag_name", pos, &tag_at);
    if (tag.empty()) break;
    size_t next_at = 0;
    const bool has_next = !JsonString(json, "tag_name", tag_at + 1, &next_at).empty();
    const std::string object = json.substr(tag_at, (has_next ? next_at : json.size()) - tag_at);
    pos = tag_at + 1;
    if (object.find("\"draft\": true") != std::string::npos || object.find("\"draft\":true") != std::string::npos)
      continue;
    Release r;
    r.tag = tag;
    r.page = kReleasePage + tag;
    for (size_t a = 0;;) {
      size_t url_at = 0;
      const std::string url = JsonString(object, "browser_download_url", a, &url_at);
      if (url.empty()) break;
      if (url.size() >= std::strlen(kUpdateSuffix) &&
          url.compare(url.size() - std::strlen(kUpdateSuffix), std::string::npos, kUpdateSuffix) == 0) {
        r.url = url;
        break;
      }
      a = url_at + 1;
    }
    if (r.url.empty()) continue;
    if (!best || ParseVersion(r.tag) > ParseVersion(best->tag)) best = r;
  }
  if (!best && json.find("tag_name") == std::string::npos) *error = "GitHub did not return any releases.";
  if (!best || ParseVersion(best->tag) <= ParseVersion(current)) return std::nullopt;
  return best;
}

// The player's own files in the game folder, which an update never replaces.
bool IsPlayerFile(const fs::path& rel) {
  auto it = rel.begin();
  if (it == rel.end() || it->string() != "game") return false;
  if (++it == rel.end()) return false;
  const std::string name = it->string();
  if (name == "saves" || name == "cache") return true;
  if (name == "launcher.txt" || name == "settings.toml" || name == "input.ini" || name == "keybinds.ini") return true;
  return name == "mods" && ++it != rel.end() && (it->string() == "state.toml" || it->string() == "installed");
}

#ifdef _WIN32
// Runs a command hidden and waits; returns its exit code (-1 if it could not start).
int RunHidden(std::wstring command) {
  STARTUPINFOW si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    return -1;
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return int(code);
}
#endif

// Task job: downloads the release and puts its program files in place.
// Windows: unpacks the zip with Windows' own tar and copies every file over
// the launcher's folder; a running exe can be renamed but not overwritten, so
// each replaced file is moved aside to *.old first. Linux: the new AppImage
// replaces the one the player started (a running AppImage stays mounted).
std::string InstallUpdate(trg::Task& task, const Release& release, const std::string& root) {
  std::error_code ec;
#ifdef _WIN32
  const fs::path work = fs::temp_directory_path(ec) / "simpsons_wrestling_update";
  fs::remove_all(work, ec);
  fs::create_directories(work / "files", ec);
  const fs::path zip = work / "update.zip";
  task.SetLabel("Downloading " + release.tag);
  if (std::string err = trg::DownloadFile(task, release.url, zip.u8string()); !err.empty()) return err;
  if (task.cancelled()) return "Cancelled.";

  task.SetLabel("Unpacking");
  task.Progress(-1, -1);
  wchar_t system_dir[MAX_PATH];
  GetSystemDirectoryW(system_dir, MAX_PATH);
  const std::wstring cmd = L"\"" + (fs::path(system_dir) / "tar.exe").wstring() + L"\" -xf \"" + zip.wstring() +
                           L"\" -C \"" + (work / "files").wstring() + L"\"";
  if (const int code = RunHidden(cmd); code != 0)
    return "Could not unpack the update (tar exit code " + std::to_string(code) + ").";

  // The new program: the folder holding the launcher inside the zip.
  fs::path from;
  for (auto& entry : fs::recursive_directory_iterator(work / "files", ec))
    if (entry.path().filename() == kLauncherExeName) {
      from = entry.path().parent_path();
      break;
    }
  if (from.empty() || !fs::exists(from / "game" / kGameExeName)) return "The update does not contain the game.";

  task.SetLabel("Installing");
  const fs::path to = U8(root);
  for (auto& entry : fs::recursive_directory_iterator(from, ec)) {
    if (!entry.is_regular_file()) continue;
    const fs::path rel = entry.path().lexically_relative(from);
    const fs::path target = to / rel;
    if (IsPlayerFile(rel) && fs::exists(target)) continue;
    fs::create_directories(target.parent_path(), ec);
    const fs::path old = target.wstring() + L".old";
    fs::remove(old, ec);
    const bool had = fs::exists(target);
    if (had) {
      fs::rename(target, old, ec);
      if (ec) return "Could not replace " + rel.u8string() + ": " + ec.message();
    }
    fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      if (had) fs::rename(old, target, ec);  // put the old one back
      return "Could not install " + rel.u8string() + ".";
    }
  }
  fs::remove_all(work, ec);
  return "";
#else
  (void)root;
  // AppRun passes the AppImage's own path on (and clears APPIMAGE for the game).
  const char* appimage = std::getenv("SW_APPIMAGE");
  if (!appimage || !*appimage) return "Download the new version from " + release.page;
  const fs::path target = appimage;
  const fs::path fresh = target.string() + ".new";
  fs::remove(fresh, ec);
  task.SetLabel("Downloading " + release.tag);
  if (std::string err = trg::DownloadFile(task, release.url, fresh.string()); !err.empty()) return err;
  if (task.cancelled()) return "Cancelled.";
  task.SetLabel("Installing");
  fs::permissions(fresh, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                             fs::perms::others_read | fs::perms::others_exec,
                  fs::perm_options::replace, ec);
  fs::rename(fresh, target, ec);
  if (ec) return "Could not replace " + target.filename().string() + ": " + ec.message();
  return "";
#endif
}

// Removes the *.old files the previous update left (they were still in use).
void CleanUpPreviousUpdate(const std::string& root) {
  std::error_code ec;
  if (const char* appimage = std::getenv("SW_APPIMAGE"); appimage && *appimage)
    fs::remove(fs::path(appimage).string() + ".new", ec);  // an interrupted AppImage update
  std::vector<fs::path> old;
  for (auto it = fs::recursive_directory_iterator(U8(root), ec); it != fs::recursive_directory_iterator();
       it.increment(ec)) {
    if (ec) break;
    if (it->is_directory() && (it->path().filename() == "saves" || it->path().filename() == "cache"))
      it.disable_recursion_pending();
    else if (it->is_regular_file() && it->path().extension() == ".old")
      old.push_back(it->path());
  }
  for (const fs::path& p : old) fs::remove(p, ec);
}

// Starts the (new) launcher again after an update. The test version is not
// passed on: the new launcher reports its real one.
void RelaunchLauncher(const std::string& root) {
#ifdef _WIN32
  SetEnvironmentVariableW(L"SW_UPDATE_TEST_VERSION", nullptr);
  const std::wstring exe = (U8(root) / kLauncherExeName).wstring();
  std::wstring cmd = L"\"" + exe + L"\" --launcher";
  STARTUPINFOW si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, U8(root).wstring().c_str(), &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
#else
  unsetenv("SW_UPDATE_TEST_VERSION");
  const char* appimage = std::getenv("SW_APPIMAGE");
  const std::string exe = appimage && *appimage ? appimage : root + kLauncherExeName;
  if (fork() == 0) {
    execl(exe.c_str(), exe.c_str(), "--launcher", static_cast<char*>(nullptr));
    _exit(127);
  }
#endif
}

// --------------------------------------------------------------- launcher ---

struct App {
  std::string root = ExeDir();      // the launcher's folder
  std::string dir = GameDir(root);  // the game, its data and every settings file
  trg::TextFileSettings settings{dir + "launcher.txt"};
  bool force_ready = false;
  std::string picked;  // a .cue chosen this session, before it is accepted
  // Controller assignments (input.ini [mapping]) and keyboard keys
  // (keybinds.ini, every [playerN]) as the Controls page shows them.
  std::map<std::string, std::string> pad_map, key_map;
  bool inputs_changed = false;
  bool expand_sections = false;  // --expand: open the Controls sections (screenshots)
  // Controllers seen while the launcher window was open. PLAY shuts SDL down
  // before the settings are written, and a fresh SDL can take longer than its
  // short probe to list a wireless pad.
  size_t pads_seen = 0;
  // Updates (About page; checked when the launcher opens).
  trg::Task update_check, update_install;
  std::optional<Release> update_found;  // written by the check Task, read after Finish()
  std::string update_error, update_status;
  bool manual_check = false;

  App() { LoadInputFiles(); }

  void LoadInputFiles() {
    const TomlEditor input(dir + "input.ini"), keys(dir + "keybinds.ini");
    for (const PadButton& b : kPadButtons) {
      const std::string pad = input.Get("mapping", b.key), key = keys.Get("player1", b.key);
      pad_map[b.key] = input.Exists() ? pad : b.pad;
      key_map[b.key] = keys.Exists() && !key.empty() ? key : b.keyboard;
    }
  }

  void ResetInputs() {
    for (const PadButton& b : kPadButtons) {
      pad_map[b.key] = b.pad;
      key_map[b.key] = b.keyboard;
    }
    inputs_changed = true;
  }

  // input.ini: vibration and the controller map. keybinds.ini: the keys, for
  // every player slot (the keyboard serves whichever player uses it), the
  // left-stick directions following the arrow keys.
  bool SaveInputFiles() const {
    if (!std::ifstream(dir + "input.ini")) {
      std::ofstream(dir + "input.ini", std::ios::binary) << kDefaultInputIni;
    }
    TomlEditor input(dir + "input.ini");
    input.Set("controller", "vibration", std::to_string(std::clamp(settings.GetInt("vibration", 100), 0, 100)));
    for (int i = 0; i < kRemappableOnController; ++i) {
      const auto it = pad_map.find(kPadButtons[i].key);
      if (it != pad_map.end()) input.Set("mapping", kPadButtons[i].key, it->second);
    }
    bool ok = input.Save();
    if (!inputs_changed && !std::ifstream(dir + "keybinds.ini")) return ok;
    TomlEditor keys(dir + "keybinds.ini");
    std::vector<std::string> slots = keys.SectionsStartingWith("player");
    if (slots.empty()) slots = {"player1", "player2"};
    for (const std::string& slot : slots)
      for (const PadButton& b : kPadButtons) {
        const std::string key = key_map.at(b.key);
        keys.Set(slot, b.key, key);
        const std::string dir_key = std::string(b.key);
        if (dir_key == "up" || dir_key == "down" || dir_key == "left" || dir_key == "right")
          keys.Set(slot, "ls_" + dir_key, key);
      }
    return keys.Save() && ok;
  }

  DiscCheck Disc() const { return CheckDisc(settings.Get("disc")); }
  bool Ready() const { return force_ready || Disc().ok; }

  void AcceptDisc(trg::Ui& ui, const std::string& path) {
    const DiscCheck check = CheckDisc(trg::AbsolutePath(path));
    if (!check.ok) {
      ui.SetStatus(check.detail, 8);
      return;
    }
    settings.Set("disc", trg::AbsolutePath(path));
    settings.Save();
    ui.SetStatus("Disc found. Press Play to start the game.", 6);
  }

  void PageGame(trg::Ui& ui) {
    const DiscCheck check = Disc();
    if (force_ready)
      ui.StatusCard(trg::Status::kReady, "Ready to play", "Preview mode: pretending the disc is set.");
    else if (check.ok)
      ui.StatusCard(trg::Status::kReady, "Ready to play", check.detail.c_str());
    else
      ui.StatusCard(trg::Status::kAttention, "Game disc needed", check.detail.c_str());

    ui.Row("Game disc",
           "Your own copy of The Simpsons Wrestling (USA) as a .cue/.bin disc image. You can also drop the .cue "
           "file onto this window. The game reads it where it is; nothing is copied.");
    if (ui.AccentButton(check.ok ? "Choose another disc..." : "Browse for the .cue file...")) {
      const std::string s = trg::BrowseForFile("Select The Simpsons Wrestling (USA) .cue file",
                                               {{"Disc image (.cue)", "*.cue"}});
      if (!s.empty()) AcceptDisc(ui, s);
    }
    ui.EndRows();
    ui.Spacer(6);
    ui.Paragraph(
        "Saves go to memory card files in the game's saves folder (About > Open save folder), so your progress "
        "carries over between sessions.");
  }

  static void PageDisplay(trg::Ui& ui) {
    ui.Choice("Window mode", "Borderless fills the screen at your desktop resolution. Alt+Enter switches while playing.",
              "window_mode", "borderless",
              {{"windowed", "Windowed"}, {"borderless", "Borderless"}, {"fullscreen", "Exclusive"}});
    ui.Combo("Window size", "The size of the window in windowed mode.", "window_width", "1920",
             {{"1280", "1280 \xC3\x97 720"}, {"1600", "1600 \xC3\x97 900"}, {"1920", "1920 \xC3\x97 1080"},
              {"2560", "2560 \xC3\x97 1440"}, {"3840", "3840 \xC3\x97 2160"}});
    ui.Combo("Monitor", "The screen the game opens on.", "monitor", "0", MonitorOptions());
    ui.Toggle("Widescreen", "16:9 in matches: the arena and crowd fill the sides. Menus and loading screens keep "
              "the original 4:3 picture.",
              "widescreen", true);
    ui.Toggle("Fill the screen",
              "Off keeps the picture's shape, with black bars on screens that are not 16:9 (or 4:3 in menus). On "
              "stretches it to fill the whole screen.",
              "stretch", false);
    ui.Choice("VSync", "Waits for the display before showing a frame, which stops tearing.", "vsync", "on",
              {{"off", "Off"}, {"on", "On"}, {"adaptive", "Adaptive"}});
  }

  static void PageGraphics(trg::Ui& ui) {
    ui.Combo("Render resolution",
             "The game is drawn at this resolution, then scaled to the window. Above your screen's resolution it "
             "smooths every edge (supersampling).",
             "internal_resolution", "display",
             {{"display", "Match my screen (recommended)"}, {"native", "Original (240p)"}, {"720p", "720p"},
              {"1080p", "1080p"}, {"1440p", "1440p"}, {"4k", "4K"}, {"5k", "5K"}, {"8k", "8K"}});
    ui.Toggle("Smooth outlines", "Anti-aliasing for the characters' black outlines and every other edge (FXAA after "
              "the supersampled image is scaled down).",
              "smooth_outlines", true);
    ui.Toggle("Smooth textures", "Bilinear filtering. Off keeps the original sharp texture pixels.",
              "texture_filtering", false);
    ui.Toggle("Stable geometry", "Sub-pixel vertex precision: stops the PlayStation's polygon wobble.",
              "geometry_correction", true);
    ui.SliderInt("Sharpening", "Contrast-adaptive sharpening of the final picture.", "sharpen", 0, 0, 100, "%d%%", 5);
    ui.SliderInt("Brightness", "100% is the original picture.", "brightness", 100, 50, 150, "%d%%", 5);
    ui.Choice("Screen filter", "Makes the picture look like it is on an old TV.", "screen_filter", "off",
              {{"off", "Off"}, {"crt", "CRT"}, {"composite", "Composite"}, {"trinitron", "Trinitron"}});
    ui.SliderInt("Scanlines", "Dark lines between the picture's rows, like a CRT. 0% is off.", "scanlines", 0, 0, 100,
                 "%d%%", 5);
  }

  static void PageGameplay(trg::Ui& ui) {
    ui.Choice("Frame rate",
              "60 FPS draws every frame the PlayStation dropped during matches, at the original game speed. 30 FPS "
              "is the original. 120 FPS (experimental) adds an in-between frame that the game draws itself, on a "
              "120 Hz or faster display; it needs a lot of CPU, works best at a low internal resolution and pauses "
              "itself whenever the game would slow down.",
              "frame_rate", "60",
              {{"30", "30 FPS (original)"}, {"60", "60 FPS"}, {"120", "120 FPS (experimental)"}});
    ui.Toggle("Skip intro", "Starts at the title screen: no copyright card, Fox Interactive or Big Ape logos.",
              "skip_intro", true);
    ui.Toggle("Unlock everything",
              "All wrestlers, the Defender and Champion circuits and Bonus Match Up, from the start.", "unlock_all",
              true);
    ui.Toggle("Fast loading",
              "Loading screens run up to four times faster. The game itself is untouched: everything happens in the "
              "same order, just sooner.",
              "fast_loading", true);
    ui.Toggle("Frame rate counter", "Shows the game and display frame rates in the window title.", "fps_counter",
              false, "Hidden", "Shown");
  }

  void PageControls(trg::Ui& ui) {
    ui.Choice("Player 1",
              "Automatic uses a controller as soon as one is switched on, even after the game has started. The "
              "keyboard works too, unless player 2 is on it.",
              "p1_input", "auto", {{"auto", "Automatic"}, {"controller", "Controller"}, {"keyboard", "Keyboard"}});
    ui.Choice("Player 2",
              "For VS matches. Automatic uses a second controller, or the keyboard when player 1 has a controller. "
              "It counts the controllers that are on when you press Play.",
              "p2_input", "auto",
              {{"auto", "Automatic"}, {"controller", "Controller"}, {"keyboard", "Keyboard"}, {"none", "Off"}});
    const std::vector<std::string> pads = ConnectedControllers();
    std::string list;
    for (size_t i = 0; i < pads.size(); ++i) list += (i ? "\n" : "") + std::to_string(i + 1) + ". " + pads[i];
    ui.Info("Connected controllers", pads.empty() ? "None found. Switch one on or plug it in; the game picks it up when it connects."
                                                  : list.c_str());
    ui.SliderInt("Vibration", "Controller rumble strength; 0% switches it off. The game starts with its own Vibration option off: turn it on in the game's Options menu.", "vibration", 100, 0,
                 100, "%d%%", 10);
    ui.SliderInt("Stick deadzone", "How far a stick must move before it counts. Raise it if a worn stick drifts.",
                 "deadzone", 10, 0, 50, "%d%%", 1);
    ui.EndRows();

    if (ui.Section("Controller buttons", expand_sections)) {
      ui.Help("Which controller button presses each PlayStation button. The d-pad and left stick always move.");
      for (int i = 0; i < kRemappableOnController; ++i) {
        const PadButton& b = kPadButtons[i];
        ui.Row(b.label);
        std::string& current = pad_map[b.key];
        ImGui::PushID(b.key);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##pad", PadInputLabel(current), ImGuiComboFlags_HeightLarge)) {
          for (const PadInput& in : kPadInputs)
            if (ImGui::Selectable(in.label, current == in.name)) {
              current = in.name;
              inputs_changed = true;
            }
          ImGui::EndCombo();
        }
        ImGui::PopID();
      }
      if (ui.ButtonRow("Defaults", "Put every controller button back.", "Reset controller buttons")) {
        for (const PadButton& b : kPadButtons) pad_map[b.key] = b.pad;
        inputs_changed = true;
      }
      ui.EndRows();
    }
    if (ui.Section("Keyboard keys", expand_sections)) {
      ui.Help("Click a key, then press the new one (Esc cancels). Used by whichever player is on the keyboard.");
      for (const PadButton& b : kPadButtons) {
        if (const std::optional<ImGuiKey> pressed = ui.KeyBindRow(b.label, nullptr, key_map[b.key])) {
          const std::string name = KeyNameForImGuiKey(*pressed);
          if (name.empty()) {
            ui.SetStatus("That key cannot be used for the game.", 4);
          } else {
            key_map[b.key] = name;
            inputs_changed = true;
          }
        }
      }
      if (ui.ButtonRow("Defaults", "Put every keyboard key back.", "Reset keyboard keys")) {
        for (const PadButton& b : kPadButtons) key_map[b.key] = b.keyboard;
        inputs_changed = true;
      }
      ui.EndRows();
    }
  }

  static void PageSound(trg::Ui& ui) {
    ui.SliderInt("Volume", "The game's overall volume. The numpad + and - keys change it while you play.", "volume",
                 100, 0, 100, "%d%%", 5);
    ui.Choice("Sound delay",
              "How much sound is buffered ahead. Low makes hits and music line up best with the picture; Safe "
              "rides out stutters on slow or busy PCs without crackling.",
              "audio_latency", "low", {{"low", "Low"}, {"normal", "Normal"}, {"safe", "Safe"}});
    ui.Toggle("High-quality audio", "Smoother resampling of the PlayStation's sound chip, for a little more CPU.",
              "spu_hq", false);
  }

  // The settings.toml device names for players 1 and 2.
  // Automatic player 1 is routed to a controller even when none is on yet: the
  // game opens one the moment it connects (a pad switched on after PLAY used
  // to be ignored for the whole session), and player 1's keyboard keys stay
  // live beside it while no other player is on the keyboard.
  std::pair<std::string, std::string> InputDevices() const {
    const size_t pads = std::max(pads_seen, ConnectedControllers().size());
    const std::string c1 = settings.Get("p1_input", "auto"), c2 = settings.Get("p2_input", "auto");
    const std::string p1 = c1 == "keyboard" ? "keyboard" : "gamepad";
    const size_t pads_for_p2 = p1 == "gamepad" ? (pads > 0 ? pads - 1 : 0) : pads;
    std::string p2 = c2 == "controller" ? "gamepad" : c2 == "keyboard" ? "keyboard" : c2 == "none" ? "none"
                   : pads_for_p2 >= 1 ? "gamepad" : p1 == "gamepad" && pads >= 1 ? "keyboard" : "none";
    return {p1, p2};
  }

  void PageAbout(trg::Ui& ui) {
    ui.Paragraph(
        "The Simpsons Wrestling, recompiled from the PlayStation game into a native PC program with "
        "psxrecomp. It runs from your own copy of the game: no game data is included.");
    ui.Spacer(4);
    ui.LauncherVisibilityRow();
    if (CanUpdate()) {
      ui.Choice("Updates",
                "When the launcher opens, look for a newer version of this port on GitHub. Ask shows a pop-up first; "
                "Automatic installs it straight away. Your settings and saves are kept. Only GitHub is contacted, and "
                "only when the launcher opens.",
                "updates", "ask", {{"ask", "Ask"}, {"auto", "Automatic"}, {"off", "Off"}});
      if (!ui.TaskProgress(update_install) &&
          ui.ButtonRow("Check for updates", update_status.empty() ? nullptr : update_status.c_str(),
                       update_check.running() ? "Checking..." : "Check now") &&
          !update_check.running()) {
        manual_check = true;
        StartUpdateCheck();
      }
    }
    ui.FolderRow("Save data", "Your memory card files.", "Open save folder", dir + "saves");
    ui.FolderRow("Game folder", "The game program and its settings (launcher.txt, settings.toml, input.ini, "
                 "keybinds.ini).",
                 "Open game folder", dir);
    ui.ResetAllRow();
    const std::string version = CurrentVersion();
    ui.Info("Version", version.empty() ? "unknown" : version.c_str());
  }

  // ------------------------------------------------------------ updates ---
  // This copy's version (the game's version stamp). SW_UPDATE_TEST_VERSION
  // makes it look older, to try an update against the live release.
  std::string CurrentVersion() const {
    if (const char* test = std::getenv("SW_UPDATE_TEST_VERSION"); test && *test) return test;
    return ReadFirstLine(dir + "psx_game_version.txt");
  }

  // Updates replace a release download's files: the Windows release layout
  // (game/ beside the launcher) or the AppImage. A development build is left
  // alone.
  bool CanUpdate() const {
#ifdef _WIN32
    return dir != root;
#else
    const char* appimage = std::getenv("SW_APPIMAGE");
    return appimage && *appimage;
#endif
  }

  void StartUpdateCheck() {
    const std::string current = CurrentVersion();
    update_check.Start("Checking for updates", [this, current](trg::Task&) {
      std::string error;
      update_found = CheckLatest(current, &error);
      update_error = error;
      return std::string();
    });
  }

  void StartUpdateInstall(trg::Launcher& l) {
    if (!update_found || update_install.running()) return;
    l.GoToPage(l.FindPage("About"));
    const Release release = *update_found;
    const std::string to = root;
    update_install.Start("Updating", [release, to](trg::Task& t) { return InstallUpdate(t, release, to); });
  }

  // Each frame: reports a finished check (a pop-up, or installs straight away
  // with Updates on Automatic) and a finished install. Returns true when the
  // new version is in place and the launcher should restart.
  bool PollUpdates(trg::Launcher& l) {
    if (update_check.Finish() != trg::Task::State::kIdle) {
      if (update_found) {
        update_status = "Version " + update_found->tag + " is available.";
        if (settings.Get("updates", "ask") == "auto") {
          StartUpdateInstall(l);
        } else {
          trg::PlayPrompt p;
          p.title = "Update available: " + update_found->tag;
          p.paragraphs = {"A newer version of The Simpsons Wrestling PC port is available (you have " +
                              CurrentVersion() + ").",
                          "Update now downloads it from GitHub, installs it and restarts the launcher. Your "
                          "settings and saves are kept."};
          p.footnote = update_found->page;
          trg::PromptButton now;
          now.label = "Update now";
          now.accent = true;
          now.play = false;
          now.action = [this, &l] { StartUpdateInstall(l); };
          trg::PromptButton later;
          later.label = "Later";
          later.play = false;
          p.buttons = {now, later};
          l.ShowPrompt(std::move(p));
        }
      } else {
        update_status = update_error.empty() ? "You have the latest version." : "Could not check: " + update_error;
        if (manual_check) l.SetStatus(update_status);
      }
      manual_check = false;
    }
    std::string error;
    switch (update_install.Finish(&error)) {
      case trg::Task::State::kDone: return true;
      case trg::Task::State::kFailed: l.SetStatus("Update failed: " + error, 10); break;
      case trg::Task::State::kCancelled: l.SetStatus("Update cancelled."); break;
      default: break;
    }
    return false;
  }

  // The runtime's own settings, from the launcher's choices.
  bool WriteRuntimeSettings() const {
    TomlEditor toml(dir + "settings.toml");
    const std::string mode = settings.Get("window_mode", "borderless");
    toml.Set("video", "fullscreen", mode == "fullscreen" ? "2" : mode == "borderless" ? "1" : "0");
    toml.Set("video", "window_width", std::to_string(std::clamp(settings.GetInt("window_width", 1920), 640, 7680)));
    toml.Set("video", "vsync", Quote(settings.Get("vsync", "on")));
    toml.Set("video", "internal_resolution", Quote(settings.Get("internal_resolution", "display")));
    const bool smooth = settings.GetBool("smooth_outlines", true);
    toml.Set("video", "antialiasing", smooth ? "true" : "false");
    toml.Set("video", "fxaa", smooth ? "true" : "false");
    toml.Set("video", "texture_filtering", Quote(settings.GetBool("texture_filtering", false) ? "bilinear" : "nearest"));
    const bool stable = settings.GetBool("geometry_correction", true);
    toml.Set("video", "geometry_correction", stable ? "true" : "false");
    toml.Set("video", "perspective_texturing", stable ? "true" : "false");
    toml.Set("video", "sharpen", std::to_string(std::clamp(settings.GetInt("sharpen", 0), 0, 100)));
    toml.Set("video", "brightness", std::to_string(std::clamp(settings.GetInt("brightness", 100), 50, 150)));
    toml.Set("video", "fps_counter", settings.GetBool("fps_counter", false) ? "true" : "false");
    toml.Set("video", "monitor", std::to_string(std::max(0, settings.GetInt("monitor", 0))));
    toml.Set("video", "stretch", settings.GetBool("stretch", false) ? "true" : "false");
    const std::string filter = settings.Get("screen_filter", "off");
    toml.Set("video", "crt_filter", Quote(filter == "off" ? "raw" : filter));
    const int scanlines = std::clamp(settings.GetInt("scanlines", 0), 0, 100);
    toml.Set("video", "scanlines", scanlines > 0 ? "true" : "false");
    char strength[16];
    std::snprintf(strength, sizeof strength, "%.2f", scanlines / 100.0);
    toml.Set("video", "scanline_strength", strength);
    toml.Set("audio", "volume", std::to_string(std::clamp(settings.GetInt("volume", 100), 0, 100)));
    const std::string delay = settings.Get("audio_latency", "low");
    toml.Set("audio", "latency_ms", delay == "safe" ? "180" : delay == "normal" ? "150" : "120");
    toml.Set("audio", "spu_hq", settings.GetBool("spu_hq", false) ? "true" : "false");
    // Widescreen and the frame rate belong to the game's plugin (launcher.txt);
    // the generic presenter blend stays off so frames are never doubled.
    toml.Set("video", "frame_interpolation", "false");
    // Release runtimes default player 1 to the keyboard only; the launcher
    // assigns the devices (each controller goes to one player at most).
    const auto [p1, p2] = InputDevices();
    toml.Set("controller", "p1_device", Quote(p1));
    toml.Set("controller", "p2_device", Quote(p2));
    toml.Set("controller", "deadzone",
             std::to_string(std::clamp(settings.GetInt("deadzone", 10), 0, 50) * 32767 / 100));
    toml.Set("launcher", "skip_launcher", "true");
    return toml.Save() && SaveInputFiles();
  }

  // Starts the game and returns at once; the launcher then exits.
  bool StartGame(std::string* error) const {
    const std::string exe = dir + kGameExeName;
    if (trg::FileSize(exe) <= 0) {
      *error = "The game program is missing: " + exe;
      return false;
    }
    const std::string disc = settings.Get("disc");
#ifdef _WIN32
    std::string cmd = "\"" + exe + "\" --no-launcher --disc \"" + disc + "\"";
    std::wstring wcmd(size_t(MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, nullptr, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, wcmd.data(), int(wcmd.size()));
    std::wstring wdir(size_t(MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, wdir.data(), int(wdir.size()));
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, FALSE, 0, nullptr, wdir.c_str(), &si, &pi)) {
      *error = "Windows could not start the game (error " + std::to_string(GetLastError()) + ").";
      return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    const pid_t pid = fork();
    if (pid < 0) {
      *error = "Could not start the game.";
      return false;
    }
    if (pid == 0) {
      if (chdir(dir.c_str()) != 0) _exit(127);
      execl(exe.c_str(), exe.c_str(), "--no-launcher", "--disc", disc.c_str(), static_cast<char*>(nullptr));
      _exit(127);
    }
    return true;
#endif
  }
};

}  // namespace

int main(int argc, char** argv) {
  App app;
  std::string screenshot;
  int start_page = -1;
  bool force_launcher = false;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--page") && i + 1 < argc) start_page = std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) screenshot = argv[++i];
    else if (!std::strcmp(argv[i], "--ready")) app.force_ready = true;
    else if (!std::strcmp(argv[i], "--launcher")) force_launcher = true;
    else if (!std::strcmp(argv[i], "--expand")) app.expand_sections = true;
  }

  const bool show = !screenshot.empty() || trg::ShouldShowLauncher(app.settings, "launcher", app.Ready(), force_launcher);
  if (!show) {
    // Launcher hidden: straight into the game with the saved settings.
    std::string error;
    if (app.WriteRuntimeSettings() && app.StartGame(&error)) return 0;
  }

  trg::LauncherConfig config;
  config.branding.title = "THE SIMPSONS WRESTLING";
  config.branding.subtitle = "RECOMPILED   \xC2\xB7   LAUNCHER";
  config.branding.background = SpringfieldSky();
  config.branding.shade_title_side = true;
  config.branding.fade_into_page = false;
  config.branding.subtitle_color = ImVec4(1.0f, 0.85f, 0.06f, 1.0f);
  config.branding.title_shadow = IM_COL32(10, 40, 90, 170);
  config.theme = trg::Theme::Ocean().WithAccent(ImVec4(0.996f, 0.851f, 0.059f, 1.0f));  // Simpsons yellow
  config.settings = &app.settings;
  config.pages = {
      {"Game", "Point the launcher at your own copy of the game.", [&](trg::Ui& ui) { app.PageGame(ui); },
       [&] { return !app.Ready(); }},
      {"Display", "Window, widescreen and how the picture reaches your screen.", App::PageDisplay},
      {"Graphics", "HD rendering, smooth outlines and the final picture.", App::PageGraphics},
      {"Gameplay", "Frame rate, the intro and what is unlocked.", App::PageGameplay},
      {"Controls", "Controllers and the keyboard for players 1 and 2.", [&](trg::Ui& ui) { app.PageControls(ui); }},
      {"Sound", "Volume and how closely the sound follows the picture.", App::PageSound},
      {"About", "About this port, and where your settings live.", [&](trg::Ui& ui) { app.PageAbout(ui); }},
  };
  config.start_page = start_page >= 0 ? start_page : app.Ready() ? 1 : 0;
  config.can_play = [&] {
    if (app.update_install.running()) return trg::PlayCheck{false, "Wait for the update to finish.", 0};
    if (!app.Ready()) return trg::PlayCheck{false, "Select your game disc first.", 0};
    return trg::PlayCheck{};
  };
  config.on_file_drop = [&](const std::string& path) { app.picked = path; };
  config.on_save = [&] { return app.SaveInputFiles(); };
  config.on_reset = [&] {
    app.ResetInputs();
    app.SaveInputFiles();
  };
  // 120 FPS is experimental: say what to expect and recommend 60, but let the
  // player go ahead.
  config.before_play = [&]() -> std::optional<trg::PlayPrompt> {
    if (app.settings.Get("frame_rate", "60") != "120") return std::nullopt;
    trg::PlayPrompt prompt;
    prompt.title = "120 FPS is experimental";
    prompt.paragraphs = {
        "You may see graphical issues at 120 FPS: the ring spotlight can flicker, and motion may not look "
        "perfectly even.",
        "It needs a lot of CPU. On most PCs only some frames get an extra in-between frame, and at high render "
        "resolutions it mostly falls back to 60 FPS on its own.",
        "60 FPS is recommended."};
    prompt.footnote = "You can change the frame rate any time on the Gameplay page.";
    trg::PromptButton sixty;
    sixty.label = "Play at 60 FPS (recommended)";
    sixty.accent = true;
    sixty.action = [&] {
      app.settings.Set("frame_rate", "60");
      app.settings.Save();
    };
    trg::PromptButton anyway;
    anyway.label = "Play at 120 FPS";
    prompt.buttons = {sixty, anyway};
    return prompt;
  };

  trg::Launcher launcher(std::move(config));
  const std::vector<uint32_t> icon = DonutIcon(128);
  CleanUpPreviousUpdate(app.root);
  if (screenshot.empty() && app.CanUpdate() && app.settings.Get("updates", "ask") != "off") app.StartUpdateCheck();

  trg::WindowOptions window;
  window.title = "The Simpsons Wrestling - Launcher";
  trg::StandaloneHooks hooks;
  hooks.on_start = [&](trg::Launcher& l) { l.config().branding.icon = trg::CreateTextureRGBA(128, 128, icon.data()); };
  hooks.on_stop = [&](trg::Launcher& l) { trg::DestroyTexture(l.config().branding.icon); };
  int frames = 0, pad_polls = 0;
  hooks.after_render = [&](trg::Launcher& l, int w, int h) {
    if (pad_polls++ % 30 == 0) app.pads_seen = ConnectedControllers().size();
    if (app.PollUpdates(l)) {  // the new version is in place: start it and close this one
      RelaunchLauncher(app.root);
      l.RequestQuit();
      return;
    }
    if (!app.picked.empty()) {  // a dropped file: check it on the UI thread
      const std::string p = std::move(app.picked);
      app.picked.clear();
      const DiscCheck check = CheckDisc(trg::AbsolutePath(p));
      if (check.ok) {
        app.settings.Set("disc", trg::AbsolutePath(p));
        app.settings.Save();
        l.SetStatus("Disc found. Press Play to start the game.", 6);
      } else {
        l.SetStatus(check.detail, 8);
      }
    }
    if (!screenshot.empty() && ++frames == 45) {
      WritePpm(screenshot.c_str(), w, h);
      l.RequestQuit();
    }
  };

  if (trg::RunStandalone(launcher, window, hooks) != trg::Result::kPlay) return 0;
  std::string error;
  if (!app.WriteRuntimeSettings()) error = "Could not write settings.toml in " + app.dir;
  if (error.empty() && app.StartGame(&error)) return 0;
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "The Simpsons Wrestling", error.c_str(), nullptr);
  return 1;
}

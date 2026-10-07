// The Simpsons Wrestling Recompiled: the TRG Launcher in front of the game.
//
// Players open this program. It keeps its own settings in launcher.txt, writes
// the runtime's [video] settings into settings.toml, and on PLAY starts the
// recompiled game (SimpsonsWrestling_Recompiled) with the chosen disc. The PC
// features the game's plugin applies (simpsons_mods.c) read launcher.txt too.
//
//   SimpsonsWrestling [--page N] [--ready] [--screenshot out.ppm]
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

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

// Sets `key = value` in [section] of the runtime's settings.toml, keeping
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

// --------------------------------------------------------------- launcher ---

struct App {
  std::string dir = ExeDir();
  trg::TextFileSettings settings{dir + "launcher.txt"};
  bool force_ready = false;
  std::string picked;  // a .cue chosen this session, before it is accepted

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
        "Saves go to the saves folder next to this program as memory card files, so your progress carries over "
        "between sessions.");
  }

  static void PageDisplay(trg::Ui& ui) {
    ui.Choice("Window mode", "Borderless fills the screen at your desktop resolution. Alt+Enter switches while playing.",
              "window_mode", "borderless",
              {{"windowed", "Windowed"}, {"borderless", "Borderless"}, {"fullscreen", "Exclusive"}});
    ui.Combo("Window size", "The size of the window in windowed mode.", "window_width", "1920",
             {{"1280", "1280 \xC3\x97 720"}, {"1600", "1600 \xC3\x97 900"}, {"1920", "1920 \xC3\x97 1080"},
              {"2560", "2560 \xC3\x97 1440"}, {"3840", "3840 \xC3\x97 2160"}});
    ui.Toggle("Widescreen", "16:9 in matches: the arena and crowd fill the sides. Menus and loading screens keep "
              "the original 4:3 picture.",
              "widescreen", true);
    ui.Choice("VSync", "Waits for the display before showing a frame, which stops tearing.", "vsync", "on",
              {{"off", "Off"}, {"on", "On"}, {"adaptive", "Adaptive"}});
  }

  static void PageGraphics(trg::Ui& ui) {
    ui.Combo("Render resolution",
             "The game is drawn at this resolution, then scaled to the window. Above your screen's resolution it "
             "smooths every edge (supersampling).",
             "internal_resolution", "4k",
             {{"native", "Original (240p)"}, {"720p", "720p"}, {"1080p", "1080p"}, {"1440p", "1440p"},
              {"4k", "4K (recommended)"}, {"5k", "5K"}, {"8k", "8K"}, {"display", "Match my screen"}});
    ui.Toggle("Smooth outlines", "Anti-aliasing for the characters' black outlines and every other edge (FXAA after "
              "the supersampled image is scaled down).",
              "smooth_outlines", true);
    ui.Toggle("Smooth textures", "Bilinear filtering. Off keeps the original sharp texture pixels.",
              "texture_filtering", false);
    ui.Toggle("Stable geometry", "Sub-pixel vertex precision: stops the PlayStation's polygon wobble.",
              "geometry_correction", true);
    ui.SliderInt("Sharpening", "Contrast-adaptive sharpening of the final picture.", "sharpen", 0, 0, 100, "%d%%", 5);
    ui.SliderInt("Brightness", "100% is the original picture.", "brightness", 100, 50, 150, "%d%%", 5);
  }

  static void PageGameplay(trg::Ui& ui) {
    ui.Choice("Frame rate",
              "60 FPS draws every frame the PlayStation dropped during matches, at the original game speed. 30 FPS "
              "is the original.",
              "frame_rate", "60", {{"30", "30 FPS (original)"}, {"60", "60 FPS"}});
    ui.Toggle("Skip intro", "Starts at the title screen: no copyright card, Fox Interactive or Big Ape logos.",
              "skip_intro", true);
    ui.Toggle("Unlock everything",
              "All wrestlers, the Defender and Champion circuits and Bonus Match Up, from the start.", "unlock_all",
              true);
    ui.Toggle("Frame rate counter", "Shows the game and display frame rates in the window title.", "fps_counter",
              false, "Hidden", "Shown");
  }

  void PageAbout(trg::Ui& ui) {
    ui.Paragraph(
        "The Simpsons Wrestling, recompiled from the PlayStation game into a native PC program with "
        "psxrecomp. It runs from your own copy of the game: no game data is included.");
    ui.Spacer(4);
    ui.LauncherVisibilityRow();
    ui.FolderRow("Game folder", "Settings, saves and the game program.", "Open game folder", dir);
    ui.ResetAllRow();
    ui.Info("Version", "1.0.0");
  }

  // The runtime's own settings, from the launcher's choices.
  bool WriteRuntimeSettings() const {
    TomlEditor toml(dir + "settings.toml");
    const std::string mode = settings.Get("window_mode", "borderless");
    toml.Set("video", "fullscreen", mode == "fullscreen" ? "2" : mode == "borderless" ? "1" : "0");
    toml.Set("video", "window_width", std::to_string(std::clamp(settings.GetInt("window_width", 1920), 640, 7680)));
    toml.Set("video", "vsync", Quote(settings.Get("vsync", "on")));
    toml.Set("video", "internal_resolution", Quote(settings.Get("internal_resolution", "4k")));
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
    // Widescreen and the frame rate belong to the game's plugin (launcher.txt);
    // the generic presenter blend stays off so frames are never doubled.
    toml.Set("video", "frame_interpolation", "false");
    toml.Set("launcher", "skip_launcher", "true");
    return toml.Save();
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
      {"About", "About this port, and where your settings live.", [&](trg::Ui& ui) { app.PageAbout(ui); }},
  };
  config.start_page = start_page >= 0 ? start_page : app.Ready() ? 1 : 0;
  config.can_play = [&] {
    if (!app.Ready()) return trg::PlayCheck{false, "Select your game disc first.", 0};
    return trg::PlayCheck{};
  };
  config.on_file_drop = [&](const std::string& path) { app.picked = path; };

  trg::Launcher launcher(std::move(config));
  const std::vector<uint32_t> icon = DonutIcon(128);

  trg::WindowOptions window;
  window.title = "The Simpsons Wrestling - Launcher";
  trg::StandaloneHooks hooks;
  hooks.on_start = [&](trg::Launcher& l) { l.config().branding.icon = trg::CreateTextureRGBA(128, 128, icon.data()); };
  hooks.on_stop = [&](trg::Launcher& l) { trg::DestroyTexture(l.config().branding.icon); };
  int frames = 0;
  hooks.after_render = [&](trg::Launcher& l, int w, int h) {
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

#include <backends/imgui_impl_win32.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <map>
#include <string>
#include <vector>

struct ItemRect {
  ImVec2 min, max;
};
static std::map<std::string, ItemRect> items;
static std::vector<std::string> text_lines;
static ImVec2 pointer{-100, -100};
static bool pressed = false;
static void synthetic_frame();
namespace ImGui {
bool observed_menu(const char *label, bool enabled = true);
bool observed_item(const char *label, const char *shortcut = nullptr,
                   bool selected = false, bool enabled = true);
bool observed_button(const char *label, const ImVec2 &size = ImVec2(0, 0));
void observed_text(const char *text, const char *end = nullptr);
} // namespace ImGui
#define ImGui_ImplWin32_NewFrame synthetic_frame
#define BeginMenu observed_menu
#define MenuItem observed_item
#define Button observed_button
#define TextUnformatted observed_text
#define wmain viewer_entrypoint
#include "../src/viewer/main.cpp"
#undef wmain
#undef ImGui_ImplWin32_NewFrame
#undef BeginMenu
#undef MenuItem
#undef Button
#undef TextUnformatted

static void record(const char *label) {
  items[label] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
}
bool ImGui::observed_menu(const char *label, bool enabled) {
  bool open = BeginMenu(label, enabled);
  if (!open)
    record(label);
  return open;
}
bool ImGui::observed_item(const char *label, const char *shortcut,
                          bool selected, bool enabled) {
  bool activated = MenuItem(label, shortcut, selected, enabled);
  record(label);
  return activated;
}
bool ImGui::observed_button(const char *label, const ImVec2 &size) {
  bool activated = Button(label, size);
  record(label);
  return activated;
}
void ImGui::observed_text(const char *text, const char *end) {
  text_lines.emplace_back(text, end ? end : text + std::strlen(text));
  TextUnformatted(text, end);
}
static void synthetic_frame() {
  ImGui_ImplWin32_NewFrame();
  auto &io = ImGui::GetIO();
  io.AddFocusEvent(true);
  io.AddMousePosEvent(pointer.x, pointer.y);
  io.AddMouseButtonEvent(0, pressed);
  text_lines.clear();
}
static void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
int wmain(int argc, wchar_t **argv) {
  try {
    App app(true);
    require(!IsWindowVisible(app.graphics.window),
            "About test opened a window");
    auto &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.ConfigInputTrickleEventQueue = false;
    io.FontGlobalScale = 1;
    auto image = sv::embedded_icon();
    require(image.width == 256 && image.height == 256,
            "About image was not embedded at its expected size");
    size_t transparent = 0, opaque = 0;
    for (size_t i = 3; i < image.rgba.size(); i += 4) {
      transparent += image.rgba[i] == 0;
      opaque += image.rgba[i] == 255;
    }
    require(transparent > 100 && opaque > 100,
            "Embedded icon lost transparency or opaque artwork");
    auto module = GetModuleHandleW(nullptr);
    auto group =
        FindResourceW(module, MAKEINTRESOURCEW(IDI_VIEWER), RT_GROUP_ICON);
    require(group, "Executable has no icon group");
    auto *directory =
        static_cast<const uint8_t *>(LockResource(LoadResource(module, group)));
    uint16_t count;
    std::memcpy(&count, directory + 4, 2);
    require(count == 10, "Executable lost its icon sizes");
    for (int size : {16, 20, 24, 32, 40, 48, 64, 96, 128, 256}) {
      auto icon = static_cast<HICON>(LoadImageW(
          module, MAKEINTRESOURCEW(IDI_VIEWER), IMAGE_ICON, size, size, 0));
      require(icon, "Cannot load a native application icon size");
      DestroyIcon(icon);
    }
    require(GetClassLongPtrW(app.graphics.window, GCLP_HICON) &&
                SendMessageW(app.graphics.window, WM_GETICON, ICON_SMALL, 0),
            "Viewer window did not use the embedded application icon");
    auto click = [&](const char *label) {
      auto r = items.at(label);
      pointer = {(r.min.x + r.max.x) * .5f, (r.min.y + r.max.y) * .5f};
      pressed = false;
      app.frame();
      pressed = true;
      app.frame();
      pressed = false;
      app.frame();
    };
    auto modal = [&]() {
      auto *window = ImGui::FindWindowByName(sv::about_title);
      return window && window->Active &&
             (window->Flags & ImGuiWindowFlags_Modal);
    };
    app.frame();
    app.frame();
    require(items.at("File").max.x <= items.at("Help").min.x,
            "Help was not placed beside File");
    click("Help");
    app.frame();
    require(items.contains("About"), "Help menu omitted About");
    click("About");
    app.frame();
    require(modal(), "Help > About did not open a modal without game files");
    for (const char *line : {"Shaolin Monks Viewer", "3D Asset Viewer",
                             sv::build_label, "Developed by RatonMalo"})
      require(std::find(text_lines.begin(), text_lines.end(), line) !=
                  text_lines.end(),
              "About dialog omitted its project/build/developer information");
    for (float scale : {1.f, 1.5f, 2.f}) {
      if (scale != 1) {
        click("OK");
        app.frame();
        io.FontGlobalScale = scale;
        app.about_requested = true;
        app.frame();
        app.frame();
      }
      auto *window = ImGui::FindWindowByName(sv::about_title);
      require(modal() && window->Pos.x >= 0 && window->Pos.y >= 0 &&
                  window->Pos.x + window->Size.x <= io.DisplaySize.x + 1 &&
                  window->Pos.y + window->Size.y <= io.DisplaySize.y + 1,
              "About window left the viewport at a supported UI scale");
      auto ok = items.at("OK");
      require(ok.min.y >= window->Pos.y &&
                  ok.max.y <= window->Pos.y + window->Size.y &&
                  window->ScrollMax.y == 0,
              "About contents or close button were clipped");
      bool icon_drawn = false;
      for (auto &cmd : window->DrawList->CmdBuffer)
        icon_drawn |= cmd.GetTexID() ==
                      reinterpret_cast<ImTextureID>(app.about_icon.Get());
      require(icon_drawn, "About dialog did not render the embedded emblem");
      if (argc == 2) {
        fs::create_directories(argv[1]);
        sv::save_png(
            app.graphics.capture(),
            fs::path(argv[1]) /
                ("about-" + std::to_string(int(scale * 100)) + ".png"));
      }
    }
    click("OK");
    app.frame();
    require(!modal(), "OK failed to dismiss About");
    click("Help");
    click("About");
    app.frame();
    require(modal(), "About could not reopen after closing");
    std::cout << "Help/About interaction, embedded icon sizes and 100/150/200% "
                 "layout passed; hidden WARP, silent audio, synthetic input\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

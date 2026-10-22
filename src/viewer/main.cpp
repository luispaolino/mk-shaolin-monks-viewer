#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "character_debug.h"
#include "renderer.h"
#include <algorithm>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <cctype>
#include <chrono>
#include <cmath>
#include <commdlg.h>
#include <cstring>
#include <fstream>
#include <future>
#include <imgui.h>
#include <iostream>
#include <map>
#include <shellapi.h>
#include <sstream>
#include <stdexcept>
#include <windows.h>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM,
                                                             LPARAM);
namespace sv = shaolin::viewer;
namespace fs = std::filesystem;
namespace {
constexpr wchar_t title[] = L"Shaolin Monks Recomp - 3D Viewer";
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
  if (ImGui::GetCurrentContext() &&
      ImGui_ImplWin32_WndProcHandler(window, message, w, l))
    return TRUE;
  if (message == WM_CLOSE) {
    DestroyWindow(window);
    return 0;
  }
  if (message == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  if (message == WM_SIZE && w == SIZE_MINIMIZED)
    return 0;
  if (message == WM_SYSCOMMAND && (w & 0xfff0) == SC_KEYMENU)
    return 0;
  return DefWindowProcW(window, message, w, l);
}
std::string utf8(const std::wstring &s) {
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0,
                              nullptr, nullptr);
  std::string out(n, 0);
  WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n,
                      nullptr, nullptr);
  return out;
}
std::string json(const std::string &s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += char(c);
    } else if (c < 32)
      out += ' ';
    else
      out += char(c);
  }
  return out + '"';
}
fs::path dialog(HWND owner, bool save, const wchar_t *filter,
                const wchar_t *ext = nullptr) {
  wchar_t buffer[32768]{};
  OPENFILENAMEW f{sizeof(f)};
  f.hwndOwner = owner;
  f.lpstrFilter = filter;
  f.lpstrFile = buffer;
  f.nMaxFile = 32768;
  f.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
            (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
  f.lpstrDefExt = ext;
  return (save ? GetSaveFileNameW(&f) : GetOpenFileNameW(&f)) ? fs::path(buffer)
                                                              : fs::path{};
}
bool is_image(sv::Kind kind) {
  return kind == sv::Kind::Texture || kind == sv::Kind::ConceptArt;
}
sv::Model texture_model(sv::Image img, const std::string &name,
                        bool opaque = false) {
  float w = float(img.width) / img.height;
  sv::Model m;
  m.name = name;
  sv::Part p;
  p.name = "Texture";
  p.texture = 0;
  p.opaque = opaque;
  p.vertices = {{-w, 1, 0, 0, 0}, {w, 1, 0, 1, 0},  {w, -1, 0, 1, 1},
                {-w, 1, 0, 0, 0}, {w, -1, 0, 1, 1}, {-w, -1, 0, 0, 1}};
  m.parts.push_back(std::move(p));
  m.textures.push_back(std::move(img));
  m.bounds();
  return m;
}
struct Graphics {
  HWND window = nullptr;
  sv::ComPtr<ID3D11Device> device;
  sv::ComPtr<ID3D11DeviceContext> context;
  sv::ComPtr<IDXGISwapChain> swap;
  sv::ComPtr<ID3D11RenderTargetView> back;
  UINT width = 0, height = 0;
  Graphics(bool hidden, bool warp) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW cls{};
    cls.lpfnWndProc = window_proc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.lpszClassName = L"ShaolinAssetViewer";
    RegisterClassW(&cls);
    window = CreateWindowExW(0, cls.lpszClassName, title, WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1440, 940, nullptr,
                             nullptr, cls.hInstance, nullptr);
    if (!window)
      throw std::runtime_error("Cannot create viewer window");
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    auto driver = warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE;
    auto hr = D3D11CreateDeviceAndSwapChain(
        nullptr, driver, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &desc,
        &swap, &device, nullptr, &context);
    if (FAILED(hr) && !warp)
      hr = D3D11CreateDeviceAndSwapChain(
          nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
          D3D11_SDK_VERSION, &desc, &swap, &device, nullptr, &context);
    sv::check(hr);
    if (!hidden)
      ShowWindow(window, SW_SHOWDEFAULT);
  }
  void resize() {
    RECT r;
    GetClientRect(window, &r);
    auto w = UINT(std::max(1L, r.right)), h = UINT(std::max(1L, r.bottom));
    if (w == width && h == height)
      return;
    context->OMSetRenderTargets(0, nullptr, nullptr);
    back.Reset();
    sv::check(swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0));
    sv::ComPtr<ID3D11Texture2D> b;
    sv::check(swap->GetBuffer(0, IID_PPV_ARGS(&b)));
    sv::check(device->CreateRenderTargetView(b.Get(), nullptr, &back));
    width = w;
    height = h;
  }
  sv::Image capture() {
    sv::ComPtr<ID3D11Texture2D> original, copy;
    sv::check(swap->GetBuffer(0, IID_PPV_ARGS(&original)));
    D3D11_TEXTURE2D_DESC desc{};
    original->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sv::check(device->CreateTexture2D(&desc, nullptr, &copy));
    context->CopyResource(copy.Get(), original.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    sv::check(context->Map(copy.Get(), 0, D3D11_MAP_READ, 0, &map));
    sv::Image image{width, height, sv::Bytes(size_t(width) * height * 4)};
    for (UINT y = 0; y < height; ++y)
      std::memcpy(image.rgba.data() + size_t(y) * width * 4,
                  static_cast<const uint8_t *>(map.pData) +
                      size_t(y) * map.RowPitch,
                  size_t(width) * 4);
    context->Unmap(copy.Get(), 0);
    return image;
  }
  ~Graphics() {
    if (context)
      context->ClearState();
    if (window && IsWindow(window))
      DestroyWindow(window);
  }
};
void theme() {
  ImGui::StyleColorsDark();
  auto &s = ImGui::GetStyle();
  s.WindowRounding = 0;
  s.FrameRounding = 4;
  s.GrabRounding = 4;
  s.FramePadding = {9, 6};
  s.ItemSpacing = {9, 8};
  s.WindowPadding = {14, 14};
  s.Colors[ImGuiCol_WindowBg] = {.075f, .08f, .095f, 1};
  s.Colors[ImGuiCol_ChildBg] = {.09f, .095f, .11f, 1};
  s.Colors[ImGuiCol_PopupBg] = {.09f, .095f, .11f, 1};
  s.Colors[ImGuiCol_FrameBg] = {.13f, .14f, .16f, 1};
  s.Colors[ImGuiCol_Header] = {.43f, .13f, .12f, 1};
  s.Colors[ImGuiCol_HeaderHovered] = {.58f, .18f, .15f, 1};
  s.Colors[ImGuiCol_HeaderActive] = {.65f, .20f, .17f, 1};
  s.Colors[ImGuiCol_Button] = {.27f, .13f, .13f, 1};
  s.Colors[ImGuiCol_ButtonHovered] = {.54f, .19f, .16f, 1};
  s.Colors[ImGuiCol_CheckMark] = {.95f, .43f, .29f, 1};
  s.Colors[ImGuiCol_SliderGrab] = {.85f, .32f, .23f, 1};
}
struct Loaded {
  std::unique_ptr<sv::Source> source;
  std::vector<sv::Asset> assets;
};
struct App {
  Graphics graphics;
  sv::Renderer renderer{graphics.device.Get(), graphics.context.Get()};
  sv::Camera camera;
  sv::Model model;
  std::unique_ptr<sv::Source> source;
  std::vector<sv::Asset> assets;
  std::future<Loaded> pending;
  sv::MusicPlayer music;
  std::future<sv::MusicTrack> pending_music;
  bool stage_bgm = true;
  std::string music_status;
  std::atomic_bool cancel = false;
  std::string status =
      "Open your Xbox game image or GAMEDATA.WAD to browse its assets.";
  fs::path source_path;
  int selected = -1, category = 1, texture_index = 0, clip_index = -1;
  bool texture_view = false, turntable = false, playing = true,
       root_motion = false, stage_helpers = false;
  bool image_fit = false;
  bool scroll_to_art = false;
  int art_group = 0;
  std::map<uint32_t, sv::ComPtr<ID3D11ShaderResourceView>> art_thumbnails;
  float speed = 20, animation_frame = 0, playback_speed = 1;
  bool secondary_motion = true;
  bool character_debug = false, debug_focus_requested = false;
  sv::CharacterDebug debug;
  sv::Camera debug_saved_camera;
  int debug_saved_clip = -1;
  float debug_saved_frame = 0;
  int scenery_clip = 0;
  int particle_command = 0;
  double scenery_time = 0;
  float scenery_speed = 1;
  bool scenery_playing = true, scenery_loop = true, animate_textures = true,
       animate_meshes = true;
  char search[128]{}, clip_search[128]{}, stage_search[128]{},
      art_search[128]{};
  std::string error;
  fs::path ui_capture;
  int ready_frames = 0;
  bool captured = false;
  int camera_drag = -1;
  bool camera_moved = false;
  float viewport_aspect = 1;
  int focused_asset = -1;
  struct HierarchyObject {
    size_t first;
    std::vector<size_t> parts;
  };
  std::vector<HierarchyObject> hierarchy_objects;
  bool reveal_selection = false;
  uint32_t initial_asset = 6029;
  bool require_initial_asset = false;
  App(bool hidden = false) : graphics(hidden, hidden), music(hidden) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    float dpi = GetDpiForWindow(graphics.window) / 96.f;
    io.FontGlobalScale = dpi;
    theme();
    ImGui::GetStyle().ScaleAllSizes(dpi);
    ImGui_ImplWin32_Init(graphics.window);
    ImGui_ImplDX11_Init(graphics.device.Get(), graphics.context.Get());
  }
  ~App() {
    stop_music();
    cancel = true;
    if (pending.valid())
      pending.wait();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
  }
  bool busy() const { return pending.valid(); }
  void stop_music() {
    music.stop();
    if (pending_music.valid()) {
      pending_music.wait();
      // Discard stale results (including failures) before changing sources.
      try {
        pending_music.get();
      } catch (...) {
      }
    }
    music_status.clear();
  }
  void start_music() {
    stop_music();
    if (!stage_bgm || !source || selected < 0 || !model.stage)
      return;
    music_status = "Loading stage music...";
    auto asset = assets[selected];
    pending_music = std::async(std::launch::async, [this, asset] {
      return source->stage_music(asset);
    });
  }
  void poll_music() {
    if (!pending_music.valid() || pending_music.wait_for(std::chrono::seconds(
                                      0)) != std::future_status::ready)
      return;
    try {
      auto track = pending_music.get();
      music_status =
          track.samples.empty() ? "No BGM defined for this stage." : track.name;
      music.play(std::move(track));
    } catch (const std::exception &e) {
      music_status = e.what();
    }
  }
  void open(fs::path path) {
    if (path.empty() || busy())
      return;
    stop_music();
    source_path = path;
    error.clear();
    status = "Indexing game assets...";
    pending = std::async(std::launch::async, [this, path] {
      Loaded out;
      out.source = std::make_unique<sv::Source>(path);
      out.assets = out.source->catalog(&cancel);
      return out;
    });
  }
  void select(int index) {
    try {
      if (index < 0 || size_t(index) >= assets.size())
        return;
      auto &a = assets[index];
      auto next = is_image(a.kind)
                      ? texture_model(source->image(a), a.name,
                                      a.kind == sv::Kind::ConceptArt)
                      : source->model(a);
      bool was_stage = model.stage;
      debug = {};
      model = std::move(next);
      selected = index;
      camera_drag = -1;
      camera_moved = false;
      renderer.selected_part = -1;
      renderer.fog_preset = 0;
      renderer.custom_background = false;
      rebuild_hierarchy();
      texture_view = is_image(a.kind);
      image_fit = texture_view;
      if (texture_view)
        turntable = false;
      if (a.kind == sv::Kind::ConceptArt) {
        renderer.textures = true;
        renderer.vertex_colors = renderer.wireframe = false;
        scroll_to_art = true;
      }
      texture_index = 0;
      if (was_stage && model.stage) {
        // Refresh clipping bounds without moving the view between maps.
        auto framed = camera;
        framed.frame(model);
        camera.scene_radius = framed.scene_radius;
      } else {
        camera.yaw = model.stage ? .785398f : texture_view ? 0 : 3.141593f;
        camera.pitch = model.stage ? .75f : texture_view ? 0 : .12f;
        camera.frame(model);
      }
      stage_helpers =
          std::any_of(model.parts.begin(), model.parts.end(),
                      [](auto &part) { return part.helper && part.visible; });
      if (model.stage || was_stage) {
        renderer.vertex_colors = model.stage;
        renderer.grid = !model.stage;
      }
      clip_index = model.clips.empty() ? -1 : 0;
      animation_frame = 0;
      scenery_clip = 0;
      particle_command = model.particle_emitters.empty()
                             ? 0
                             : int(model.particle_emitters.front().command);
      scenery_time = 0;
      scenery_playing = true;
      clip_search[0] = 0;
      model.pose(clip_index, 0, root_motion);
      if (character_debug && a.kind == sv::Kind::Character &&
          !model.bones.empty())
        begin_debug();
      renderer.selected_bone = -1;
      renderer.upload(model);
      start_music();
      error.clear();
      status = model.name + " | " + std::to_string(model.triangles) +
               " triangles / " + std::to_string(model.parts.size()) + " parts";
      if (a.kind == sv::Kind::ConceptArt)
        status = model.name + " | Original gallery image | " +
                 std::to_string(model.textures[0].width) + " x " +
                 std::to_string(model.textures[0].height) + " pixels";
      if (model.unsupported)
        status += " | " + std::to_string(model.unsupported) +
                  " unsupported records omitted";
    } catch (const std::exception &e) {
      error = e.what();
    }
  }
  void poll() {
    poll_music();
    if (!busy() ||
        pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
      return;
    try {
      auto result = pending.get();
      stop_music();
      source = std::move(result.source);
      assets = std::move(result.assets);
      art_thumbnails.clear();
      model = {};
      selected = -1;
      int first = -1;
      for (size_t i = 0; i < assets.size(); ++i)
        if (assets[i].kind == sv::Kind::Character) {
          first = int(i);
          break;
        }
      for (size_t i = 0; i < assets.size(); ++i)
        if (assets[i].id == initial_asset) {
          first = int(i);
          break;
        }
      if (require_initial_asset &&
          (first < 0 || assets[first].id != initial_asset))
        throw std::runtime_error("Requested asset has no supported geometry");
      if (first >= 0 && assets[first].kind == sv::Kind::Stage)
        category = 2;
      if (first >= 0 && assets[first].kind == sv::Kind::ConceptArt)
        category = 3;
      status = "Indexed " + std::to_string(assets.size()) + " assets.";
      if (first >= 0)
        select(first);
    } catch (const std::exception &e) {
      error = e.what();
      status = "Could not open game source.";
    }
  }
  void choose() {
    auto p =
        dialog(graphics.window, false,
               L"Xbox game source\0*.iso;*.xiso;GAMEDATA.WAD;default.xbe\0All "
               L"files\0*.*\0");
    open(p);
  }
  bool character_selected() const {
    return selected >= 0 && size_t(selected) < assets.size() &&
           assets[selected].kind == sv::Kind::Character;
  }
  void debug_camera() {
    model.bounds();
    camera.frame(model);
    camera.yaw = -1.570796327f;
    camera.pitch = .24f;
    camera.target = {model.grid_origin[0],
                     model.grid_origin[1] + model.debug_meter * .95f,
                     model.grid_origin[2]};
    camera.distance = model.debug_meter * 4.5f;
  }
  void begin_debug() {
    auto checker = source->checker_texture();
    auto common = source->common_motion(model);
    debug_saved_camera = camera;
    debug_saved_clip = clip_index;
    debug_saved_frame = animation_frame;
    debug.begin(model, checker, std::move(common));
    turntable = false;
    debug_camera();
    debug_focus_requested = true;
    renderer.selected_part = -1;
    rebuild_hierarchy();
  }
  void toggle_debug() {
    try {
      if (character_debug)
        begin_debug();
      else {
        debug.end(model);
        clip_index = debug_saved_clip;
        animation_frame = debug_saved_frame;
        model.pose(clip_index, animation_frame, root_motion);
        model.bounds();
        camera = debug_saved_camera;
        renderer.selected_part = -1;
        rebuild_hierarchy();
      }
      renderer.upload(model);
    } catch (const std::exception &e) {
      character_debug = false;
      error = e.what();
    }
  }
  void frame_target() {
    if (model.parts.empty())
      return;
    if (texture_view) {
      image_fit = true;
    } else if ((renderer.selected_part >= 0 &&
                model.parts[renderer.selected_part].sky) ||
               !camera.frame_object(model, size_t(renderer.selected_part),
                                    viewport_aspect)) {
      model.bounds();
      camera.frame(model);
    }
  }
  void rebuild_hierarchy() {
    hierarchy_objects.clear();
    reveal_selection = false;
    std::map<uint64_t, size_t> objects;
    for (size_t i = 0; i < model.parts.size(); ++i) {
      if (model.parts[i].role == sv::MeshRole::DebugFloor)
        continue;
      // Material batches share a source mesh ID. Unidentified meshes remain
      // separate objects even when they have the same display name or CRC.
      uint64_t key = model.parts[i].mesh_id == UINT32_MAX
                         ? (uint64_t(1) << 32) | i
                         : model.parts[i].mesh_id;
      auto [at, inserted] = objects.emplace(key, hierarchy_objects.size());
      if (inserted)
        hierarchy_objects.push_back({i, {}});
      hierarchy_objects[at->second].parts.push_back(i);
    }
  }
  void select_part(int part, bool reveal = false) {
    renderer.selected_part =
        part >= 0 && size_t(part) < model.parts.size() ? part : -1;
    reveal_selection = reveal && renderer.selected_part >= 0;
    for (const auto &e : model.particle_emitters)
      if (int(e.part) == renderer.selected_part)
        particle_command = int(e.command);
  }
  void particles_inspector() {
    if (!model.stage)
      return;
    ImGui::TextDisabled("PARTICLES (PREVIEW)");
    if (!model.particle_library) {
      ImGui::TextWrapped("%s", model.particle_status.empty()
                                   ? "No shared particle bank available."
                                   : model.particle_status.c_str());
      ImGui::Separator();
      return;
    }
    const auto &library = *model.particle_library;
    ImGui::Checkbox("Environment particles", &model.particles_enabled);
    size_t alive = 0;
    for (const auto &e : model.particle_emitters)
      alive += e.alive;
    ImGui::TextWrapped("%zu commands / %zu emitters / %zu live particles",
                       library.commands.size(), model.particle_emitters.size(),
                       alive);
    auto label = [](size_t id, const sv::ParticleCommand &c) {
      return "Command " + std::to_string(id) + " / sprite " +
             std::to_string(c.texture);
    };
    if (ImGui::BeginCombo(
            "##particlecommand",
            label(size_t(particle_command), library.commands[particle_command])
                .c_str())) {
      for (size_t i = 0; i < library.commands.size(); ++i) {
        const auto &c = library.commands[i];
        if (!c.maximum || c.life <= 0)
          continue;
        if (ImGui::Selectable(label(i, c).c_str(), particle_command == int(i)))
          particle_command = int(i);
      }
      ImGui::EndCombo();
    }
    int selected_emitter = -1;
    for (size_t i = 0; i < model.particle_emitters.size(); ++i)
      if (int(model.particle_emitters[i].part) == renderer.selected_part)
        selected_emitter = int(i);
    bool anchor = renderer.selected_part >= 0 && selected_emitter < 0 &&
                  !model.parts[renderer.selected_part].sky &&
                  !model.parts[renderer.selected_part].helper;
    ImGui::BeginDisabled(!anchor || model.particle_emitters.size() >= 64);
    if (ImGui::Button("Add at selected object")) {
      try {
        size_t e = model.add_particle_emitter(size_t(renderer.selected_part),
                                              size_t(particle_command));
        rebuild_hierarchy();
        select_part(int(model.particle_emitters[e].part), true);
        renderer.upload(model);
      } catch (const std::exception &e) {
        error = e.what();
      }
    }
    ImGui::EndDisabled();
    if (selected_emitter >= 0) {
      auto &e = model.particle_emitters[selected_emitter];
      ImGui::TextWrapped("Anchor: %s", model.parts[e.anchor].name.c_str());
      if (ImGui::Button("Apply command")) {
        model.set_particle_command(size_t(selected_emitter),
                                   size_t(particle_command));
      }
      ImGui::SameLine();
      if (ImGui::Button("Select anchor"))
        select_part(int(e.anchor), true);
    }
    ImGui::TextWrapped("Automatic flame anchors are inferred. Gameplay "
                       "emitter placement and triggers are not executed.");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip(
          "Source 60 Hz velocity/gravity, flagged size/color "
          "changes, sprite frames and alpha/add/subtract blending. "
          "Limited to 64 emitters and 64 particles each. "
          "Script events, collisions and special particle draw "
          "modes are not reproduced.");
    ImGui::Separator();
  }
  void part_tooltip(const sv::Part &part) {
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip(
          "%s\n%s\n%s%s%s\nDepth writes: %s\nRender: 0x%llX / object: 0x%llX",
          part.name.c_str(), sv::mesh_role_name(part.role),
          part.blend == sv::BlendMode::Additive      ? "Additive light / fog"
          : part.blend == sv::BlendMode::Multiply    ? "Shadow multiply preview"
          : part.blend == sv::BlendMode::Subtractive ? "Subtractive effect"
                                                     : "Diffuse / alpha",
          part.helper ? " (stage helper)" : "",
          part.sky ? " (camera-relative sky)" : "",
          part.sky           ? "off"
          : part.depth_write ? "on"
                             : "off",
          part.render_flags, part.object_flags);
  }
  void hierarchy() {
    ImGui::TextDisabled("HIERARCHY");
    ImGui::TextDisabled("%zu objects", hierarchy_objects.size());
    if (ImGui::Button("Show all"))
      for (auto &part : model.parts)
        part.visible = true;
    ImGui::SameLine();
    if (ImGui::Button("Hide all"))
      for (auto &part : model.parts)
        part.visible = false;
    ImGui::BeginChild("Objects", {0, 0}, true);
    auto start_row = [] {
      // Fractional DPI line heights round differently above and below the
      // viewport. A whole-pixel minimum keeps long-list reveal positions
      // stable.
      ImGui::Dummy({0, std::ceil(ImGui::GetFrameHeight())});
      ImGui::SameLine(0, 0);
    };
    for (const auto &object : hierarchy_objects) {
      auto &part = model.parts[object.first];
      bool selected =
          model.same_object(object.first, size_t(renderer.selected_part));
      bool visible =
          std::any_of(object.parts.begin(), object.parts.end(),
                      [&](size_t i) { return model.parts[i].visible; });
      ImGui::PushID(int(object.first));
      start_row();
      if (ImGui::Checkbox("##visible", &visible))
        for (size_t i : object.parts)
          model.parts[i].visible = visible;
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show / hide object");
      ImGui::SameLine();
      auto name = part.name.substr(0, part.name.find(" / "));
      if (part.sky)
        name += " (sky)";
      else if (part.role == sv::MeshRole::ShadowReceiver ||
               part.role == sv::MeshRole::ShadowOverlay)
        name += " (shadow)";
      else if (part.role == sv::MeshRole::Camera)
        name += " (camera)";
      else if (part.helper)
        name += " (helper)";
      auto flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                   ImGuiTreeNodeFlags_OpenOnArrow |
                   ImGuiTreeNodeFlags_OpenOnDoubleClick;
      if (selected)
        flags |= ImGuiTreeNodeFlags_Selected;
      if (object.parts.size() == 1)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
      if (selected && reveal_selection)
        ImGui::SetNextItemOpen(true);
      bool open = ImGui::TreeNodeEx("object", flags, "%s", name.c_str());
      if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        select_part(int(object.first));
      part_tooltip(part);
      if (selected && reveal_selection)
        ImGui::SetScrollHereY(.5f);
      if (open && object.parts.size() > 1) {
        for (size_t i : object.parts) {
          auto &material = model.parts[i];
          ImGui::PushID(int(i));
          start_row();
          ImGui::Checkbox("##visible", &material.visible);
          ImGui::SameLine();
          if (ImGui::Selectable(material.name.c_str(), selected))
            select_part(int(i));
          part_tooltip(material);
          ImGui::PopID();
        }
        ImGui::TreePop();
      }
      ImGui::PopID();
    }
    ImGui::EndChild();
    reveal_selection = false;
  }
  void export_model() {
    if (model.parts.empty())
      return;
    auto p = dialog(graphics.window, true, L"FBX model\0*.fbx\0", L"fbx");
    if (!p.empty())
      try {
        sv::export_fbx(model, p);
        status = "Exported visible parts, materials, and PNG textures.";
      } catch (const std::exception &e) {
        error = e.what();
      }
  }
  void screenshot() {
    if (model.parts.empty())
      return;
    auto p = dialog(graphics.window, true, L"PNG image\0*.png\0", L"png");
    if (!p.empty())
      try {
        sv::save_png(renderer.capture(), p);
        status = "Saved viewport image.";
      } catch (const std::exception &e) {
        error = e.what();
      }
  }
  bool matches(const sv::Asset &a) const {
    const sv::Kind categories[] = {sv::Kind::Character,  sv::Kind::Stage,
                                   sv::Kind::ConceptArt, sv::Kind::Environment,
                                   sv::Kind::Prop,       sv::Kind::Texture};
    if (category > 0 && a.kind != categories[category - 1])
      return false;
    std::string text = a.name, query = search;
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    std::transform(query.begin(), query.end(), query.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return text.find(query) != std::string::npos;
  }
  bool art_matches(const sv::Asset &asset) const {
    if (asset.kind != sv::Kind::ConceptArt ||
        (art_group && int(asset.art.group) != art_group))
      return false;
    std::string text = asset.name, query = art_search;
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    std::transform(query.begin(), query.end(), query.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return text.find(query) != std::string::npos;
  }
  std::vector<int> art_pages() const {
    std::vector<int> pages;
    for (size_t i = 0; i < assets.size(); ++i)
      if (art_matches(assets[i]))
        pages.push_back(int(i));
    return pages;
  }
  void art_step(int direction) {
    auto pages = art_pages();
    auto at = std::find(pages.begin(), pages.end(), selected);
    if (pages.empty())
      return;
    int next = at == pages.end() ? 0 : int(at - pages.begin()) + direction;
    if (next >= 0 && size_t(next) < pages.size())
      select(pages[next]);
  }
  ID3D11ShaderResourceView *art_thumbnail(const sv::Asset &asset) {
    auto [found, inserted] = art_thumbnails.try_emplace(asset.id);
    if (inserted) {
      try {
        sv::Image image;
        try {
          image = source->image(asset, true);
        } catch (const std::exception &) {
          image = source->image(asset);
        }
        found->second = renderer.image_view(image);
      } catch (const std::exception &) {
        // A missing preview must not prevent opening the full-size page.
      }
    }
    return found->second.Get();
  }
  void art_library() {
    const char *groups[] = {"All concept art", "Characters", "Environments",
                            "Storyboards", "Design studies"};
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##artgroup", &art_group, groups, 5);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##artsearch", "Search artwork or resource ID",
                             art_search, sizeof(art_search));
    auto pages = art_pages();
    ImGui::TextDisabled("%zu artwork pages", pages.size());
    ImGui::BeginChild("Art pages", {0, 0}, true);
    if (ImGui::BeginTable("Artwork thumbnails", 2,
                          ImGuiTableFlags_SizingStretchSame)) {
      for (int index : pages) {
        auto &asset = assets[index];
        ImGui::TableNextColumn();
        ImGui::PushID(int(asset.id));
        float side = std::max(24.f, ImGui::GetContentRegionAvail().x - 8);
        bool visible = ImGui::IsRectVisible({side, side});
        auto *view = visible ? art_thumbnail(asset) : nullptr;
        if (selected == index)
          ImGui::PushStyleColor(ImGuiCol_Button, {.65f, .20f, .17f, 1});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {3, 3});
        bool clicked = view ? ImGui::ImageButton(
                                  "##page", reinterpret_cast<ImTextureID>(view),
                                  {side, side})
                            : ImGui::Button("Open", {side + 6, side + 6});
        ImGui::PopStyleVar();
        if (selected == index)
          ImGui::PopStyleColor();
        if (clicked)
          select(index);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", asset.name.c_str());
        ImGui::TextWrapped("%s", sv::art_group_name(asset.art.group));
        ImGui::TextDisabled("%02u / #%u", asset.art.number, asset.id);
        if (selected == index && scroll_to_art) {
          ImGui::SetScrollHereY(.5f);
          scroll_to_art = false;
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    ImGui::EndChild();
  }
  void sidebar() {
    ImGui::TextColored({1, .51f, .34f, 1}, "SHAOLIN MONKS");
    ImGui::TextDisabled("RECOMP  /  3D VIEWER");
    ImGui::Spacing();
    ImGui::BeginDisabled(busy());
    if (ImGui::Button("Open game source...", {-1, 0}))
      choose();
    ImGui::EndDisabled();
    if (!source_path.empty())
      ImGui::TextWrapped("%s", utf8(source_path.filename().wstring()).c_str());
    ImGui::Separator();
    const char *categories[] = {"All assets",  "Characters",   "Stages",
                                "Concept Art", "Environments", "Props",
                                "Textures"};
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##category", &category, categories, 7) && category == 3 &&
        (selected < 0 || assets[selected].kind != sv::Kind::ConceptArt)) {
      auto pages = art_pages();
      if (!pages.empty())
        select(pages.front());
    }
    if (category == 3) {
      art_library();
      return;
    }
    bool character_library =
        category == 1 || (category == 0 && character_selected());
    if (character_library) {
      ImGui::BeginDisabled(!character_selected() || model.bones.empty() ||
                           busy());
      if (ImGui::Checkbox("Character Debug", &character_debug))
        toggle_debug();
      ImGui::EndDisabled();
      if (debug.active)
        ImGui::TextWrapped("Click the canvas to control. WASD / arrows: move. "
                           "Right Shift: run. Space: jump. J/K/L: quick/power/"
                           "launch. U: throw. E: special modifier. I: E+J.");
    } else {
      ImGui::TextDisabled("STAGE");
      ImGui::SetNextItemWidth(-1);
      ImGui::BeginDisabled(!source || busy());
      const char *stage =
          selected >= 0 && assets[selected].kind == sv::Kind::Stage
              ? assets[selected].name.c_str()
              : "Select a stage...";
      if (ImGui::BeginCombo("##stage", stage, ImGuiComboFlags_HeightLarge)) {
        ImGui::InputTextWithHint("##stagesearch", "Filter stages", stage_search,
                                 sizeof(stage_search));
        ImGui::BeginChild("Stage choices",
                          {0, ImGui::GetTextLineHeightWithSpacing() * 14});
        for (size_t i = 0; i < assets.size(); ++i) {
          auto &asset = assets[i];
          if (asset.kind != sv::Kind::Stage)
            continue;
          std::string name = asset.name, query = stage_search;
          std::transform(name.begin(), name.end(), name.begin(),
                         [](unsigned char c) { return char(std::tolower(c)); });
          std::transform(query.begin(), query.end(), query.begin(),
                         [](unsigned char c) { return char(std::tolower(c)); });
          if (name.find(query) == std::string::npos)
            continue;
          if (ImGui::Selectable(asset.name.c_str(), selected == int(i))) {
            select(int(i));
            category = 2;
            search[0] = 0;
          }
          if (selected == int(i))
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndChild();
        ImGui::EndCombo();
      }
      ImGui::EndDisabled();
      ImGui::TextDisabled(
          "%zu stages available",
          size_t(std::count_if(assets.begin(), assets.end(), [](auto &a) {
            return a.kind == sv::Kind::Stage;
          })));
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "Search name or resource ID", search,
                             sizeof(search));
    ImGui::TextDisabled("%zu resources indexed", assets.size());
    ImGui::BeginChild("Asset list", {0, 0}, true);
    for (size_t i = 0; i < assets.size(); ++i)
      if (matches(assets[i])) {
        ImGui::PushID(int(i));
        bool clicked =
            ImGui::Selectable(assets[i].name.c_str(), selected == int(i));
        bool focused = ImGui::IsItemFocused();
        bool navigated = focused && focused_asset != int(i) &&
                         (assets[i].kind == sv::Kind::Stage ||
                          assets[i].kind == sv::Kind::Character) &&
                         !ImGui::GetIO().WantTextInput;
        if (focused)
          focused_asset = int(i);
        if (clicked || (navigated && selected != int(i)))
          select(int(i));
        ImGui::PopID();
      }
    ImGui::EndChild();
  }
  void inspector() {
    if (selected >= 0 && assets[selected].kind == sv::Kind::ConceptArt) {
      auto &asset = assets[selected];
      ImGui::TextDisabled("CONCEPT ART");
      ImGui::TextWrapped("%s / Page %02u", sv::art_group_name(asset.art.group),
                         asset.art.number);
      ImGui::TextDisabled("Original gallery image");
      auto &image = model.textures.front();
      ImGui::Text("%u x %u pixels", image.width, image.height);
      ImGui::Separator();
      auto pages = art_pages();
      auto at = std::find(pages.begin(), pages.end(), selected);
      ImGui::BeginDisabled(at == pages.begin() || pages.empty());
      if (ImGui::Button("Previous"))
        art_step(-1);
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::BeginDisabled(pages.empty() ||
                           (at != pages.end() && at + 1 == pages.end()));
      if (ImGui::Button("Next"))
        art_step(1);
      ImGui::EndDisabled();
      if (ImGui::Button("Fit image  [F]", {-1, 0}))
        image_fit = true;
      ImGui::TextWrapped(
          "Drag to pan. Wheel to zoom. Left / right arrows change pages.");
      ImGui::Separator();
      if (ImGui::Button("Save artwork (.png)", {-1, 0})) {
        auto path =
            dialog(graphics.window, true, L"PNG image\0*.png\0", L"png");
        if (!path.empty())
          try {
            sv::save_png(model.textures.front(), path);
            status = "Saved original artwork image.";
          } catch (const std::exception &e) {
            error = e.what();
          }
      }
      return;
    }
    if (model.stage) {
      ImGui::TextDisabled("STAGE PREVIEW");
      ImGui::TextWrapped("Original scenery and vertex colors. Preview ambient "
                         "motion or select an animation group below.");
      if (ImGui::Checkbox("Stage BGM", &stage_bgm))
        start_music();
      float volume = music.volume() * 100;
      ImGui::SetNextItemWidth(-1);
      if (ImGui::SliderFloat("##BGM volume", &volume, 0, 100,
                             "Volume %.0f%%")) {
        try {
          music.set_volume(volume * .01f);
        } catch (const std::exception &e) {
          music_status = e.what();
        }
      }
      if (stage_bgm && !music_status.empty())
        ImGui::TextWrapped("%s", music_status.c_str());
      if (ImGui::Checkbox("Stage helpers", &stage_helpers))
        for (auto &part : model.parts)
          if (part.helper)
            part.visible = stage_helpers;
      ImGui::Checkbox("Cull back faces", &renderer.cull_backfaces);
      ImGui::Separator();
      ImGui::TextDisabled("ATMOSPHERE");
      ImGui::Checkbox("Sky domes", &renderer.sky);
      ImGui::Checkbox("Distance fog", &renderer.fog);
      ImGui::Checkbox("Stage background", &renderer.stage_background);
      if (model.atmosphere.available) {
        const char *presets[] = {"Primary", "Preset 2", "Preset 3", "Preset 4",
                                 "Preset 5"};
        ImGui::SetNextItemWidth(
            std::max(60.f, ImGui::GetContentRegionAvail().x -
                               ImGui::CalcTextSize("Fog preset").x -
                               ImGui::GetStyle().ItemInnerSpacing.x));
        if (ImGui::Combo("Fog preset", &renderer.fog_preset, presets, 5))
          renderer.custom_background = false;
        const auto &preset = model.atmosphere.fog[renderer.fog_preset];
        if (preset.active())
          ImGui::TextDisabled("Fog range: %.0f to %.0f", preset.start,
                              preset.end);
        else
          ImGui::TextDisabled("This preset has no distance fog.");
      } else
        ImGui::TextWrapped("%s", model.atmosphere_status.empty()
                                     ? "No original fog settings in this map."
                                     : model.atmosphere_status.c_str());
      auto color = renderer.custom_background ? renderer.background_color
                   : model.atmosphere.available
                       ? model.atmosphere.fog[renderer.fog_preset].color
                       : std::array<float, 3>{};
      if (ImGui::ColorEdit3("Background", color.data(),
                            ImGuiColorEditFlags_NoInputs)) {
        renderer.background_color = color;
        renderer.custom_background = true;
      }
      ImGui::TextDisabled(
          "%zu sky parts",
          size_t(std::count_if(model.parts.begin(), model.parts.end(),
                               [](auto &part) { return part.sky; })));
      if (ImGui::Button("Reset atmosphere")) {
        renderer.fog = renderer.sky = renderer.stage_background = true;
        renderer.custom_background = false;
        renderer.fog_preset = 0;
      }
      ImGui::Separator();
    }
    if (model.stage) {
      ImGui::TextDisabled("STAGE LIGHTING");
      ImGui::Checkbox("Source lights", &renderer.source_lights);
      ImGui::Checkbox("Show light positions", &renderer.light_gizmos);
      ImGui::TextWrapped("%zu source lights. Compact scenery keeps its baked "
                         "vertex lighting.",
                         model.lights.size());
      if (ImGui::CollapsingHeader("Light records")) {
        ImGui::BeginChild("##lights", {0, 150}, true);
        for (size_t i = 0; i < model.lights.size(); ++i) {
          auto &light = model.lights[i];
          ImGui::PushID(int(i));
          ImGui::Checkbox("##enabled", &light.enabled);
          ImGui::SameLine();
          ImGui::Text("%zu / %s", i + 1,
                      light.type == 1   ? "Point"
                      : light.type == 2 ? "Spot"
                      : light.type == 4 ? "Directional"
                                        : "Unsupported");
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "ID %08X\nPosition %.0f, %.0f, %.0f\nRGB %.2f, %.2f, "
                "%.2f\nNear %.0f / far %.0f\nAffects meshes: %s",
                light.id, light.position[0], light.position[1],
                light.position[2], light.color[0], light.color[1],
                light.color[2], light.near_range, light.far_range,
                light.affects_meshes ? "yes" : "no");
          ImGui::PopID();
        }
        ImGui::EndChild();
      }
      ImGui::Separator();
    }
    particles_inspector();
    if (!model.scenery.empty()) {
      ImGui::TextDisabled("SCENERY ANIMATION");
      const char *name = scenery_clip >= 0
                             ? model.scenery[scenery_clip].name.c_str()
                             : "Static pose";
      if (ImGui::BeginCombo("##sceneryclip", name)) {
        if (ImGui::Selectable("Static pose", scenery_clip < 0)) {
          scenery_clip = -1;
          scenery_time = 0;
        }
        for (size_t i = 0; i < model.scenery.size(); ++i)
          if (ImGui::Selectable(model.scenery[i].name.c_str(),
                                scenery_clip == int(i))) {
            scenery_clip = int(i);
            scenery_time = 0;
            scenery_playing = true;
          }
        ImGui::EndCombo();
      }
      if (scenery_clip >= 0) {
        ImGui::Checkbox("Play", &scenery_playing);
        ImGui::SameLine();
        if (ImGui::Button("Restart"))
          scenery_time = 0;
        float seconds = float(scenery_time);
        if (ImGui::SliderFloat("Time", &seconds, 0,
                               model.scenery[scenery_clip].duration, "%.2f s"))
          scenery_time = seconds;
        ImGui::SliderFloat("Rate", &scenery_speed, .1f, 3, "%.2fx");
        ImGui::Checkbox("Loop preview", &scenery_loop);
        ImGui::Checkbox("Animate textures", &animate_textures);
        ImGui::Checkbox("Animate meshes", &animate_meshes);
      }
      ImGui::TextWrapped("%zu scrolling parts / %zu moving parts",
                         model.scrolling_parts(), model.moving_parts());
      if (scenery_clip > 0)
        ImGui::TextWrapped("%zu group tracks; ambient motion continues.",
                           model.scenery[scenery_clip].tracks.size());
      ImGui::Separator();
    }
    if (!model.scenery_status.empty())
      ImGui::TextWrapped("%s", model.scenery_status.c_str());
    if (debug.active) {
      ImGui::TextDisabled("CHARACTER DEBUG");
      ImGui::Text("%s", sv::CharacterDebug::action_name(debug.action));
      if (debug.attack_clip >= 0)
        ImGui::TextWrapped(
            "Attack: %s%s", model.clips[debug.attack_clip].name.c_str(),
            model.clips[debug.attack_clip].common ? " (fallback)" : "");
      ImGui::Text("Speed: %.2f m/s", debug.velocity / model.debug_meter);
      ImGui::SliderFloat("Movement rate", &debug.locomotion_rate, .25f, 3.f,
                         "%.2fx");
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Walk/run cadence and travel only. Jump physics "
                          "uses the source profile at fixed 60 Hz.");
      ImGui::Text("Position: %.1f, %.1f m",
                  debug.position[0] / model.debug_meter,
                  debug.position[2] / model.debug_meter);
      ImGui::TextWrapped(
          "1 checker square = 1 meter. Black fog stays enabled.");
      if (ImGui::Button("Reset character")) {
        debug.reset();
        debug.update(model, {}, 0, camera.yaw);
        debug_camera();
      }
      if (std::any_of(debug.clips.begin(), debug.clips.end(),
                      [](int c) { return c < 0; }))
        ImGui::TextWrapped(
            "Some actions have no compatible source clip. Missing "
            "ground actions hold idle; unavailable jumps are disabled.");
      if (ImGui::CollapsingHeader("Movement clips"))
        for (int a = 0; a < sv::CharacterDebug::Count; ++a) {
          int clip = debug.clips[a];
          ImGui::TextWrapped(
              "%s: %s%s",
              sv::CharacterDebug::action_name(sv::CharacterDebug::Action(a)),
              clip >= 0 ? model.clips[clip].name.c_str() : "No compatible clip",
              clip >= 0 && model.clips[clip].common ? " (fallback)" : "");
        }
      if (ImGui::CollapsingHeader("Attacks and kombos",
                                  ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int kind = 0; kind < sv::CharacterDebug::AttackCount; ++kind) {
          ImGui::PushID(kind);
          ImGui::BeginDisabled(!model.action_graph.available() &&
                               debug.attacks[kind].empty());
          if (ImGui::Button(sv::CharacterDebug::attack_name(kind)))
            debug.request_attack(kind);
          ImGui::SameLine();
          if (!model.action_graph.available())
            if (ImGui::Button("Preview sequence"))
              debug.request_attack(kind, true);
          ImGui::EndDisabled();
          ImGui::PopID();
        }
        static int move = 0;
        move = std::clamp(move, 0, std::max(0, int(debug.moves.size()) - 1));
        const char *name = debug.moves.empty()
                               ? "No source attacks"
                               : model.clips[debug.moves[move]].name.c_str();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##Attack clip", name)) {
          for (size_t i = 0; i < debug.moves.size(); ++i) {
            const auto &clip = model.clips[debug.moves[i]];
            std::string label = clip.name + (clip.common ? " (fallback)"
                                             : clip.encoding != 11 ? " (legacy)"
                                                                   : "");
            ImGui::PushID(int(i));
            if (ImGui::Selectable(label.c_str(), move == int(i)))
              move = int(i);
            ImGui::PopID();
          }
          ImGui::EndCombo();
        }
        ImGui::BeginDisabled(debug.moves.empty());
        if (ImGui::Button("Execute selected attack"))
          debug.request_move(debug.moves[move]);
        ImGui::EndDisabled();
        ImGui::TextWrapped(model.action_graph.available()
                               ? "Original input graph: press successive "
                                 "attack buttons to chain. "
                                 "Source links, input timeouts, start frames "
                                 "and blends are used. "
                                 "Target, hit and unlock predicates are "
                                 "unavailable on this floor; "
                                 "those branches are disabled. The move "
                                 "browser previews clips directly."
                               : "No compatible original input graph. These "
                                 "buttons and O preview "
                                 "animation sequences only; they do not "
                                 "reproduce gameplay kombos.");
      }
      ImGui::Checkbox("Show joints", &renderer.joints);
      ImGui::TextWrapped(
          model.controls.available
              ? "Recovered jump physics: hold Space for full height, tap for a "
                "short jump. Full gameplay controls remain unverified."
              : "Source jump parameters unavailable; jumping is disabled.");
      ImGui::Separator();
    }
    if (!model.bones.empty() && !debug.active) {
      ImGui::TextDisabled("ANIMATION");
      if (ImGui::BeginCombo("##clip", clip_index >= 0
                                          ? model.clips[clip_index].name.c_str()
                                          : "Bind pose")) {
        ImGui::InputTextWithHint("##clipsearch", "Filter clips", clip_search,
                                 sizeof(clip_search));
        if (ImGui::Selectable("Bind pose", clip_index < 0)) {
          clip_index = -1;
          animation_frame = 0;
        }
        for (size_t i = 0; i < model.clips.size(); ++i) {
          std::string name = model.clips[i].name, query = clip_search;
          std::transform(name.begin(), name.end(), name.begin(),
                         [](unsigned char c) { return char(std::tolower(c)); });
          std::transform(query.begin(), query.end(), query.begin(),
                         [](unsigned char c) { return char(std::tolower(c)); });
          if (name.find(query) == std::string::npos)
            continue;
          if (ImGui::Selectable(model.clips[i].name.c_str(),
                                clip_index == int(i))) {
            clip_index = int(i);
            animation_frame = 0;
          }
        }
        ImGui::EndCombo();
      }
      if (clip_index >= 0) {
        auto &clip = model.clips[clip_index];
        ImGui::Checkbox("Play", &playing);
        ImGui::SameLine();
        if (ImGui::Button("Restart"))
          animation_frame = 0;
        ImGui::SliderFloat("Frame", &animation_frame, 0, float(clip.frames - 1),
                           "%.1f");
        ImGui::SliderFloat("Rate", &playback_speed, .1f, 2, "%.2fx");
        ImGui::SliderFloat("Playback FPS", &clip.fps, 1, 120, "%.1f");
        if (ImGui::Button("30 FPS / 1x")) {
          clip.fps = 30;
          playback_speed = 1;
        }
        ImGui::TextDisabled("%.2f s / %.2f s", animation_frame / clip.fps,
                            clip.frames / clip.fps);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Nominal clip clock. Gameplay scripts can change "
                            "the rate; those overrides are not executed here.");
        ImGui::Checkbox("Root motion", &root_motion);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip(
              "Keep horizontal travel. Disabled: remove net travel "
              "while preserving hip sway. Clips start at the grid "
              "center; vertical motion is always preserved.");
        ImGui::TextDisabled("%u frames / %.0f fps", clip.frames, clip.fps);
      }
      ImGui::TextWrapped("%s", model.animation_status.c_str());
      if (model.secondary_joints()) {
        ImGui::Checkbox(model.joint_dynamics.available
                            ? "Dynamic joints"
                            : "Secondary motion (preview)",
                        &secondary_motion);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip(
              "Original flap hierarchy with executable spring gravity, "
              "stiffness, damping and stretch limits. Actor movement drives "
              "inertia. Gameplay collision volumes and scripted overrides "
              "are not reproduced.");
      }
      ImGui::Checkbox("Show joints", &renderer.joints);
      if (renderer.joints)
        ImGui::Checkbox("All rig joints", &renderer.all_joints);
      ImGui::BeginChild("Joints", {0, 105}, true);
      for (size_t i = 0; i < model.bones.size(); ++i)
        if (model.bones[i].used || renderer.all_joints) {
          ImGui::PushID(int(i));
          if (ImGui::Selectable(model.bones[i].name.c_str(),
                                renderer.selected_bone == int(i)))
            renderer.selected_bone = int(i);
          if (ImGui::IsItemHovered()) {
            auto &bone = model.bones[i];
            ImGui::SetTooltip(
                "Parent: %s\nPosition: %.1f, %.1f, %.1f",
                bone.parent >= 0 ? model.bones[bone.parent].name.c_str()
                                 : "root",
                bone.position[0], bone.position[1], bone.position[2]);
          }
          ImGui::PopID();
        }
      ImGui::EndChild();
      ImGui::Separator();
    }
    ImGui::TextDisabled("DISPLAY");
    ImGui::Checkbox("Textures", &renderer.textures);
    ImGui::Checkbox("Lighting", &renderer.lighting);
    if (!model.stage && !model.bones.empty() && renderer.lighting) {
      ImGui::Checkbox("Directional shadows", &renderer.shadows);
      ImGui::SliderFloat("Light azimuth", &renderer.light_azimuth, -180, 180,
                         "%.0f deg");
      ImGui::SliderFloat("Light elevation", &renderer.light_elevation, 10, 85,
                         "%.0f deg");
    }
    ImGui::Checkbox("Wireframe", &renderer.wireframe);
    ImGui::Checkbox("Vertex colors", &renderer.vertex_colors);
    if (!debug.active) {
      ImGui::Checkbox("Ground grid", &renderer.grid);
      ImGui::Checkbox("Turntable", &turntable);
    } else
      ImGui::TextDisabled("Checker floor / black fog locked on");
    if (turntable)
      ImGui::SliderFloat("Speed", &speed, -90, 90, "%.0f deg/s");
    ImGui::Checkbox("Bloom (preview)", &renderer.bloom);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip(
          "Soft glow from additive layers and identified eye/glow materials. "
          "This is a viewer approximation of emission, not a reproduction of "
          "the game's post-processing.");
    if (renderer.bloom) {
      ImGui::TextUnformatted("Glow strength");
      ImGui::SetNextItemWidth(-1);
      ImGui::SliderFloat("##Glow strength", &renderer.bloom_strength, 0, 3,
                         "%.2f");
      ImGui::TextUnformatted("Glow radius");
      ImGui::SetNextItemWidth(-1);
      ImGui::SliderFloat("##Glow radius", &renderer.bloom_radius, .5f, 4,
                         "%.2f");
      ImGui::TextUnformatted("Glow threshold");
      ImGui::SetNextItemWidth(-1);
      ImGui::SliderFloat("##Glow threshold", &renderer.bloom_threshold, 0, 1,
                         "%.2f");
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("CAMERA");
    if (renderer.selected_part >= 0 &&
        size_t(renderer.selected_part) < model.parts.size()) {
      ImGui::TextWrapped("Selected: %s",
                         model.parts[renderer.selected_part].name.c_str());
      auto &part = model.parts[renderer.selected_part];
      ImGui::TextWrapped("%s", sv::mesh_role_name(part.role));
      if (part.emissive)
        ImGui::TextWrapped("Glow source (preview)");
      if (ImGui::CollapsingHeader("Object details")) {
        ImGui::Text("Object ID: %08X", part.object);
        ImGui::Text("Render flags: %llX", part.render_flags);
        ImGui::Text("Object flags: %llX", part.object_flags);
        ImGui::Text("Texture: %d / alpha cutoff %.2f", part.texture,
                    part.alpha_cutoff);
        if (part.source_material) {
          ImGui::Text("Material flags: %X", part.material_flags);
          ImGui::Text("Reflection: %d / gloss: %d", part.reflection.texture,
                      part.gloss.texture);
          ImGui::Text("Address U/V: %s / %s",
                      (part.material_flags & 1) ? "wrap" : "clamp",
                      (part.material_flags & 2) ? "wrap" : "clamp");
          ImGui::TextWrapped(part.prelit ? "Baked vertex lighting"
                                         : "Normal-based lighting");
        }
        ImGui::Text("Opacity: %.0f%%", part.opacity * 100);
        if (part.inferred_solid_alpha)
          ImGui::TextWrapped(
              "Sparse dark alpha inferred as solid diffuse detail.");
        ImGui::Checkbox("Use texture alpha", &part.texture_alpha);
        bool two_sided = part.two_sided;
        if (ImGui::Checkbox("Two-sided object preview", &two_sided))
          for (size_t p = 0; p < model.parts.size(); ++p)
            if (model.same_object(p, size_t(renderer.selected_part)))
              model.parts[p].two_sided = two_sided;
        if (part.legacy_two_sided)
          ImGui::TextWrapped("Open legacy shell: front side is ambiguous; both "
                             "sides are previewed by default.");
        if (part.flicker)
          ImGui::TextWrapped("Original background opacity flicker. Random "
                             "perturbations are not simulated.");
        unsigned lo = 255, hi = 0;
        for (auto &v : part.vertices) {
          lo = std::min(lo, v.color >> 24);
          hi = std::max(hi, v.color >> 24);
        }
        ImGui::Text("Vertex alpha: %u to %u / 128", lo, hi);
        if (part.role == sv::MeshRole::ShadowReceiver)
          ImGui::TextWrapped("Multiplicative texture preview. Dynamic shadow "
                             "projection is not simulated.");
        if (model.stage && ImGui::Button("Flip object faces")) {
          model.flip_object(size_t(renderer.selected_part));
          renderer.upload(model);
        }
      }
      if (ImGui::Button("Clear selection"))
        select_part(-1);
    }
    if (ImGui::Button(renderer.selected_part >= 0 ? "Frame selected  [F]"
                                                  : "Frame asset  [F]",
                      {-1, 0}))
      frame_target();
    const char *views[] = {"Front", "Back", "Left", "Right", "Top"};
    for (int i = 0; i < 5; ++i) {
      if (i & 1)
        ImGui::SameLine();
      if (ImGui::Button(views[i], {90, 0})) {
        camera.yaw = i == 0   ? 3.141593f
                     : i == 2 ? -1.570796f
                     : i == 3 ? 1.570796f
                              : 0;
        camera.pitch = i == 4 ? 1.55f : 0;
      }
    }
    ImGui::SliderFloat("FOV", &camera.fov, 15, 100, "%.0f");
    ImGui::TextDisabled("Left click: select / clear\nLeft drag: orbit\nMiddle "
                        "drag: pan\nWheel / right drag: dolly");
    ImGui::Separator();
    ImGui::TextDisabled("GEOMETRY");
    ImGui::Text("%zu triangles", model.triangles);
    ImGui::Text("%zu texture slots", model.textures.size());
    ImGui::Text("%zu original normals", model.authored_normals);
    if (model.generated_normals)
      ImGui::Text("%zu generated normals", model.generated_normals);
    if (model.legacy_winding) {
      ImGui::TextWrapped("Legacy orientation repair: %zu triangles",
                         model.repaired_triangles);
      bool repair = model.winding_repair;
      if (ImGui::Checkbox("Repair legacy winding", &repair)) {
        model.set_winding_repair(repair);
        renderer.upload(model);
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Connected faces are oriented consistently. Broad "
                          "terrain sheets face up; closed meshes face outward. "
                          "Disable to inspect the original strip orientation.");
    }
    ImGui::Separator();
    ImGui::TextDisabled("EXPORT");
    if (ImGui::Button("Visible mesh + textures (.fbx)", {-1, 0}))
      export_model();
    if (ImGui::Button("Viewport image (.png)", {-1, 0}))
      screenshot();
    if (!model.textures.empty()) {
      ImGui::SliderInt("Texture slot", &texture_index, 0,
                       int(model.textures.size() - 1));
      auto &image = model.textures[texture_index];
      ImGui::Text("%u x %u", image.width, image.height);
      if (ImGui::Button("Save texture (.png)", {-1, 0}) &&
          !image.rgba.empty()) {
        auto p = dialog(graphics.window, true, L"PNG image\0*.png\0", L"png");
        if (!p.empty())
          try {
            sv::save_png(image, p);
          } catch (const std::exception &e) {
            error = e.what();
          }
      }
    }
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Original meshes, skeletons, and motion clips. Preview lighting; "
        "Gameplay animation events are not simulated. "
        "Dynamic joints use recovered spring defaults; gameplay collision "
        "volumes and scripted spring overrides are not simulated.");
  }
  void frame() {
    poll();
    graphics.resize();
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    auto &io = ImGui::GetIO();
    float scale = io.FontGlobalScale;
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Viewer", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_MenuBar);
    if (ImGui::BeginMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open game source...", "Ctrl+O", false, !busy()))
          choose();
        if (ImGui::MenuItem("Export visible mesh...", nullptr, false,
                            !model.parts.empty() && !texture_view))
          export_model();
        if (ImGui::MenuItem("Save viewport...", nullptr, false,
                            !model.parts.empty()))
          screenshot();
        ImGui::EndMenu();
      }
      ImGui::TextDisabled("Xbox assets  /  independent native viewer");
      ImGui::EndMenuBar();
    }
    float footer = 50 * scale,
          height = std::max(100.f, ImGui::GetContentRegionAvail().y - footer);
    float left = 280 * scale, right = 260 * scale;
    bool show_hierarchy = !texture_view && !model.parts.empty();
    float hierarchy_width = show_hierarchy ? 250 * scale : 0;
    float center = std::max(80.f, ImGui::GetContentRegionAvail().x - left -
                                      right - hierarchy_width -
                                      (show_hierarchy ? 30 : 20) * scale);
    ImGui::BeginChild("Library", {left, height}, true);
    sidebar();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("Viewport", {center, height}, false,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
    if (debug_focus_requested && debug.active) {
      ImGui::SetWindowFocus();
      debug_focus_requested = false;
    }
    ImGui::TextUnformatted(model.name.empty() ? "ASSET PREVIEW"
                                              : model.name.c_str());
    ImGui::Separator();
    auto available = ImGui::GetContentRegionAvail();
    viewport_aspect = std::max(1.f, available.x) / std::max(1.f, available.y);
    renderer.resize(uint32_t(std::max(1.f, available.x)),
                    uint32_t(std::max(1.f, available.y)));
    if (!model.parts.empty()) {
      if (texture_view && image_fit) {
        camera.frame(model);
        camera.yaw = camera.pitch = 0;
        float aspect = std::max(1.f, available.x) / std::max(1.f, available.y);
        float tangent = std::tan(camera.fov * 3.141593f / 360);
        camera.distance =
            1.06f * std::max(1.f / tangent, float(model.textures[0].width) /
                                                model.textures[0].height /
                                                (tangent * aspect));
        image_fit = false;
      }
      if (clip_index >= 0 && playing && !debug.active) {
        auto &clip = model.clips[clip_index];
        animation_frame = sv::advance_animation_frame(
            clip, animation_frame, io.DeltaTime, playback_speed);
      }
      if (!model.bones.empty() && !debug.active) {
        model.pose(clip_index, animation_frame, root_motion, secondary_motion,
                   playing ? io.DeltaTime * playback_speed : 0, -1, 0, 1, false,
                   true);
        renderer.update_pose(model);
      } else if (!model.scenery.empty()) {
        if (scenery_clip >= 0 && scenery_playing) {
          double duration = model.scenery[scenery_clip].duration;
          scenery_time += io.DeltaTime * scenery_speed;
          if (scenery_time >= duration) {
            if (scenery_loop)
              scenery_time = std::fmod(scenery_time, duration);
            else {
              scenery_time = duration;
              scenery_playing = false;
            }
          }
        }
        model.animate_scenery(scenery_clip, scenery_time, animate_textures,
                              animate_meshes);
      }
      if (turntable)
        camera.yaw += speed * 3.141593f / 180 * std::min(io.DeltaTime, .1f);
      ImGui::Image(reinterpret_cast<ImTextureID>(renderer.view()), available);
      auto viewport = ImGui::GetItemRectMin();
      bool hovered = ImGui::IsItemHovered();
      if (io.AppFocusLost) {
        camera_drag = -1;
        camera_moved = false;
      }
      if (!io.AppFocusLost && hovered) {
        if (camera_drag < 0)
          for (int button : {ImGuiMouseButton_Left, ImGuiMouseButton_Middle,
                             ImGuiMouseButton_Right})
            if (ImGui::IsMouseClicked(button)) {
              camera_drag = button;
              camera_moved = false;
              break;
            }
        if (io.MouseWheel)
          camera.zoom(io.MouseWheel);
        if (assets[selected].kind == sv::Kind::ConceptArt &&
            !io.WantTextInput) {
          if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
            art_step(-1);
          if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
            art_step(1);
        }
      }
      if (camera_drag >= 0) {
        camera_moved |= ImGui::IsMouseDragging(camera_drag);
        if (!ImGui::IsMouseDown(camera_drag)) {
          if (camera_drag == ImGuiMouseButton_Left && !texture_view &&
              !camera_moved && hovered) {
            int hit = renderer.pick(model, camera, io.MousePos.x - viewport.x,
                                    io.MousePos.y - viewport.y);
            select_part(
                model.same_object(size_t(hit), size_t(renderer.selected_part))
                    ? -1
                    : hit,
                true);
          }
          camera_drag = -1;
          camera_moved = false;
        } else if (camera_moved) {
          if (texture_view || camera_drag == ImGuiMouseButton_Middle)
            camera.pan(io.MouseDelta.x, io.MouseDelta.y, available.y);
          else if (camera_drag == ImGuiMouseButton_Right)
            camera.dolly(io.MouseDelta.y);
          else
            camera.orbit(io.MouseDelta.x, io.MouseDelta.y);
        }
      }
      if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F))
        frame_target();
      bool debug_controls =
          debug.active && !io.AppFocusLost && !io.WantTextInput &&
          ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
          !ImGui::IsAnyItemActive();
      if (debug_controls)
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
      else
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
      if (debug.active) {
        sv::DebugInput input;
        if (debug_controls) {
          auto held = [](ImGuiKey key) { return ImGui::IsKeyDown(key); };
          input.right = float(held(ImGuiKey_D) || held(ImGuiKey_RightArrow)) -
                        float(held(ImGuiKey_A) || held(ImGuiKey_LeftArrow));
          input.forward = float(held(ImGuiKey_W) || held(ImGuiKey_UpArrow)) -
                          float(held(ImGuiKey_S) || held(ImGuiKey_DownArrow));
          input.run = held(ImGuiKey_RightShift);
          input.jump = ImGui::IsKeyPressed(ImGuiKey_Space, false);
          input.jump_held = ImGui::IsKeyDown(ImGuiKey_Space);
          if (input.right > .5f)
            input.buttons |= 0x2000;
          if (input.right < -.5f)
            input.buttons |= 0x8000;
          if (input.forward > .5f)
            input.buttons |= 0x1000;
          if (input.forward < -.5f)
            input.buttons |= 0x4000;
          if (held(ImGuiKey_E))
            input.buttons |= 0x08;
          const ImGuiKey attacks[]{ImGuiKey_J, ImGuiKey_K, ImGuiKey_L,
                                   ImGuiKey_U, ImGuiKey_I};
          const uint16_t buttons[]{0x80, 0x20, 0x10, 0x02, 0x88};
          for (int i = 0; i < sv::CharacterDebug::AttackCount; ++i) {
            if (held(attacks[i]))
              input.buttons |= buttons[i];
            if (ImGui::IsKeyPressed(attacks[i], false))
              input.attack = i;
          }
          if (!model.action_graph.available() &&
              ImGui::IsKeyPressed(ImGuiKey_O, false) && !io.KeyCtrl) {
            input.attack = sv::CharacterDebug::Quick;
            input.kombo = true;
          }
        }
        auto previous = debug.position;
        if (!io.AppFocusLost)
          debug.update(model, input, io.DeltaTime, camera.yaw,
                       secondary_motion);
        for (size_t k = 0; k < 3; ++k)
          camera.target[k] += debug.position[k] - previous[k];
        renderer.update_pose(model);
      }
      if (model.bones.empty() && !model.scenery.empty()) {
        bool enabled = model.particles_enabled;
        model.particles_enabled = enabled && scenery_clip >= 0;
        camera.animate_particles(model, scenery_time);
        model.particles_enabled = enabled;
        renderer.update_pose(model, true);
      }
      renderer.draw(model, camera, texture_view);
    } else {
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + height * .35f);
      ImGui::TextWrapped(
          busy() ? "Reading the game archives..."
                 : "Open your Xbox game source to preview characters, "
                   "stages, concept art, environments, props, and textures.");
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (show_hierarchy) {
      ImGui::BeginChild("Hierarchy", {hierarchy_width, height}, true);
      hierarchy();
      ImGui::EndChild();
      ImGui::SameLine();
    }
    ImGui::BeginChild("Inspector", {0, height}, true);
    inspector();
    ImGui::EndChild();
    ImGui::Separator();
    if (!error.empty())
      ImGui::TextColored({1, .45f, .35f, 1}, "%s", error.c_str());
    else
      ImGui::TextWrapped("%s", status.c_str());
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O) && !busy())
      choose();
    ImGui::End();
    ImGui::Render();
    float clear[] = {.05f, .05f, .06f, 1};
    auto *target = graphics.back.Get();
    graphics.context->OMSetRenderTargets(1, &target, nullptr);
    graphics.context->ClearRenderTargetView(target, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if (!ui_capture.empty() && !model.parts.empty() && ++ready_frames >= 3 &&
        !captured) {
      sv::save_png(graphics.capture(), ui_capture);
      captured = true;
    }
    sv::check(graphics.swap->Present(1, 0));
  }
  void run() {
    MSG msg{};
    bool running = true;
    while (running) {
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
          running = false;
          break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      if (!running)
        break;
      if (IsIconic(graphics.window)) {
        Sleep(25);
        continue;
      }
      frame();
    }
  }
};
} // namespace
int wmain(int argc, wchar_t **argv) {
  bool inspect = false, validate = false, smoke = false, ui_smoke = false;
  bool asset_requested = false;
  fs::path source_path, report, capture;
  uint32_t asset_id = 6029;
  int clip_index = -1;
  float animation_frame = 0;
  double scenery_time = 0;
  try {
    for (int i = 1; i < argc; ++i) {
      std::wstring a = argv[i];
      auto argument = [&]() {
        if (++i >= argc)
          throw std::runtime_error("Missing command-line value");
        return std::wstring(argv[i]);
      };
      if (a == L"--source")
        source_path = argument();
      else if (a == L"--inspect")
        inspect = true;
      else if (a == L"--validate")
        validate = true;
      else if (a == L"--smoke-test")
        smoke = true;
      else if (a == L"--ui-smoke-test")
        ui_smoke = true;
      else if (a == L"--report")
        report = argument();
      else if (a == L"--capture")
        capture = argument();
      else if (a == L"--asset") {
        asset_id = uint32_t(std::stoul(argument()));
        asset_requested = true;
      } else if (a == L"--stage") {
        auto stage = std::stoul(argument());
        if (!stage || stage > 256)
          throw std::runtime_error("Stage map ID must be between 1 and 256");
        asset_id = 893 + 20 * uint32_t(stage);
        asset_requested = true;
      } else if (a == L"--clip")
        clip_index = std::stoi(argument());
      else if (a == L"--frame")
        animation_frame = std::stof(argument());
      else if (a == L"--time")
        scenery_time = std::stod(argument());
      else if (a == L"--help") {
        std::cout
            << "Shaolin Monks Recomp 3D Viewer\n--source "
               "ISO/XISO/GAMEDATA.WAD/default.xbe\n--inspect [--validate] "
               "[--report report.json]\n--smoke-test [--asset ID] [--clip "
               "INDEX] [--frame FRAME] [--capture image.png]\n"
               "Scenery previews: --time SECONDS [--clip GROUP_INDEX]\n"
               "--stage MAP_ID selects a stage instead of --asset ID\n"
               "--ui-smoke-test --capture viewer.png [--stage MAP_ID]\n";
        return 0;
      } else
        throw std::runtime_error("Unknown command-line option");
    }
    if (inspect || validate || smoke) {
      if (source_path.empty())
        throw std::runtime_error("--source is required for diagnostics");
      sv::Source source(source_path);
      auto catalog = source.catalog();
      std::ostringstream out;
      out << "{\"resources\":" << source.count() << ",\"assets\":[";
      size_t models = 0, images = 0, failures = 0, triangles = 0;
      size_t rigs = 0, animated = 0, clips = 0, poses = 0, stages = 0;
      size_t artwork = 0, thumbnails = 0;
      size_t scenery_clips = 0, scenery_samples = 0, scrolling_parts = 0,
             moving_parts = 0;
      size_t particle_emitters = 0, particle_stages = 0, particle_samples = 0;
      bool first = true;
      for (auto &a : catalog) {
        if (!first)
          out << ',';
        first = false;
        out << "{\"id\":" << a.id << ",\"kind\":" << int(a.kind)
            << ",\"name\":" << json(a.name);
        if (a.kind == sv::Kind::Stage) {
          ++stages;
          out << ",\"stage_id\":" << a.stage_id;
        }
        if (a.kind == sv::Kind::ConceptArt)
          out << ",\"art_group\":" << json(sv::art_group_name(a.art.group))
              << ",\"art_page\":" << a.art.number;
        if (validate) {
          try {
            if (is_image(a.kind)) {
              auto image = source.image(a);
              out << ",\"width\":" << image.width
                  << ",\"height\":" << image.height;
              ++images;
              if (a.kind == sv::Kind::ConceptArt) {
                ++artwork;
                source.image(a, true);
                ++thumbnails;
              }
            } else {
              auto m = source.model(a);
              triangles += m.triangles;
              ++models;
              out << ",\"bones\":" << m.bones.size()
                  << ",\"clips\":" << m.clips.size()
                  << ",\"unsupported_mesh_records\":" << m.unsupported
                  << ",\"animation_status\":" << json(m.animation_status);
              out << ",\"authored_normals\":" << m.authored_normals
                  << ",\"generated_normals\":" << m.generated_normals;
              out << ",\"source_lights\":" << m.lights.size()
                  << ",\"prelit_batches\":"
                  << std::count_if(m.parts.begin(), m.parts.end(),
                                   [](const auto &p) { return p.prelit; })
                  << ",\"reflection_batches\":"
                  << std::count_if(m.parts.begin(), m.parts.end(),
                                   [](const auto &p) {
                                     return p.reflection.texture >= 0;
                                   })
                  << ",\"gloss_batches\":"
                  << std::count_if(
                         m.parts.begin(), m.parts.end(),
                         [](const auto &p) { return p.gloss.texture >= 0; });
              out << ",\"scenery_clips\":" << m.scenery.size()
                  << ",\"scrolling_parts\":" << m.scrolling_parts()
                  << ",\"moving_parts\":" << m.moving_parts()
                  << ",\"scenery_status\":" << json(m.scenery_status);
              out << ",\"particle_commands\":"
                  << (m.particle_library ? m.particle_library->commands.size()
                                         : 0)
                  << ",\"particle_emitters\":" << m.particle_emitters.size()
                  << ",\"particle_status\":" << json(m.particle_status);
              if (!m.particle_status.empty())
                throw std::runtime_error(m.particle_status);
              particle_emitters += m.particle_emitters.size();
              particle_stages += !m.particle_emitters.empty();
              out << ",\"atmosphere_available\":"
                  << (m.atmosphere.available ? "true" : "false")
                  << ",\"atmosphere_status\":" << json(m.atmosphere_status)
                  << ",\"sky_parts\":"
                  << std::count_if(m.parts.begin(), m.parts.end(),
                                   [](auto &part) { return part.sky; });
              if (m.atmosphere.available) {
                const auto &fog = m.atmosphere.fog[0];
                out << ",\"fog_start\":" << fog.start
                    << ",\"fog_end\":" << fog.end
                    << ",\"fog_minimum\":" << fog.minimum
                    << ",\"fog_maximum\":" << fog.maximum;
              }
              scrolling_parts += m.scrolling_parts();
              moving_parts += m.moving_parts();
              auto check_geometry = [&] {
                for (auto &p : m.parts)
                  for (auto &v : p.vertices) {
                    if (!std::isfinite(v.x) || !std::isfinite(v.y) ||
                        !std::isfinite(v.z))
                      throw std::runtime_error("Non-finite vertex position");
                    float squared = v.nx * v.nx + v.ny * v.ny + v.nz * v.nz;
                    if (!std::isfinite(squared) ||
                        std::abs(squared - 1) > .001f)
                      throw std::runtime_error("Invalid vertex normal");
                  }
              };
              check_geometry();
              if (!m.bones.empty()) {
                ++rigs;
                if (m.clips.empty())
                  throw std::runtime_error("Animation unavailable: " +
                                           m.animation_status);
                ++animated;
                clips += m.clips.size();
              }
              for (size_t c = 0; c < m.clips.size(); ++c)
                for (float f : {0.f, float(m.clips[c].frames) * .5f,
                                float(m.clips[c].frames - 1)}) {
                  m.pose(int(c), f);
                  ++poses;
                  for (auto &b : m.bones)
                    for (float p : b.position)
                      if (!std::isfinite(p))
                        throw std::runtime_error("Non-finite animated joint");
                  check_geometry();
                }
              scenery_clips += m.scenery.size();
              for (size_t c = 0; c < m.scenery.size(); ++c)
                for (double time : {0., double(m.scenery[c].duration) * .5,
                                    double(m.scenery[c].duration)}) {
                  m.animate_scenery(int(c), time);
                  if (!m.particle_emitters.empty()) {
                    m.animate_particles(time, {1, 0, 0}, {0, 1, 0});
                    ++particle_samples;
                  }
                  check_geometry();
                  for (auto &part : m.parts)
                    for (float offset : part.uv_offset)
                      if (!std::isfinite(offset))
                        throw std::runtime_error(
                            "Invalid animated texture offset");
                  ++scenery_samples;
                }
            }
            out << ",\"valid\":true";
          } catch (const std::exception &e) {
            ++failures;
            out << ",\"error\":" << json(e.what());
          }
        }
        out << '}';
      }
      out << "],\"models_validated\":" << models
          << ",\"textures_validated\":" << images
          << ",\"triangles\":" << triangles << ",\"failures\":" << failures
          << ",\"rigged_models\":" << rigs
          << ",\"animated_models\":" << animated
          << ",\"clips_validated\":" << clips << ",\"poses_sampled\":" << poses
          << ",\"stages\":" << stages << ",\"artwork_validated\":" << artwork
          << ",\"artwork_thumbnails_validated\":" << thumbnails
          << ",\"scenery_clips_validated\":" << scenery_clips
          << ",\"scenery_samples\":" << scenery_samples
          << ",\"scrolling_parts\":" << scrolling_parts
          << ",\"moving_parts\":" << moving_parts
          << ",\"particle_emitters\":" << particle_emitters
          << ",\"particle_stages\":" << particle_stages
          << ",\"particle_samples\":" << particle_samples << "}\n";
      if (!report.empty()) {
        std::ofstream f(report);
        f << out.str();
        if (!f)
          throw std::runtime_error("Cannot write diagnostic report");
      }
      if (smoke) {
        auto it = std::find_if(catalog.begin(), catalog.end(),
                               [&](auto &a) { return a.id == asset_id; });
        if (it == catalog.end())
          throw std::runtime_error("Requested asset not found");
        auto m = is_image(it->kind)
                     ? texture_model(source.image(*it), it->name,
                                     it->kind == sv::Kind::ConceptArt)
                     : source.model(*it);
        Graphics g(true, true);
        sv::Renderer renderer(g.device.Get(), g.context.Get());
        sv::Camera camera;
        camera.frame(m);
        camera.yaw = m.stage ? .785398f : is_image(it->kind) ? 0 : 3.141593f;
        if (m.stage) {
          camera.pitch = .75f;
          renderer.grid = false;
          renderer.vertex_colors = true;
        }
        if (clip_index < -1 || !std::isfinite(animation_frame) ||
            !std::isfinite(scenery_time) || scenery_time < 0 ||
            (clip_index >= 0 &&
             size_t(clip_index) >=
                 (m.scenery.empty() ? m.clips.size() : m.scenery.size())))
          throw std::runtime_error("Invalid animation clip or frame");
        m.pose(clip_index, animation_frame);
        if (!m.scenery.empty())
          m.animate_scenery(std::max(clip_index, 0), scenery_time);
        camera.animate_particles(m, scenery_time);
        renderer.upload(m);
        renderer.resize(960, 720);
        renderer.draw(m, camera, is_image(it->kind));
        if (!capture.empty())
          sv::save_png(renderer.capture(), capture);
        std::cout << m.bones.size() << " bones / " << m.clips.size()
                  << " clips / " << m.animation_status << "\n";
        std::cout << "Hidden WARP render: " << m.triangles << " triangles\n";
      }
      std::cout << catalog.size() << " assets, " << models << " models and "
                << images << " textures validated, " << failures
                << " failures\n";
      return failures ? 1 : 0;
    }
    App app(ui_smoke);
    app.initial_asset = asset_id;
    app.require_initial_asset = asset_requested;
    if (!source_path.empty())
      app.open(source_path);
    if (ui_smoke) {
      if (source_path.empty() || capture.empty())
        throw std::runtime_error(
            "UI smoke test requires --source and --capture");
      app.ui_capture = capture;
      auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(60);
      while (!app.captured && std::chrono::steady_clock::now() < deadline) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
          TranslateMessage(&msg);
          DispatchMessageW(&msg);
        }
        app.frame();
        if (!app.error.empty())
          throw std::runtime_error(app.error);
      }
      if (!app.captured)
        throw std::runtime_error("UI smoke test timed out");
      std::cout << "Hidden viewer UI rendered successfully\n";
    } else
      app.run();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    if (!inspect && !validate && !smoke && !ui_smoke)
      MessageBoxA(nullptr, e.what(), "Shaolin Monks Recomp - 3D Viewer",
                  MB_OK | MB_ICONERROR);
    return 1;
  }
}

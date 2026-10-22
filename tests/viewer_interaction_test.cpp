#include <backends/imgui_impl_win32.h>
#include <cstdarg>
#include <cstdio>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <vector>
struct HierarchyRow {
  std::string name;
  ImVec2 minimum, maximum;
  bool selected, open;
};
static std::vector<HierarchyRow> hierarchy_rows;
static ImVec2 bgm_volume_min, bgm_volume_max;
namespace ImGui {
bool observed_tree(const char *id, ImGuiTreeNodeFlags flags, const char *format,
                   ...);
bool observed_slider(const char *label, float *value, float minimum,
                     float maximum, const char *format = "%.3f",
                     ImGuiSliderFlags flags = 0);
} // namespace ImGui
static float pointer_x = -100, pointer_y = -100, wheel = 0;
static bool pointer_buttons[3]{}, focused = true, up = false, down = false,
                                  fkey = false;
static int attack_key = -1;
static bool kombo_key = false;
static void test_frame();
#define ImGui_ImplWin32_NewFrame test_frame
#define TreeNodeEx observed_tree
#define SliderFloat observed_slider
#define wmain viewer_entrypoint
#include "../src/viewer/main.cpp"
#undef wmain
#undef ImGui_ImplWin32_NewFrame
#undef TreeNodeEx
#undef SliderFloat
bool ImGui::observed_slider(const char *label, float *value, float minimum,
                            float maximum, const char *format,
                            ImGuiSliderFlags flags) {
  bool changed = SliderFloat(label, value, minimum, maximum, format, flags);
  if (std::strcmp(label, "##BGM volume") == 0) {
    bgm_volume_min = GetItemRectMin();
    bgm_volume_max = GetItemRectMax();
  }
  return changed;
}
bool ImGui::observed_tree(const char *id, ImGuiTreeNodeFlags flags,
                          const char *format, ...) {
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  char label[256];
  std::vsnprintf(label, sizeof(label), format, copy);
  va_end(copy);
  bool open = TreeNodeExV(id, flags, format, args);
  va_end(args);
  hierarchy_rows.push_back({label, GetItemRectMin(), GetItemRectMax(),
                            (flags & ImGuiTreeNodeFlags_Selected) != 0, open});
  return open;
}
static void test_frame() {
  hierarchy_rows.clear();
  ImGui_ImplWin32_NewFrame();
  auto &io = ImGui::GetIO();
  io.AddFocusEvent(focused);
  io.AddKeyEvent(ImGuiKey_UpArrow, up);
  io.AddKeyEvent(ImGuiKey_DownArrow, down);
  io.AddKeyEvent(ImGuiKey_F, fkey);
  const ImGuiKey attacks[]{ImGuiKey_J, ImGuiKey_K, ImGuiKey_L, ImGuiKey_U,
                           ImGuiKey_I};
  for (int i = 0; i < 5; ++i)
    io.AddKeyEvent(attacks[i], attack_key == i);
  io.AddKeyEvent(ImGuiKey_O, kombo_key);
  io.AddMousePosEvent(pointer_x, pointer_y);
  for (int i = 0; i < 3; ++i)
    io.AddMouseButtonEvent(i, pointer_buttons[i]);
  if (wheel) {
    io.AddMouseWheelEvent(0, wheel);
    wheel = 0;
  }
}
void require(bool good, const char *message) {
  if (!good)
    throw std::runtime_error(message);
}
bool same_camera(const sv::Camera &a, const sv::Camera &b) {
  return a.yaw == b.yaw && a.pitch == b.pitch && a.distance == b.distance &&
         a.target == b.target && a.fov == b.fov;
}
struct Rect {
  float x, y, w, h;
};
Rect viewport(App &app) {
  auto *data = ImGui::GetDrawData();
  for (int l = 0; l < data->CmdListsCount; ++l) {
    auto *list = data->CmdLists[l];
    for (auto &cmd : list->CmdBuffer) {
      if (cmd.GetTexID() != reinterpret_cast<ImTextureID>(app.renderer.view()))
        continue;
      Rect r{1e9f, 1e9f, 0, 0};
      float right = -1e9f, bottom = -1e9f;
      for (unsigned i = 0; i < cmd.ElemCount; ++i) {
        auto &p =
            list->VtxBuffer[cmd.VtxOffset + list->IdxBuffer[cmd.IdxOffset + i]]
                .pos;
        r.x = std::min(r.x, p.x);
        r.y = std::min(r.y, p.y);
        right = std::max(right, p.x);
        bottom = std::max(bottom, p.y);
      }
      r.w = right - r.x;
      r.h = bottom - r.y;
      return r;
    }
  }
  throw std::runtime_error("No viewport image");
}
sv::Part quad(float x, int texture) {
  sv::Part p;
  p.name = "Synthetic object " + std::to_string(texture);
  p.texture = texture;
  p.mesh_id = texture;
  p.vertices = {{x - .7f, -.7f, 0, 0, 0}, {x + .7f, -.7f, 0, 1, 0},
                {x + .7f, .7f, 0, 1, 1},  {x - .7f, -.7f, 0, 0, 0},
                {x + .7f, .7f, 0, 1, 1},  {x - .7f, .7f, 0, 0, 1}};
  return p;
}

struct Fixture {
  fs::path path =
      fs::temp_directory_path() /
      ("shaolin-viewer-interaction-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  template <class T> static void put(sv::Bytes &b, size_t at, T value) {
    std::memcpy(b.data() + at, &value, sizeof(value));
  }
  Fixture() {
    fs::create_directory(path);
    sv::Bytes mesh(512);
    std::memcpy(mesh.data(), "2EMP", 4);
    put(mesh, 20, 1u);
    put(mesh, 24, 192u);
    put(mesh, 64, 22u);
    put(mesh, 76, 0u);
    std::memcpy(mesh.data() + 124, "Scenery", 8);
    put(mesh, 196, 3u);
    put(mesh, 200, 6u);
    put(mesh, 208, 3u);
    put(mesh, 212, UINT32_MAX);
    put(mesh, 256, 316u);
    put(mesh, 260, 364u);
    for (size_t v = 0; v < 3; ++v) {
      put(mesh, 316 + v * 16, int16_t(v == 0 ? 10 : 0));
      put(mesh, 318 + v * 16, int16_t(v == 1 ? 10 : 0));
      put(mesh, 320 + v * 16, int16_t(v == 2 ? 10 : 0));
      put(mesh, 328 + v * 16, 0xffffffffu);
    }
    put(mesh, 368, 16u);
    put(mesh, 376, 3u);
    put(mesh, 380, 384u);
    put(mesh, 384, uint16_t(0x8000));
    put(mesh, 386, uint16_t(1));
    put(mesh, 388, uint16_t(2));
    sv::Bytes wad(32 + 974 * 8);
    std::memcpy(wad.data(), "PWF ", 4);
    put(wad, 8, 2u);
    put(wad, 12, 974u);
    put(wad, 16, 0x100000u);
    for (uint32_t id : {913u, 933u, 953u, 973u}) {
      put(wad, 32 + id * 8, uint32_t(wad.size()));
      put(wad, 36 + id * 8, uint32_t(mesh.size()));
      wad.insert(wad.end(), mesh.begin(), mesh.end());
    }
    std::ofstream file(path / "GAMEDATA.WAD", std::ios::binary);
    file.write(reinterpret_cast<const char *>(wad.data()), wad.size());
    require(bool(file), "Cannot write synthetic fixture");
  }
  ~Fixture() {
    std::error_code e;
    fs::remove(path / "GAMEDATA.WAD", e);
    fs::remove(path, e);
  }
};
int wmain() {
  try {
    // All input goes through ImGui's queue. The window stays hidden, the
    // backend cannot poll controllers, and keyboard navigation never moves the
    // OS cursor.
    Fixture fixture;
    App app(true);
    auto &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.ConfigInputTrickleEventQueue = false;
    io.FontGlobalScale = 1.25f;
    app.source = std::make_unique<sv::Source>(fixture.path);
    app.assets = app.source->catalog();
    require(app.assets.size() == 4, "Synthetic stage catalog missing");
    app.assets[0].name = "Match 1";
    app.assets[1].name = "Skipped";
    app.assets[2].name = "Match 2";
    app.assets[3].name = "Match 3";
    app.select(0);
    require(app.renderer.lighting && app.renderer.bloom &&
                app.renderer.bloom_strength == .8f &&
                app.renderer.bloom_threshold == 0,
            "Loading a stage changed the requested lighting/bloom defaults");
    app.playing = app.scenery_playing = app.turntable = false;
    app.renderer.grid = app.renderer.joints = app.renderer.lighting = false;
    auto index = [&](uint32_t id) {
      for (size_t i = 0; i < app.assets.size(); ++i)
        if (app.assets[i].id == id)
          return int(i);
      throw std::runtime_error("Missing asset");
    };
    auto move = [&](float x, float y) {
      pointer_x = x;
      pointer_y = y;
      app.frame();
    };
    auto release = [&] {
      for (auto &b : pointer_buttons)
        b = false;
      app.frame();
    };
    auto click = [&](float x, float y) {
      move(x, y);
      pointer_buttons[0] = true;
      app.frame();
      release();
    };
    auto key = [&](bool &k) {
      k = true;
      app.frame();
      k = false;
      app.frame();
      app.frame();
    };
    app.model = {};
    app.model.name = "Synthetic selection fixture";
    app.model.textures = {{1, 1, {40, 80, 120, 255}},
                          {1, 1, {140, 100, 80, 255}}};
    app.model.parts = {quad(-1, 0), quad(1, 1)};
    app.rebuild_hierarchy();
    app.model.bounds();
    app.clip_index = -1;
    app.camera.frame(app.model);
    app.camera.pitch = 0;
    app.renderer.upload(app.model);
    app.frame();
    auto rect = viewport(app);
    auto point = [&](float x, float y, float z) {
      using namespace DirectX;
      auto p = XMVector3TransformCoord(
          XMVectorSet(x, y, z, 1), app.camera.view_projection(rect.w / rect.h));
      return ImVec2(rect.x + (XMVectorGetX(p) * .5f + .5f) * rect.w,
                    rect.y + (.5f - XMVectorGetY(p) * .5f) * rect.h);
    };
    auto p = point(-1, 0, 0);
    auto before = app.camera;
    click(p.x, p.y);
    require(app.renderer.selected_part == 0, "Click did not select object");
    require(same_camera(before, app.camera), "Click moved camera");
    auto row = [&](const std::string &name) {
      auto found = std::find_if(hierarchy_rows.begin(), hierarchy_rows.end(),
                                [&](auto &item) { return item.name == name; });
      require(found != hierarchy_rows.end(), "Missing hierarchy object");
      return *found;
    };
    app.frame();
    auto first = row("Synthetic object 0");
    require(first.selected && first.minimum.x > rect.x + rect.w,
            "Viewport selection was not reflected in the right hierarchy");

    click(p.x, p.y);
    require(app.renderer.selected_part == -1, "Second click did not deselect");
    click(p.x, p.y);
    click(rect.x + 5, rect.y + 5);
    require(app.renderer.selected_part == -1, "Empty click did not deselect");
    app.frame();
    require(std::none_of(hierarchy_rows.begin(), hierarchy_rows.end(),
                         [](auto &item) { return item.selected; }),
            "Empty-space deselection left a hierarchy row selected");
    auto second = row("Synthetic object 1");
    auto other = point(1, 0, 0);
    auto original = app.renderer.capture();
    click(second.minimum.x + ImGui::GetTreeNodeToLabelSpacing() + 5,
          (second.minimum.y + second.maximum.y) * .5f);
    app.frame();
    require(app.renderer.selected_part == 1 &&
                row("Synthetic object 1").selected,
            "Hierarchy click did not select the matching viewport object");
    auto highlighted = app.renderer.capture();
    size_t pixel =
        (size_t(other.y - rect.y) * original.width + size_t(other.x - rect.x)) *
        4;
    require(highlighted.rgba[pixel] > original.rgba[pixel] &&
                highlighted.rgba[pixel + 2] < original.rgba[pixel + 2],
            "Hierarchy selection did not apply the viewport's yellow overlay");
    first = row("Synthetic object 0");
    click(first.minimum.x - ImGui::GetStyle().ItemSpacing.x -
              ImGui::GetFrameHeight() * .5f,
          (first.minimum.y + first.maximum.y) * .5f);
    require(!app.model.parts[0].visible && app.renderer.selected_part == 1,
            "Hierarchy visibility toggle changed selection or failed to hide");
    click(first.minimum.x - ImGui::GetStyle().ItemSpacing.x -
              ImGui::GetFrameHeight() * .5f,
          (first.minimum.y + first.maximum.y) * .5f);
    require(app.model.parts[0].visible,
            "Hierarchy checkbox did not restore visibility");
    click(p.x, p.y);
    key(fkey);
    require(app.renderer.selected_part == 0 &&
                std::abs(app.camera.target[0] + 1) < .001f &&
                app.camera.distance < before.distance,
            "F did not focus selected object");

    app.renderer.selected_part = -1;
    key(fkey);
    require(std::abs(app.camera.target[0]) < .001f,
            "F without selection did not frame asset");
    move(-10, -10);
    pointer_buttons[0] = true;
    app.frame();
    before = app.camera;
    move(rect.x + rect.w * .5f, rect.y + rect.h * .5f);
    release();
    require(same_camera(before, app.camera) && app.renderer.selected_part == -1,
            "A gesture starting outside the canvas navigated or selected");
    move(rect.x + rect.w * .5f, rect.y + rect.h * .5f);
    before = app.camera;
    pointer_buttons[0] = true;
    app.frame();
    move(pointer_x + 30, pointer_y + 20);
    require(std::abs(app.camera.yaw - before.yaw - .24f) < .0001f &&
                std::abs(app.camera.pitch - before.pitch - .16f) < .0001f,
            "Mouse-only orbit direction/rate failed");
    release();
    require(app.renderer.selected_part == -1, "Drag selected an object");
    before = app.camera;
    pointer_buttons[2] = true;
    app.frame();
    move(pointer_x + 20, pointer_y + 15);
    release();
    require(app.camera.target != before.target && app.camera.yaw == before.yaw,
            "Mouse-only middle pan failed");
    before = app.camera;
    wheel = 1;
    app.frame();
    require(app.camera.distance < before.distance, "Mouse-only wheel failed");
    before = app.camera;
    pointer_buttons[1] = true;
    app.frame();
    move(pointer_x, pointer_y + 20);
    release();
    require(app.camera.distance > before.distance,
            "Mouse-only right dolly failed");
    pointer_buttons[0] = true;
    app.frame();
    before = app.camera;
    move(300, pointer_y);
    require(app.camera.yaw != before.yaw,
            "Drag did not continue outside viewport");
    release();
    before = app.camera;
    move(rect.x + rect.w * .5f, rect.y + rect.h * .5f);
    require(same_camera(before, app.camera), "Released drag kept moving");
    pointer_buttons[0] = true;
    app.frame();
    move(pointer_x + 20, pointer_y);
    focused = false;
    app.frame();
    require(app.camera_drag == -1, "Focus loss did not cancel drag");
    before = app.camera;
    move(pointer_x + 20, pointer_y);
    require(same_camera(before, app.camera), "Unfocused drag moved camera");
    release();
    focused = true;
    app.frame();
    // Pick a late object with two material batches while its hierarchy row is
    // outside the scroll area. Both batches must map back to one expanded row.
    app.select_part(-1);
    app.camera.yaw = app.camera.pitch = 0;
    app.camera.frame(app.model);
    app.model.parts = {quad(-1, 0)};
    for (uint32_t i = 0; i < 360; ++i) {
      auto part = quad(100 + float(i), 0);
      part.mesh_id = 100 + i;
      part.name = "Offscreen object " + std::to_string(i);
      app.model.parts.push_back(std::move(part));
    }
    auto last = quad(1, 1);
    last.name = "Last object / material A";
    last.mesh_id = 10000;
    app.model.parts.push_back(last);
    last.name = "Last object / material B";
    for (auto &vertex : last.vertices)
      vertex.x += 1.5f;
    app.model.parts.push_back(last);
    app.rebuild_hierarchy();
    require(app.hierarchy_objects.size() == 362 &&
                app.hierarchy_objects.back().parts.size() == 2,
            "Hierarchy did not group the object's material batches");
    app.renderer.upload(app.model);
    app.frame();
    other = point(1, 0, 0);
    click(other.x, other.y);
    app.frame();
    app.frame();
    auto late = row("Last object");
    ImGuiWindow *objects = nullptr;
    for (auto *window : ImGui::GetCurrentContext()->Windows)
      if (std::strstr(window->Name, "/Objects_"))
        objects = window;
    require(objects && late.selected && late.open &&
                late.minimum.y >= objects->InnerRect.Min.y &&
                late.maximum.y <= objects->InnerRect.Max.y &&
                objects->Scroll.y > 0,
            "Viewport picking did not select, expand and reveal the hierarchy "
            "object");
    move((objects->InnerRect.Min.x + objects->InnerRect.Max.x) * .5f,
         (objects->InnerRect.Min.y + objects->InnerRect.Max.y) * .5f);
    wheel = -100;
    app.frame();
    app.frame();
    late = row("Last object");
    float stride =
        std::ceil(ImGui::GetFrameHeight()) + ImGui::GetStyle().ItemSpacing.y;
    click(late.minimum.x + ImGui::GetStyle().IndentSpacing +
              ImGui::GetTreeNodeToLabelSpacing() + 5,
          late.minimum.y + stride * 2 + ImGui::GetFrameHeight() * .5f);
    int last_batch = int(app.model.parts.size()) - 1;
    require(app.renderer.selected_part == last_batch,
            "Selecting a material row did not select its viewport object");
    key(fkey);
    require(std::abs(app.camera.target[0] - 1.75f) < .001f,
            "F from the hierarchy did not fit every visible object material");
    late = row("Last object");
    float visibility_x = late.minimum.x - ImGui::GetStyle().ItemSpacing.x -
                         ImGui::GetFrameHeight() * .5f;
    float visibility_y = (late.minimum.y + late.maximum.y) * .5f;
    click(visibility_x, visibility_y);
    require(!app.model.parts[last_batch].visible &&
                !app.model.parts[last_batch - 1].visible &&
                app.renderer.selected_part == last_batch,
            "Object visibility did not hide all materials while preserving "
            "selection");
    click(visibility_x, visibility_y);
    require(app.model.parts[last_batch].visible &&
                app.model.parts[last_batch - 1].visible,
            "Object visibility did not restore every material");
    // Follow actual ImGui keyboard focus through a filtered stage list.
    app.category = 2;
    std::strcpy(app.search, "Match");
    app.select(index(913));
    app.frame();
    app.frame();
    click(bgm_volume_min.x + (bgm_volume_max.x - bgm_volume_min.x) * .25f,
          (bgm_volume_min.y + bgm_volume_max.y) * .5f);
    require(app.music.volume() > .15f && app.music.volume() < .35f,
            "BGM volume slider did not change the playback level");
    float volume = app.music.volume();
    app.camera.pan(17, -13, 400);
    app.camera.orbit(8, -4);
    app.camera.zoom(.8f);
    app.camera.fov = 62;
    before = app.camera;
    ImGuiWindow *list = nullptr;
    for (auto *w : ImGui::GetCurrentContext()->Windows)
      if (std::strstr(w->Name, "Asset list")) {
        list = w;
        break;
      }
    require(list, "Missing asset list");

    click(list->WorkRect.Min.x + 25, list->WorkRect.Min.y + 8);
    require(app.assets[app.selected].id == 913,
            "Cannot focus first filtered stage");
    key(down);
    require(app.assets[app.selected].id == 953,
            "Down did not load next filtered stage");
    require(same_camera(before, app.camera) &&
                app.camera.radius == before.radius &&
                app.music.volume() == volume,
            "Changing stages reset the camera or BGM volume");
    key(down);
    require(app.assets[app.selected].id == 973,
            "Second Down did not load stage");
    key(up);
    require(app.assets[app.selected].id == 953,
            "Up did not load previous stage");
    require(same_camera(before, app.camera),
            "Repeated stage changes reset position, rotation or zoom");
    app.frame_target();
    require(!same_camera(before, app.camera),
            "Explicit framing stopped working after preserving a stage camera");
    require(app.renderer.selected_part == -1,
            "Loading a stage retained an old object selection");
    app.model = texture_model({1, 1, {80, 120, 160, 255}}, "Synthetic image");
    app.texture_view = app.image_fit = true;
    app.renderer.upload(app.model);
    app.frame();
    app.frame();
    rect = viewport(app);
    move(rect.x + rect.w * .5f, rect.y + rect.h * .5f);
    pointer_buttons[0] = true;
    app.frame();
    before = app.camera;
    move(pointer_x + 30, pointer_y + 20);
    release();
    require(app.texture_view && app.camera.target != before.target &&
                app.camera.yaw == before.yaw,
            "Art pan changed");
    wheel = 1;
    before = app.camera;
    app.frame();
    require(app.camera.distance < before.distance, "Art zoom changed");
    key(fkey);
    require(!same_camera(before, app.camera), "Art fit failed");
    app.model = {};
    app.model.bones = {{"pelvis", -1}};
    app.model.parts = {quad(0, 0)};
    app.model.textures = {{1, 1, {200, 200, 200, 255}}};
    const char *actions[]{"a00_stnd", "g00_st_q", "i00_st_p", "k10_st_m",
                          "j00_st_d", "l10_sp_q", "g01_st_q"};
    for (const char *name : actions) {
      sv::Clip clip;
      clip.name = name;
      clip.frames = 60;
      clip.bones = {0};
      clip.keys = {{0, {sv::Pose{}}}, {59, {sv::Pose{}}}};
      app.model.clips.push_back(clip);
    }
    app.model.debug_meter = 1;
    app.model.bounds();
    app.assets[app.selected].kind = sv::Kind::Character;
    app.category = 1;
    app.character_debug = true;
    app.texture_view = false;
    app.debug.begin(app.model, app.model.textures[0], {});
    app.debug_camera();
    require(std::abs(app.camera.yaw + 1.570796327f) < .0001f,
            "Character debug did not start looking from left to right");
    app.renderer.upload(app.model);
    app.rebuild_hierarchy();
    app.frame();
    auto control_view = viewport(app);
    click(control_view.x + control_view.w / 2,
          control_view.y + control_view.h / 2);
    for (int kind = 0; kind < 5; ++kind) {
      app.debug.reset();
      attack_key = kind;
      app.frame();
      require(
          app.debug.attack_clip == kind + 1,
          "A mapped attack key did not execute its source clip in the canvas");
      attack_key = -1;
      app.frame();
    }
    app.debug.reset();
    kombo_key = true;
    app.frame();
    require(app.debug.attack_clip == 1 &&
                app.debug.attack_queue == std::vector<int>{6},
            "O did not execute and queue the source quick kombo");
    kombo_key = false;
    app.frame();
    app.debug.end(app.model);
    std::cout << "TOTAL Hidden UI click/toggle/empty selection, F focus, "
                 "mouse-only orbit/pan/dolly, gesture ownership, focus loss, "
                 "hierarchy selection/highlight/visibility/scrolling, filtered "
                 "stage Up/Down loading, and image controls passed"
              << std::endl;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "ERROR " << e.what() << std::endl;
    return 1;
  }
}

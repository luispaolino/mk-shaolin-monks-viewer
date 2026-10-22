#include "character_debug.h"
#include "renderer.h"
#include <iostream>
using namespace shaolin::viewer;
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
void put(Bytes &b, size_t at, auto value) {
  std::memcpy(b.data() + at, &value, sizeof(value));
}
struct Executable {
  Bytes data = Bytes(0x6400);
  struct Section {
    uint32_t va, raw, size;
  };
  std::array<Section, 4> sections{{{0x14c000, 0x400, 0x2000},
                                   {0x500000, 0x2400, 0x2000},
                                   {0xbf000, 0x4400, 0x1000},
                                   {0x600000, 0x5400, 0x1000}}};
  Executable() {
    std::memcpy(data.data(), "XBEH", 4);
    put(data, 0x104, 0x10000u);
    put(data, 0x11c, 4u);
    put(data, 0x120, 0x10180u);
    for (size_t i = 0; i < sections.size(); ++i) {
      put(data, 0x180 + i * 56 + 4, sections[i].va);
      put(data, 0x180 + i * 56 + 12, sections[i].raw);
      put(data, 0x180 + i * 56 + 16, sections[i].size);
    }
  }
  size_t at(uint32_t va) const {
    for (auto s : sections)
      if (va >= s.va && va - s.va < s.size)
        return s.raw + va - s.va;
    throw std::runtime_error("Invalid fixture address");
  }
  void immediate(uint32_t va, uint8_t reg, uint8_t field, float value) {
    size_t p = at(va);
    data[p] = 0xc7;
    data[p + 1] = reg;
    data[p + 2] = field;
    put(data, p + 3, value);
  }
  void springs() {
    immediate(0x14d805, 0x41, 0x60, .5f);
    immediate(0x14d80c, 0x41, 0x64, .3f);
    immediate(0x14d7fe, 0x41, 0x6c, 10);
    immediate(0x14d813, 0x41, 0x74, 1.14f);
    data[at(0x14d7d2)] = 0xba;
    put(data, at(0x14d7d3), 1.f);
    immediate(0x14cf4f, 0x40, 0x0c, 60);
    size_t p = at(0x14cf56);
    data[p] = 0xc7;
    data[p + 1] = 0x40;
    data[p + 2] = 0x18;
    put(data, p + 3, 0x500100u);
    for (int i = 0; i < 4; ++i)
      put(data, at(0x500100) + i * 4, i == 2 ? .5f : 1.f);
    data[at(0x500110)] = 1;
  }
};
void metadata_and_mapping() {
  Executable e;
  e.springs();
  auto settings = decode_joint_dynamics(e.data);
  require(settings.available && settings.stiffness == .5f &&
              settings.gravity == 10 && settings.hz == 60,
          "Original spring metadata was not decoded");
  put(e.data, e.at(0x14d808), .25f);
  require(decode_joint_dynamics(e.data).stiffness == .25f,
          "Spring settings were hardcoded");
  e.data[e.at(0x14cf56)] = 0;
  require(!decode_joint_dynamics(e.data).available &&
              !decode_joint_dynamics(std::span(e.data).first(12)).available,
          "Unsupported source invented spring settings");

  // A short body clip and an extended clip must not share a longest-table
  // mapping when extra joints have been inserted before the head.
  const char *names[] = {"pelvis", "head", "neck", "finger"};
  for (size_t i = 0; i < 4; ++i)
    std::memcpy(e.data.data() + e.at(0x500800) + i * 32, names[i],
                std::strlen(names[i]) + 1);
  auto table = [&](uint32_t va, std::initializer_list<int> indices) {
    size_t i = 0;
    for (int index : indices)
      put(e.data, e.at(va) + i++ * 4, 0x500800u + uint32_t(index * 32));
  };
  table(0x500400, {0, 1});
  table(0x500440, {0, 2, 1, 3});
  Model m;
  for (auto name : names)
    m.bones.push_back({name});
  m.clips.resize(2);
  m.clips[0].bones.resize(2, -1);
  m.clips[1].bones.resize(4, -1);
  map_animation_bones(m, e.data);
  require(m.clips[0].bones == std::vector<int>({0, 1}) &&
              m.clips[1].bones == std::vector<int>({0, 2, 1, 3}),
          "An extended table shifted a shorter clip");
  // Actor metadata disambiguates tables with identical names in another order.
  table(0x500480, {0, 1, 2, 3});
  size_t code = e.at(0xbf893);
  e.data[code] = 0x8d;
  e.data[code + 1] = 0x3c;
  e.data[code + 2] = 0xbd;
  put(e.data, code + 3, 0x500000u);
  put(e.data, e.at(0x500000) + 2 * 12, 0x500480u);
  put(e.data, e.at(0x500000) + 2 * 12 + 4, 4u);
  size_t record = e.at(0x600000);
  put(e.data, record, 0x20004u);
  put(e.data, record + 4, 1u);
  put(e.data, record + 12, 12345u);
  map_animation_bones(m, e.data, 12345);
  require(m.clips[1].bones == std::vector<int>({0, 1, 2, 3}),
          "Actor-specific facial order was replaced by a matching name set");
}
Model chain() {
  Executable e;
  e.springs();
  Model m;
  m.joint_dynamics = decode_joint_dynamics(e.data);
  m.bones = {{"head", -1, {0, -100, 0}},
             {"flap_start", 0, {0, -100, 0}},
             {"flap_01", 1, {0, -100, 10}}};
  Clip c;
  c.frames = 300;
  c.bones = {0};
  c.keys = {{0, {Pose{}}}};
  m.clips.push_back(c);
  m.pose(0, 0, false, true, 0);
  return m;
}
void dynamics() {
  auto m = chain();
  // Independent original-x86 force/integration/constraint trace, Y-down:
  // (-94.9018,10.1965), (-90.7502,6.66338), (-89.0796,3.27171).
  const float positions[][2] = {
      {94.9018f, 10.1965f}, {90.7502f, 6.66338f}, {89.0796f, 3.27171f}};
  for (int i = 0; i < 3; ++i) {
    m.pose(0, (i + 1) * .5f, false, true, 1.f / 60);
    require(std::abs(m.bones[2].position[1] - positions[i][0]) < .003f &&
                std::abs(m.bones[2].position[2] - positions[i][1]) < .003f,
            "Spring step differs from the recovered original integration");
  }
  auto extended = chain();
  extended.bones.push_back({"flap_02", 2, {0, -100, 20}});
  extended.pose(0, 0, false, true, 0);
  const float chain_positions[][2] = {
      {95, 20}, {87.3143f, 17.498f}, {81.347f, 11.8219f}};
  for (int i = 0; i < 3; ++i) {
    extended.pose(0, (i + 1) * .5f, false, true, 1.f / 60);
    require(
        std::abs(extended.bones[3].position[1] - chain_positions[i][0]) <
                .003f &&
            std::abs(extended.bones[3].position[2] - chain_positions[i][1]) <
                .003f,
        "A multi-segment chain differs from the original directed-link pass");
  }
  auto a = chain(), b = chain();
  for (int i = 1; i <= 60; ++i)
    a.pose(0, i * .5f, false, true, 1.f / 60);
  for (int i = 1; i <= 120; ++i)
    b.pose(0, i * .25f, false, true, 1.f / 120);
  for (size_t k = 0; k < 3; ++k)
    require(std::abs(a.bones[2].position[k] - b.bones[2].position[k]) < .001f,
            "Dynamic joints depend on render frame rate");
  auto stopped = a.bones[2].position;
  a.pose(0, 30, false, true, 0);
  require(a.bones[2].position == stopped, "Paused dynamic joints moved");
  a.joint_translation = {10, 0, 0};
  a.pose(0, 30.5f, false, true, 1.f / 60);
  require(a.bones[1].position[0] == 0 && a.bones[2].position[0] < -1,
          "Actor travel lost inertia or detached the root");
  a.pose(0, 31, false, false, 1.f / 60);
  require(a.secondary_velocity.empty() && a.bones[2].position[1] == 100,
          "Disabling dynamics did not restore the authored rig");
}
void attachment_skin() {
  auto m = chain();
  m.bones[1].bind[2] = 10;
  m.bones[2].bind[2] = 20;
  m.bones.push_back({"flap_02", 2, {0, -100, 30}});
  m.clips[0].frames = 30;
  m.clips[0].keys = {{0, {Pose{{0, 0, 512}}}}, {29, {Pose{{-150, 100, 700}}}}};
  Part hair;
  auto vertex = [&](uint16_t bone, std::array<float, 3> local) {
    hair.vertices.push_back({});
    Influence skin;
    skin.bone[0] = bone;
    skin.weight[0] = 1;
    skin.local[0] = local;
    hair.skin.push_back(skin);
  };
  vertex(1, {0, 0, 0});
  vertex(1, {0, 0, -10}); // The hair base reaches back to the scalp.
  vertex(1, {2, -4, 3});
  for (uint16_t bone : {2, 3}) {
    vertex(bone, {0, 0, 0});
    vertex(bone, {2, -4, 3});
  }
  hair.bind = hair.vertices;
  m.parts.push_back(hair);
  auto authored = m;
  auto close = [](float a, float b) { return std::abs(a - b) < .001f; };
  bool moved = false;
  // Exercise moving head poses, loop boundaries and render-only substeps.
  for (int step = 0; step < 432; ++step) {
    float frame = std::fmod(step * 30.f / 144, 30.f);
    authored.pose(0, frame);
    m.pose(0, frame, false, true, step ? 1.f / 144 : 0);
    const auto &a = authored.parts[0].vertices;
    const auto &v = m.parts[0].vertices;
    for (size_t i : {0u, 1u, 2u})
      require(close(v[i].x, a[i].x) && close(v[i].y, a[i].y) &&
                  close(v[i].z, a[i].z),
              "Dynamic hair rotated its animated scalp attachment");
    for (size_t i : {3u, 5u}) {
      require(close(v[i + 1].x - v[i].x, a[2].x - a[0].x) &&
                  close(v[i + 1].y - v[i].y, a[2].y - a[0].y) &&
                  close(v[i + 1].z - v[i].z, a[2].z - a[0].z),
              "Simulated skin lost the original attachment root basis");
      moved |= std::abs(v[i].y - a[i].y) > 1;
    }
  }
  require(moved, "Hair attachment check did not exercise spring motion");
}
void shadows() {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                          D3D11_SDK_VERSION, &device, nullptr, &context));
  Renderer r(device.Get(), context.Get());
  r.grid = r.joints = r.bloom = false;
  r.resize(256, 256);
  Model m;
  m.bones.push_back({"root"});
  m.textures = {{1, 1, {255, 255, 255, 255}}, {1, 1, {255, 255, 255, 255}}};
  Part floor;
  floor.role = MeshRole::DebugFloor;
  floor.texture = 0;
  const int corners[6][2]{{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
  for (auto &v : corners)
    floor.vertices.push_back(
        {v[0] * 5.f, 0, v[1] * 5.f, 0, 0, 0x80808080, 0, 1, 0});
  Part caster;
  caster.texture = 1;
  for (auto &v : corners)
    caster.vertices.push_back(
        {v[0] * .5f, v[1] + 1.f, 0, 0, 0, 0x80808080, 0, 0, 1});
  m.parts = {floor, caster};
  Camera c;
  c.target = {0, .6f, 0};
  c.distance = 7;
  c.pitch = .7f;
  c.radius = c.scene_radius = 4;
  auto draw = [&] {
    r.upload(m);
    r.draw(m, c);
    return r.capture();
  };
  r.shadows = false;
  auto unshadowed = draw();
  r.shadows = true;
  auto shadowed = draw();
  size_t darker = 0;
  for (size_t i = 0; i < shadowed.rgba.size(); i += 4)
    darker += shadowed.rgba[i] + 8 < unshadowed.rgba[i];
  require(darker > 100, "Directional light did not cast a shadow on the floor");
  r.light_azimuth = -r.light_azimuth;
  require(draw().rgba != shadowed.rgba,
          "Changing light direction did not move shadows");
  m.parts[1].visible = false;
  auto hidden = draw();
  r.shadows = false;
  require(hidden.rgba == draw().rgba, "Hidden geometry still cast a shadow");
  m.parts[1].visible = true;
  m.textures[1].rgba[3] = 0;
  r.shadows = true;
  auto transparent = draw();
  r.shadows = false;
  require(transparent.rgba == draw().rgba,
          "Transparent texels cast solid shadows");
}
} // namespace
int main() {
  try {
    metadata_and_mapping();
    dynamics();
    attachment_skin();
    shadows();
    std::cout << "Actor bone mapping, original spring trace, attached hair skin, "
                 "inertia and directional shadows passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

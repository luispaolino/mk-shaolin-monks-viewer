#include "camera.h"
#include <DirectXMath.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

using namespace shaolin::viewer;
namespace fs = std::filesystem;
namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
bool near(float a, float b) { return std::abs(a - b) < .0001f; }
template <class T> void put(Bytes &bytes, size_t at, T value) {
  std::memcpy(bytes.data() + at, &value, sizeof(value));
}
Bytes mesh(uint32_t format) {
  Bytes bytes(1024);
  std::memcpy(bytes.data(), "2EMP", 4);
  put(bytes, 20, 1u);
  put(bytes, 24, 188u);
  put(bytes, 64, 22u);
  put(bytes, 76, UINT32_MAX);
  put(bytes, 192, format == 2 ? 11u : 3u);
  put(bytes, 196, format);
  put(bytes, 204, 3u);
  put(bytes, 208, UINT32_MAX);
  put(bytes, 188 + (format == 2 ? 44 : 64), 400u);
  put(bytes, 188 + (format == 2 ? 48 : 68), 800u);
  size_t stride = format == 0 ? 44 : format == 1 ? 28 : format == 2 ? 64 : 16;
  for (size_t i = 0; i < 3; ++i) {
    size_t at = 400 + i * stride;
    // Distinct authored normals on one planar triangle must not be flattened.
    float x = i == 1 ? 1.f : 0.f, y = i == 2 ? 1.f : 0.f;
    float nx = i == 0 ? 1.f : 0.f, ny = i == 1 ? -1.f : 0.f;
    float nz = i == 2 ? 1.f : 0.f;
    if (format == 0) {
      put(bytes, at, x);
      put(bytes, at + 4, y);
      put(bytes, at + 12, nx * 2);
      put(bytes, at + 16, ny * 2);
      put(bytes, at + 20, nz * 2);
    } else {
      put(bytes, at, int16_t(x));
      put(bytes, at + 2, int16_t(y));
      if (format != 6) {
        put(bytes, at + 6, int16_t(nx * 4096));
        put(bytes, at + 8, int16_t(ny * 4096));
        put(bytes, at + 10, int16_t(nz * 4096));
      }
    }
  }
  put(bytes, 804, 16u);
  put(bytes, 812, 3u);
  put(bytes, 816, 820u);
  put(bytes, 820, uint16_t(0x8000));
  put(bytes, 822, uint16_t(1));
  put(bytes, 824, uint16_t(2));
  return bytes;
}
void normals(const Model &model) {
  for (auto &part : model.parts)
    for (auto &v : part.vertices)
      require(near(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz, 1),
              "Normal is not finite and normalized");
}
void decode_checks() {
  for (uint32_t format : {0u, 1u, 2u}) {
    auto model = decode_model(mesh(format));
    auto &v = model.parts.at(0).vertices;
    require(model.authored_normals == 3 && model.generated_normals == 0,
            "Original normals were not decoded");
    require(near(v[0].nx, 1) && near(v[1].ny, 1) && near(v[2].nz, 1),
            "Original normals or coordinate conversion changed");
    require(near(model.parts[0].bind[1].ny, 1), "Bind normal is missing");
    normals(model);
  }
  auto bytes = mesh(0);
  put(bytes, 188 + 44, 1.57079632679f);
  put(bytes, 188 + 24, 100.f);
  put(bytes, 188 + 28, 200.f);
  put(bytes, 188 + 32, -300.f);
  auto stage = decode_model(bytes, true);
  require(near(stage.parts[0].vertices[0].nx, 0) &&
              near(stage.parts[0].vertices[0].ny, -1) &&
              near(stage.parts[0].vertices[1].nx, 1),
          "Stage normal rotation or translation independence changed");
  auto compact = decode_model(mesh(6), true);
  require(compact.authored_normals == 0 && compact.generated_normals == 3,
          "Compact scenery needs fallback normals");
  normals(compact);
  for (auto &v : compact.parts[0].vertices)
    require(near(v.nz, -1), "Compact triangle normal is incorrect");
  auto legacy = mesh(6);
  put(legacy, 28, 33u);
  require(decode_model(legacy, true).legacy_winding,
          "Legacy compact export without direction bits was not identified");
  put(legacy, 820, uint16_t(0xc000));
  require(!decode_model(legacy, true).legacy_winding,
          "Explicit strip direction incorrectly selected heuristic repair");
  bytes = mesh(0);
  for (size_t i = 0; i < 3; ++i) {
    put(bytes, 400 + i * 44 + 12, std::numeric_limits<float>::quiet_NaN());
    put(bytes, 400 + i * 44, 0.f);
    put(bytes, 400 + i * 44 + 4, 0.f);
  }
  // Invalid float data is rejected before it can reach GPU buffers.
  bool rejected = false;
  try {
    decode_model(bytes);
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "Non-finite source normal accepted");
  auto degenerate = mesh(6);
  for (size_t i = 0; i < 3; ++i) {
    put(degenerate, 400 + i * 16, int16_t(0));
    put(degenerate, 400 + i * 16 + 2, int16_t(0));
  }
  normals(decode_model(degenerate, true));
}
void winding_checks() {
  for (uint32_t format : {0u, 1u, 6u}) {
    Bytes bytes(1536);
    std::memcpy(bytes.data(), "2EMP", 4);
    put(bytes, 20, 1u);
    put(bytes, 24, 188u);
    put(bytes, 64, 22u);
    put(bytes, 76, UINT32_MAX);
    put(bytes, 192, 3u);
    put(bytes, 196, format);
    put(bytes, 204, 16u);
    put(bytes, 208, UINT32_MAX);
    put(bytes, 252, 312u);
    put(bytes, 256, 1200u);
    size_t stride = format == 0 ? 44 : format == 1 ? 28 : 16;
    for (size_t i = 0; i < 16; ++i) {
      size_t at = 312 + i * stride;
      int x = int(i / 4 * 2 + (i & 1)), z = int((i % 4) / 2);
      if (format == 0) {
        put(bytes, at, float(x));
        put(bytes, at + 8, float(z));
        put(bytes, at + 16, -1.f);
      } else {
        put(bytes, at, int16_t(x));
        put(bytes, at + 4, int16_t(z));
        if (format == 1)
          put(bytes, at + 8, int16_t(-4096));
      }
    }
    // Reversed strips use both even and odd lengths. The following restart
    // must reset both parity and winding. All seven
    // triangles describe upward-facing floor surfaces in viewer coordinates.
    const uint16_t indices[] = {0x8000, 2, 1,  3,      0xc004, 5,  6, 7,
                                0xc008, 9, 10, 0x800c, 14,     13, 15};
    put(bytes, 1204, 16u);
    put(bytes, 1212, uint32_t(std::size(indices)));
    put(bytes, 1216, 1220u);
    std::memcpy(bytes.data() + 1220, indices, sizeof(indices));
    for (bool stage : {false, true}) {
      auto model = decode_model(bytes, stage);
      require(model.triangles == 7,
              "Strip restart omitted or joined triangles");
      for (auto &part : model.parts)
        for (size_t i = 0; i < part.vertices.size(); i += 3) {
          auto &a = part.vertices[i], &b = part.vertices[i + 1],
               &c = part.vertices[i + 2];
          float up = (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z);
          require(near(up, 1), "Strip winding hides a valid floor triangle");
          require(near(a.ny, 1) && near(b.ny, 1) && near(c.ny, 1),
                  "Face winding and stored or generated normals disagree");
        }
    }
  }
}
void animation_checks() {
  Model model;
  model.bones = {{"root", -1, {100, 200, 300}}, {"child", 0, {101, 200, 300}}};
  Part part;
  part.vertices = {{0, 0, 0, 0, 0, 0xffffffff, 1, 0, 0}};
  part.bind = part.vertices;
  Influence skin;
  skin.bone = {0, 1};
  skin.weight = {.5f, .5f};
  part.skin.push_back(skin);
  model.parts.push_back(part);
  Clip clip;
  clip.bones = {0, 1};
  clip.frames = 2;
  clip.keys.resize(1);
  clip.keys[0].pose.resize(2);
  clip.keys[0].pose[1].rotation[2] = 1024;
  model.clips.push_back(clip);
  model.pose(0, 0);
  auto &v = model.parts[0].vertices[0];
  require(near(v.nx, std::sqrt(.5f)) && near(v.ny, -std::sqrt(.5f)) &&
              near(v.nz, 0),
          "Weighted normal rotation is incorrect");
  normals(model);
  model.pose(0, 0);
  require(near(v.nx, std::sqrt(.5f)) && near(v.ny, -std::sqrt(.5f)),
          "Repeated posing accumulates normal rotation");
  model.pose(-1, 0);
  require(near(model.parts[0].vertices[0].nx, 1) &&
              near(model.parts[0].vertices[0].ny, 0),
          "Bind pose did not restore the original normal");
}
void timing_and_secondary_checks() {
  Model cyclic;
  cyclic.bones = {{"pelvis", -1}, {"foot", 0, {0, -1, 0}}};
  Clip loop;
  loop.frames = 4;
  loop.bones = {0, 1};
  loop.keys = {{0, {{{0, 0, 0}, {10, 0, 0}, true}, {}}},
               {2, {{{0, 0, 512}, {20, 0, 0}, true}, {}}}};
  cyclic.clips = {loop};
  cyclic.pose(0, 3.999f, false, false, 0, -1, 0, 1, false, true);
  auto end = cyclic.bones[1].position;
  cyclic.pose(0, .001f, false, false, 0, -1, 0, 1, false, true);
  for (size_t k = 0; k < 3; ++k)
    require(std::abs(end[k] - cyclic.bones[1].position[k]) < .01f,
            "Loop boundary introduced a one-frame pose/root discontinuity");
  cyclic.clips[0].common = true;
  for (auto &key : cyclic.clips[0].keys) {
    key.pose[1].translated = true;
    key.pose[1].translation = {0, -10, 0};
  }
  cyclic.pose(0, 0);
  require(near(cyclic.bones[1].position[1] - cyclic.bones[0].position[1], 1),
          "A shared animation stretched the target rig's limb length");
  Clip clip;
  clip.frames = 39;
  require(near(advance_animation_frame(clip, 0, .5), 15),
          "Slow rendering throttles the nominal animation clock");
  require(near(advance_animation_frame(clip, 35, .2, 2), 8),
          "Playback rate or looping changed elapsed time");
  float a = 0, b = 0;
  for (int i = 0; i < 30; ++i)
    a = advance_animation_frame(clip, a, 1. / 30);
  for (int i = 0; i < 120; ++i)
    b = advance_animation_frame(clip, b, 1. / 120);
  require(near(a, b) && near(a, 30), "Animation speed depends on render FPS");
  Model m;
  m.minimum = {-10, 0, -10};
  m.maximum = {10, 100, 10};
  m.bones = {{"head", -1, {0, -100, 0}},
             {"flap_start", 0, {0, -100, 0}},
             {"flap_01", 1, {0, -100, 10}},
             {"flap_02", 2, {0, -100, 20}}};
  clip.frames = 300;
  clip.bones = {0};
  clip.keys.resize(1);
  clip.keys[0].pose.resize(1);
  m.clips.push_back(clip);
  Part ribbon;
  ribbon.vertices = {{0, 100, 0, 0, 0, 0xffffffff, 0, 0, 1},
                     {0, 100, 20, 0, 0, 0xffffffff, 0, 0, 1}};
  ribbon.bind = ribbon.vertices;
  ribbon.skin.resize(2);
  ribbon.skin[0].bone[0] = 0;
  ribbon.skin[1].bone[0] = 3;
  ribbon.skin[0].weight[0] = ribbon.skin[1].weight[0] = 1;
  m.parts.push_back(ribbon);
  m.pose(0, 0, false, true, 0);
  auto head = m.bones[0].position;
  for (int i = 1; i <= 60; ++i)
    m.pose(0, float(i) / 2, false, true, 1.f / 60);
  require(m.bones[3].position[1] < 100 && m.bones[0].position == head,
          "Secondary motion is rigid or moves an anatomical joint");
  require(m.parts[0].vertices[1].y < 100 && near(m.parts[0].vertices[0].y, 100),
          "Secondary motion does not deform the ribbon skin independently");
  normals(m);
  for (size_t i = 2; i < 4; ++i) {
    float length = 0;
    for (size_t k = 0; k < 3; ++k) {
      float d = m.bones[i].position[k] - m.bones[i - 1].position[k];
      length += d * d;
    }
    require(std::abs(std::sqrt(length) - 10) < .001f,
            "Secondary motion stretches the original joint chain");
  }
  auto stopped = m.bones[3].position;
  m.pose(0, 30, false, true, 0);
  require(stopped == m.bones[3].position,
          "Paused secondary motion keeps advancing");
  auto seam = m;
  seam.clips[0].frames = 4;
  for (int i = 0; i <= 120; ++i)
    seam.pose(0, std::fmod(i * .5f, 4.f), false, true, 1.f / 60, -1, 0, 1,
              false, true);
  require(seam.bones[3].position[1] < 99.99f,
          "A looping clip reset secondary motion at the seam");
  m.pose(0, 31, false, true, 0);
  require(near(m.bones[3].position[1], 100),
          "Scrubbing retained a stale secondary pose");
  m.pose(0, 30);
  require(near(m.bones[3].position[1], 100) && m.secondary_positions.empty(),
          "Disabling secondary motion did not restore the authored pose");
  auto enlarged_bounds = m;
  enlarged_bounds.maximum[1] = 100000;
  for (int i = 0; i < 90; ++i) {
    m.pose(0, i * .5f, false, true, 1.f / 60);
    enlarged_bounds.pose(0, i * .5f, false, true, 1.f / 60);
  }
  require(m.bones[3].position == enlarged_bounds.bones[3].position,
          "Unrelated model bounds amplify headband gravity");
}
void secondary_stability_checks() {
  using namespace DirectX;
  Model fixture;
  fixture.bones.push_back({"head", -1, {0, -100, 0}});
  for (int chain = 0; chain < 2; ++chain)
    for (int segment = 0; segment < 5; ++segment) {
      std::string name =
          "flap_" + std::to_string(chain) + "_" + std::to_string(segment);
      int parent = segment ? int(fixture.bones.size()) - 1 : 0;
      fixture.bones.push_back(
          {name, parent, {chain ? 2.f : -2.f, -100, segment * 10.f}});
    }
  Clip clip;
  clip.frames = 61;
  clip.bones = {0};
  for (int f = 0; f <= 60; f += 5) {
    Pose head;
    head.rotation = {std::sin(f * .3f) * 600, std::cos(f * .2f) * 800,
                     std::sin(f * .4f) * 400};
    head.translation = {std::sin(f * .3f) * 90, -100, std::cos(f * .2f) * 60};
    head.translated = true;
    clip.keys.push_back({float(f), {head}});
  }
  fixture.clips.push_back(clip);
  auto load = [](const auto &p) { return XMVectorSet(p[0], p[1], p[2], 0); };
  for (bool parallel : {true, false})
    for (int hz : {15, 30, 60, 120}) {
      auto dynamic = fixture;
      if (!parallel)
        for (size_t segment = 1; segment < 5; ++segment)
          dynamic.bones[6 + segment].bind[0] += float(segment) * 8;
      auto authored = dynamic;
      float frame = 0;
      for (int step = 0; step < hz * 4; ++step) {
        frame = advance_animation_frame(clip, frame, 1. / hz);
        authored.pose(0, frame);
        dynamic.pose(0, frame, false, true, 1.f / hz);
        require(dynamic.bones[0].position == authored.bones[0].position,
                "Secondary simulation changed the animated head");
        for (size_t i = 1; i < dynamic.bones.size(); ++i) {
          int parent = dynamic.bones[i].parent;
          const auto &position = dynamic.bones[i].position;
          for (float component : position)
            require(std::isfinite(component),
                    "Secondary motion produced an invalid joint");
          if (parent == 0) {
            require(position == authored.bones[i].position,
                    "The headband attachment drifted from the head");
            continue;
          }
          auto a = load(authored.bones[i].position) -
                   load(authored.bones[parent].position);
          auto b = load(position) - load(dynamic.bones[parent].position);
          require(std::abs(XMVectorGetX(XMVector3Length(b)) -
                           XMVectorGetX(XMVector3Length(a))) < .001f,
                  "A fast head turn stretched a headband segment");
          require(XMVectorGetX(XMVector3Dot(XMVector3Normalize(a),
                                            XMVector3Normalize(b))) > .9395f,
                  "A headband segment exceeded its bend limit");
        }
        auto side = XMVector3Normalize(load(authored.bones[6].position) -
                                       load(authored.bones[1].position));
        for (size_t i = 1; parallel && i <= 5; ++i) {
          auto gap = load(dynamic.bones[i + 5].position) -
                     load(dynamic.bones[i].position);
          require(XMVectorGetX(XMVector3Dot(gap, side)) > 2.39f,
                  "Parallel headband tails crossed their separation corridors");
        }
      }
      auto paused = dynamic.bones.back().position;
      dynamic.pose(0, frame, false, true, 0);
      require(dynamic.bones.back().position == paused,
              "A constrained headband kept moving while paused");
      dynamic.pose(-1, 0);
      require(dynamic.secondary_rest.empty(),
              "Bind pose retained secondary motion history");
    }
}
void legacy_repair_checks() {
  using namespace DirectX;
  Model model;
  Part p;
  p.mesh_id = 1;
  p.vertices = {{0, 0, 0}, {0, 0, 1}, {1, 0, 0},
                {1, 0, 0}, {1, 0, 1}, {0, 0, 1}};
  // The first face points up, but the second shares the edge in the same
  // direction. Repair must retain both triangles and orient them together.
  p.bind = p.vertices;
  model.parts.push_back(p);
  require(model.repair_winding() == 1,
          "Legacy adjacent strips were not repaired");
  model.prepare_normals();
  for (auto &v : model.parts[0].vertices)
    require(near(v.ny, 1), "Legacy terrain still faces below the ground");
  model.set_winding_repair(false);
  require(model.parts[0].vertices[3].ny == -1,
          "Disabling legacy repair did not restore source orientation");
  model.set_winding_repair(true);
  model.flip_object(0);
  for (auto &v : model.parts[0].vertices)
    require(near(v.ny, -1), "Object face override did not reverse normals");
  require(model.parts[0].vertices.size() == 6 &&
              model.parts[0].bind[0].ny == -1,
          "Face repair changed triangle count or lost bind normals");
  Model authored;
  for (auto &v : p.vertices)
    v.ny = 1;
  authored.parts.push_back(p);
  require(authored.repair_winding() == 0,
          "Legacy terrain heuristic replaced authored normal evidence");
  Model shell;
  Part open;
  open.mesh_id = 9;
  const std::array<std::array<float, 3>, 4> quads[] = {
      {{{-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}, {1, -1, -1}}},
      {{{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}}},
      {{{-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}, {-1, -1, -1}}},
      {{{1, -1, -1}, {1, 1, -1}, {1, 1, 1}, {1, -1, 1}}},
      {{{-1, 1, -1}, {-1, 1, 1}, {1, 1, 1}, {1, 1, -1}}},
      {{{-1, -1, 1}, {-1, -1, -1}, {1, -1, -1}, {1, -1, 1}}}};
  for (size_t q = 0; q < std::size(quads); ++q)
    for (int i : {0, 2, 1, 0, 3, 2}) {
      if (q == 5 && open.vertices.size() >= 33)
        break;
      auto v = quads[q][i];
      open.vertices.push_back({v[0] + 1000, v[1] + 2000, v[2] - 4000});
    }
  open.bind = open.vertices;
  open.vertices.insert(
      open.vertices.end(),
      {{1002, 2002, -4000}, {1003, 2002, -4000}, {1002, 2003, -4000}});
  open.bind = open.vertices;
  shell.parts = {open};
  require(shell.repair_winding() == 12,
          "An inward open prop shell was not oriented from its opposing sides");
  for (size_t t = 0; t < 33; t += 3) {
    auto &v = shell.parts[0].vertices;
    auto a = DirectX::XMVectorSet(v[t].x, v[t].y, v[t].z, 0);
    auto b = DirectX::XMVectorSet(v[t + 1].x, v[t + 1].y, v[t + 1].z, 0);
    auto c = DirectX::XMVectorSet(v[t + 2].x, v[t + 2].y, v[t + 2].z, 0);
    auto n = DirectX::XMVector3Cross(b - a, c - a);
    require(DirectX::XMVectorGetX(DirectX::XMVector3Dot(
                n, a - DirectX::XMVectorSet(1000, 2000, -4000, 0))) > 0,
            "An open prop retained an inward-facing triangle");
  }
  const auto &v = shell.parts[0].vertices;
  require(v[34].y > v[33].y && v[35].x > v[33].x,
          "A small detached prop piece retained the reversed shell winding");
  Model legacy_open;
  legacy_open.stage = legacy_open.legacy_winding = true;
  for (auto &v : p.vertices)
    v.nx = v.ny = v.nz = 0;
  legacy_open.parts = {p};
  legacy_open.repair_winding();
  require(legacy_open.parts[0].two_sided &&
              legacy_open.parts[0].legacy_two_sided,
          "An open normal-less legacy shell has no inspection fallback");
  legacy_open.set_winding_repair(false);
  require(!legacy_open.parts[0].two_sided,
          "Original orientation comparison retained the legacy fallback");
  legacy_open.set_winding_repair(true);
  require(legacy_open.parts[0].two_sided,
          "Re-enabling legacy repair lost the open-shell fallback");
  for (auto &v : p.vertices)
    v.ny = 1;
  legacy_open.parts = {p};
  legacy_open.repair_winding();
  require(!legacy_open.parts[0].two_sided,
          "Authored normals were replaced with an ambiguous-shell fallback");
  open.vertices.clear();
  for (const auto &quad : quads)
    for (int i : {0, 1, 2, 0, 2, 3})
      open.vertices.push_back({quad[i][0], quad[i][1], quad[i][2]});
  open.bind = open.vertices;
  legacy_open.parts = {open};
  legacy_open.repair_winding();
  require(!legacy_open.parts[0].two_sided,
          "A closed solid was unnecessarily made two-sided");
}
void smooth_generated_checks() {
  Model m;
  m.stage = true;
  Part a, b, sharp;
  a.mesh_id = b.mesh_id = sharp.mesh_id = 4;
  a.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  b.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 1}};
  sharp.vertices = {{0, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  for (auto *p : {&a, &b, &sharp})
    p->bind = p->vertices;
  m.parts = {a, b, sharp};
  m.prepare_normals();
  auto &n = m.parts[0].vertices[0];
  require(near(n.ny, -1 / std::sqrt(5.f)) && near(n.nz, 2 / std::sqrt(5.f)),
          "Generated normals did not smooth across a material seam");
  require(near(m.parts[1].vertices[0].ny, n.ny) &&
              near(m.parts[2].vertices[0].nx, 1),
          "Smoothing lost a shared normal or rounded a sharp crease");
  require(m.authored_normals == 0 && m.generated_normals == 9 &&
              near(m.parts[0].bind[0].ny, n.ny),
          "Smoothed normals lost their generated count or bind data");
  for (auto &v : a.vertices)
    v.nx = 1;
  m.parts = {a, b};
  m.prepare_normals();
  require(m.authored_normals == 3 && near(m.parts[0].vertices[0].nx, 1),
          "Fallback smoothing replaced an authored normal");
  normals(m);
}
void camera_checks() {
  auto screen_x = [](const Camera &c, const std::array<float, 3> &point) {
    using namespace DirectX;
    auto target = XMVectorSet(c.target[0], c.target[1], c.target[2], 1);
    auto eye = target +
               XMVectorSet(std::sin(c.yaw) * std::cos(c.pitch) * c.distance,
                           std::sin(c.pitch) * c.distance,
                           std::cos(c.yaw) * std::cos(c.pitch) * c.distance, 0);
    auto matrix =
        XMMatrixLookAtLH(eye, target, XMVectorSet(0, 1, 0, 0)) *
        XMMatrixPerspectiveFovLH(XMConvertToRadians(c.fov), 1, .01f, 1000);
    return XMVectorGetX(XMVector3TransformCoord(
        XMVectorSet(point[0], point[1], point[2], 1), matrix));
  };
  Camera camera;
  camera.yaw = .7f;
  camera.pitch = .5f;
  camera.distance = 10;
  auto before = camera;
  camera.pan(40, 25, 800);
  float scale =
      2 * before.distance * std::tan(before.fov * 3.141592654f / 360) / 800;
  float sy = std::sin(before.yaw), cy = std::cos(before.yaw);
  float sp = std::sin(before.pitch), cp = std::cos(before.pitch);
  auto &t = camera.target;
  require(near(t[0] * sy * cp + t[1] * sp + t[2] * cy * cp, 0),
          "Panning moves along the camera depth axis");
  require(near(t[0] * cy - t[2] * sy, 40 * scale) &&
              near(-t[0] * sy * sp + t[1] * cp - t[2] * cy * sp, 25 * scale),
          "Panning is not aligned with screen axes");
  auto larger = before;
  larger.distance *= 2;
  larger.pan(40, 25, 1600);
  for (size_t k = 0; k < 3; ++k)
    require(near(larger.target[k], t[k]), "Pan scale ignores viewport height");
  camera = before;
  camera.orbit(10, 20);
  require(near(camera.yaw, .78f) && near(camera.pitch, .66f),
          "Orbit direction or speed changed");
  for (float yaw : {0.f, .7f, 3.141593f, -1.570796f}) {
    camera = before;
    camera.yaw = yaw;
    std::array<float, 3> foreground = {std::sin(yaw) * std::cos(camera.pitch),
                                       std::sin(camera.pitch),
                                       std::cos(yaw) * std::cos(camera.pitch)};
    float center = screen_x(camera, foreground);
    camera.orbit(10, 0);
    require(screen_x(camera, foreground) > center,
            "Right orbit drag moves the foreground left");
    camera.orbit(-20, 0);
    require(screen_x(camera, foreground) < center,
            "Left orbit drag moves the foreground right");
    camera = before;
    camera.yaw = yaw;
    center = screen_x(camera, {0, 0, 0});
    camera.pan(10, 0, 800);
    require(screen_x(camera, {0, 0, 0}) > center,
            "Right pan drag moves the image left");
  }
  camera.orbit(0, 10000);
  require(near(camera.pitch, 1.55f), "Upper orbit pole is not clamped");
  camera.orbit(0, -10000);
  require(near(camera.pitch, -1.55f), "Lower orbit pole is not clamped");
  camera.zoom(1);
  require(near(camera.distance, 10 * std::exp(-.08f)), "Wheel dolly changed");
  Model stage;
  stage.stage = true;
  stage.minimum = {-10000, -20000, -30000};
  stage.maximum = {10000, 20000, 30000};
  camera.frame(stage);
  require(near(camera.radius / 10000, std::sqrt(14.f)) &&
              near(camera.distance / camera.radius, 3.4f),
          "Stage framing does not account for all three extents");
  for (int i = 0; i < 20; ++i)
    camera.dolly(-10000);
  require(near(camera.distance / camera.radius, .02f),
          "Close dolly limit changed");
  for (int i = 0; i < 20; ++i)
    camera.dolly(10000);
  require(near(camera.distance / camera.radius, 10000),
          "Far dolly limit changed");
  camera = {};
  camera.pitch = camera.yaw = 0;
  camera.radius = .005f;
  camera.scene_radius = 50000;
  camera.distance = 10000;
  auto projection = camera.view_projection(1);
  auto depth = [&](float z) {
    return DirectX::XMVectorGetZ(DirectX::XMVector3TransformCoord(
        DirectX::XMVectorSet(0, 0, z, 1), projection));
  };
  require(depth(0) - depth(100) > 2.f / 16777215,
          "Dollying away from a tiny selection lost 24-bit depth precision");
}
} // namespace
int main() {
  try {
    decode_checks();
    winding_checks();
    animation_checks();
    timing_and_secondary_checks();
    secondary_stability_checks();
    legacy_repair_checks();
    smooth_generated_checks();
    camera_checks();
    std::cout << "Strip winding; original, transformed, fallback, animated and "
                 "normalized normals; "
                 "camera orbit, screen pan and dolly checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

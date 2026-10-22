#include "assets.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace shaolin::viewer;
namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
bool near(float a, float b) { return std::abs(a - b) < .0001f; }
template <class T> void put(Bytes &bytes, size_t at, T value) {
  std::memcpy(bytes.data() + at, &value, sizeof(value));
}
template <class T> void append(Bytes &bytes, T value) {
  size_t at = bytes.size();
  bytes.resize(at + sizeof(value));
  put(bytes, at, value);
}
void rejects(auto work) {
  bool rejected = false;
  try {
    work();
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "Malformed scenery animation was accepted");
}
void track(Bytes &bytes, uint32_t object, uint32_t parent, uint32_t playback,
           bool rotate = true) {
  for (uint32_t value : {28u, object, parent, 2u, 60u, playback, 0u})
    append(bytes, value);
  for (uint32_t frame : {0u, 60u}) {
    append(bytes, frame << 16 | 0x708u);
    append(bytes, frame * 80);
    for (float value :
         {frame ? 10.f : 0.f, frame ? 20.f : 0.f, frame ? 30.f : 0.f})
      append(bytes, value);
    float angle = frame && rotate ? std::sqrt(.5f) : 0;
    for (float value : {0.f, 0.f, angle, angle ? angle : 1.f})
      append(bytes, value);
    for (float value : {frame ? 2.f : 1.f, 1.f, frame ? .5f : 1.f})
      append(bytes, value);
  }
}
Bytes animation() {
  Bytes bytes(20);
  put(bytes, 0, 4u);
  put(bytes, 8, 20u);
  put(bytes, 12, 1u);
  put(bytes, 16, 1u);
  track(bytes, 10, 0, 0);
  for (uint32_t value : {12u, 100u, 1u})
    append(bytes, value);
  track(bytes, 10, 0, 1, false);
  put(bytes, 4, uint32_t(bytes.size()));
  return bytes;
}
Part part(uint32_t object) {
  Part part;
  part.object = object;
  part.scene_position = {100, 200, 300};
  part.scene_group = "Door";
  float n = std::sqrt(.5f);
  part.vertices = {{100, -200, 300, 0, 0, 0xffffffff, n, -n, 0},
                   {101, -200, 300, 1, 0, 0xffffffff, n, -n, 0},
                   {100, -201, 300, 0, 1, 0xffffffff, n, -n, 0}};
  part.bind = part.vertices;
  part.uv_speed = {.25f, -.5f};
  return part;
}
void decoding() {
  auto bytes = animation();
  auto clips = decode_scenery(bytes);
  require(clips.size() == 2 && clips[0].name == "Ambient" &&
              clips[0].tracks.size() == 1 && clips[1].group == 100,
          "Scenery track/group catalog changed");
  require(clips[0].tracks[0].keys[1].frame == 60 &&
              near(clips[0].duration, 61.f / 60),
          "Scenery frame timing changed");
  for (size_t size : std::array<size_t, 6>{0, 19, 20, 47, 80, bytes.size() - 1})
    rejects([&] { decode_scenery(std::span(bytes).first(size)); });
  auto bad = bytes;
  put(bad, 12, UINT32_MAX);
  rejects([&] { decode_scenery(bad); });
  bad = bytes;
  put(bad, 20, UINT32_MAX);
  rejects([&] { decode_scenery(bad); });
  bad = bytes;
  put(bad, 48, 0xf08u);
  rejects([&] { decode_scenery(bad); });
  bad = bytes;
  put(bad, 56, std::numeric_limits<float>::infinity());
  rejects([&] { decode_scenery(bad); });
  bad = bytes;
  for (size_t at = 68; at < 84; at += 4)
    put(bad, at, 0.f);
  rejects([&] { decode_scenery(bad); });
  bad = bytes;
  put(bad, 48 + 48, 0x708u);
  rejects([&] { decode_scenery(bad); });
  bad = bytes;
  put(bad, 28, 10u);
  rejects([&] { decode_scenery(bad); });
  bad = bytes;
  put(bad, 0, 3u);
  rejects([&] { decode_scenery(bad); });
  Bytes empty(20);
  put(empty, 0, 2u);
  put(empty, 4, 20u);
  put(empty, 8, 20u);
  require(decode_scenery(empty).empty(), "Empty legacy banks need no playback");
}
void playback() {
  Model model;
  model.parts.push_back(part(10));
  model.scenery = decode_scenery(animation());
  model.bind_scenery();
  require(model.moving_parts() == 1 && model.scrolling_parts() == 1 &&
              model.scenery[1].name == "Door",
          "Scenery binding or original group name is missing");
  model.animate_scenery(0, 0);
  require(near(model.parts[0].vertices[1].x, 101),
          "Time zero changed bind geometry");
  model.animate_scenery(0, 1);
  auto &p = model.parts[0];
  require(near(p.vertices[1].x, 110) && near(p.vertices[1].y, -222) &&
              near(p.vertices[1].z, 330),
          "Scenery rotation, scale, offset or coordinate basis is incorrect");
  require(near(p.vertices[1].nx, -2 / std::sqrt(5.f)) &&
              near(p.vertices[1].ny, -1 / std::sqrt(5.f)),
          "Scenery normal needs the inverse transpose for nonuniform scale");
  require(near(p.uv_offset[0], .25f) && near(p.uv_offset[1], -.5f),
          "Original U/V scrolling rates were not applied");
  auto expected = p.vertices;
  model.animate_scenery(0, 1);
  require(near(p.vertices[1].x, expected[1].x) &&
              near(p.vertices[1].ny, expected[1].ny),
          "Repeated scenery evaluation accumulates transforms");
  model.animate_scenery(0, .5);
  float root = std::sqrt(.5f);
  require(near(p.vertices[1].x, 105 + 1.5f * root) &&
              near(p.vertices[1].y, -210 - 1.5f * root),
          "Scenery translation or quaternion interpolation is incorrect");
  model.animate_scenery(0, 91. / 60);
  require(near(p.vertices[1].x, 105 + 1.5f * root),
          "Ambient track did not loop independently");
  model.animate_scenery(1, 3);
  require(near(p.vertices[1].x, 112) && near(p.vertices[1].y, -220),
          "Named one-shot group did not override ambient or hold its last key");
  model.animate_scenery(0, 1, false, true);
  require(p.uv_offset == std::array<float, 2>{0, 0} &&
              near(p.vertices[1].x, 110),
          "Texture toggle changed mesh playback");
  model.animate_scenery(0, 1, true, false);
  require(near(p.vertices[1].x, 101) && near(p.uv_offset[0], .25f),
          "Mesh toggle changed texture playback or failed to restore geometry");
  model.animate_scenery(-1, 2);
  require(near(p.vertices[1].x, 101) &&
              p.uv_offset == std::array<float, 2>{0, 0},
          "Static pose did not reset geometry and texture offsets");
  rejects([&] {
    model.animate_scenery(0, std::numeric_limits<double>::infinity());
  });
  model.scenery[0].tracks[0].keys[1].scale = {0, 0, 0};
  model.animate_scenery(0, 1);
  require(std::isfinite(p.vertices[0].nx) &&
              near(p.vertices[0].nx * p.vertices[0].nx +
                       p.vertices[0].ny * p.vertices[0].ny +
                       p.vertices[0].nz * p.vertices[0].nz,
                   1),
          "Collapsed scenery emitted an invalid normal");
}
void properties() {
  Bytes bytes(768);
  std::memcpy(bytes.data(), "2EMP", 4);
  put(bytes, 20, 1u);
  put(bytes, 24, 188u);
  put(bytes, 64, 22u);
  put(bytes, 76, UINT32_MAX);
  put(bytes, 192, 3u);
  put(bytes, 196, 6u);
  put(bytes, 204, 3u);
  put(bytes, 208, UINT32_MAX);
  put(bytes, 252, 312u);
  put(bytes, 256, 360u);
  put(bytes, 268, 123u);
  put(bytes, 328, int16_t(1));
  put(bytes, 348, int16_t(1));
  put(bytes, 360, 400u);
  put(bytes, 364, 16u);
  put(bytes, 372, 3u);
  put(bytes, 376, 380u);
  put(bytes, 380, uint16_t(0x8000));
  put(bytes, 382, uint16_t(1));
  put(bytes, 384, uint16_t(2));
  const char *names[] = {"USCROLLS", "VSCROLLS", "ANIMGROUP"};
  const char *values[] = {".8", "-.5]RENDERFLAGS", "Door"};
  for (size_t i = 0; i < 3; ++i) {
    size_t at = 400 + i * 40;
    put(bytes, at, i < 2 ? uint32_t(at + 40) : 0u);
    put(bytes, at + 4, 19u);
    std::memcpy(bytes.data() + at + 8, names[i], std::strlen(names[i]));
    std::memcpy(bytes.data() + at + 24, values[i], std::strlen(values[i]));
  }
  auto model = decode_model(bytes, true);
  require(model.parts[0].object == 123 &&
              near(model.parts[0].uv_speed[0], .8f) &&
              near(model.parts[0].uv_speed[1], -.5f) &&
              model.parts[0].scene_group == "Door",
          "Original mesh properties were not parsed");
  model.bind_scenery();
  require(model.scenery.size() == 1,
          "Texture-only scenery has no playback timeline");
  model.animate_scenery(0, 2);
  require(near(model.parts[0].uv_offset[0], .6f) &&
              near(model.parts[0].uv_offset[1], 0),
          "Texture-only scenery does not wrap deterministically");
  auto invalid = bytes;
  put(invalid, 480, 400u);
  rejects([&] { decode_model(invalid, true); });
}
void hierarchy() {
  Model model;
  model.parts = {part(10), part(20)};
  model.parts[1].scene_position = {0, 0, 0};
  for (auto &v : model.parts[1].bind) {
    v.x -= 100;
    v.y += 200;
    v.z -= 300;
  }
  model.parts[1].vertices = model.parts[1].bind;
  model.scenery = decode_scenery(animation());
  auto child = model.scenery[0].tracks[0];
  child.object = 20;
  child.parent = 10;
  for (auto &key : child.keys) {
    key.translation = {0, 0, 5};
    key.rotation = {0, 0, 0, 1};
    key.scale = {1, 1, 1};
  }
  model.scenery[0].tracks.push_back(child);
  model.bind_scenery();
  model.animate_scenery(0, 0);
  auto &v = model.parts[1].vertices[0];
  require(near(v.x, 100) && near(v.y, -200) && near(v.z, 305),
          "Scenery child did not inherit parent placement");
  model.animate_scenery(0, 1);
  require(near(v.x, 110) && near(v.y, -220) && near(v.z, 332.5f),
          "Scenery child did not inherit animated parent scale/rotation");
  model.scenery[0].tracks[0].parent = 20;
  rejects([&] { model.bind_scenery(); });
}
} // namespace
int main() {
  try {
    decoding();
    playback();
    properties();
    hierarchy();
    std::cout
        << "Scenery properties, map tracks/groups, interpolation, normals, "
           "looping, one-shots, scrubbing, toggles and malformed input "
           "passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

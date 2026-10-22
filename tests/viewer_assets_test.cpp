#include "assets.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace shaolin::viewer;
void require(bool b) {
  if (!b)
    throw std::runtime_error("Viewer regression failed");
}
void put(Bytes &b, size_t off, uint32_t v) {
  std::memcpy(b.data() + off, &v, 4);
}
void number(Bytes &b, size_t off, float f) {
  std::memcpy(b.data() + off, &f, 4);
}
void rejects(auto work) {
  bool failed = false;
  try {
    work();
  } catch (const std::exception &) {
    failed = true;
  }
  require(failed);
}
void half(Bytes &b, size_t off, uint16_t v) {
  std::memcpy(b.data() + off, &v, 2);
}
void packed(Bytes &b, size_t start, uint32_t bit, int v, unsigned width) {
  uint32_t bits = uint32_t(v) & ((1u << width) - 1);
  for (unsigned i = 0; i < width; ++i)
    if (bits & (1u << i))
      b[start + (bit + i) / 8] |= uint8_t(1u << ((bit + i) % 8));
}
void animation_checks() {
  Bytes bank(160);
  put(bank, 0, 16);
  put(bank, 4, 128);
  put(bank, 16, 16);
  put(bank, 128, 8);
  std::memcpy(bank.data() + 136, "sample", 7);
  size_t clip = 32;
  half(bank, clip, 11);
  half(bank, clip + 2, 11);
  half(bank, clip + 4, 2);
  half(bank, clip + 6, 1);
  half(bank, clip + 8, 16);
  put(bank, clip + 12, 2);
  bank[clip + 18] = 2;
  half(bank, clip + 24, 16);
  half(bank, clip + 26, 2);
  bank[clip + 33] = 1;
  size_t desc = clip + 36;
  half(bank, desc, 29);
  for (int i = 0; i < 6; ++i)
    bank[desc + 2 + i] = 6;
  half(bank, desc + 8, 65);
  bank[desc + 10] = 6;
  size_t keys = clip + 52;
  int values[] = {-8, 7, -4, 10, -20, 3};
  for (int k = 0; k < 2; ++k) {
    put(bank, keys + k * 16, k * 10);
    for (int i = 0; i < 6; ++i)
      packed(bank, keys + k * 16 + 4, 29 + i * 6, values[i] + k, 6);
  }
  packed(bank, keys + 4, 65, -13, 6);
  auto clips = decode_animations(bank);
  require(clips.size() == 1 && clips[0].name == "sample" &&
          clips[0].frames == 11 && clips[0].keys.size() == 2);
  require(clips[0].keys[0].pose[0].rotation[0] == -8 &&
          clips[0].keys[1].pose[0].translation[1] == -19);
  require(clips[0].keys[1].pose[1].rotation[0] == -13);
  auto bad = bank;
  half(bad, desc, 127);
  rejects([&] { decode_animations(bad); });
  bad = bank;
  put(bad, clip + 12, 0xffffffff);
  rejects([&] { decode_animations(bad); });
  bad.resize(100);
  rejects([&] { decode_animations(bad); });
  Model model;
  model.bones = {{"root", -1, {0, 0, 0}}, {"child", 0, {1, 0, 0}}};
  Part part;
  part.vertices = {{2, 0, 0, 0, 0}};
  part.bind = part.vertices;
  Influence skin;
  skin.bone[0] = 1;
  skin.weight[0] = 1;
  skin.local[0] = {1, 0, 0};
  part.skin.push_back(skin);
  model.parts.push_back(part);
  Clip motion;
  motion.bones = {0, 1};
  motion.frames = 11;
  motion.keys.resize(2);
  motion.keys[0].pose.resize(2);
  motion.keys[1].pose.resize(2);
  motion.keys[1].frame = 10;
  motion.keys[1].pose[0].rotation[2] = 1024;
  model.clips.push_back(motion);
  model.pose(0, 5);
  require(std::abs(model.parts[0].vertices[0].x - std::sqrt(2.f)) < .001f &&
          std::abs(model.parts[0].vertices[0].y + std::sqrt(2.f)) < .001f);
  model.pose(0, 5);
  require(std::abs(model.parts[0].vertices[0].x - std::sqrt(2.f)) < .001f);
  model.pose(-1, 0);
  require(model.parts[0].vertices[0].x == 2 &&
          model.parts[0].vertices[0].y == 0);
}
void skeleton_checks() {
  Bytes b(768);
  std::memcpy(b.data(), "2EMP", 4);
  put(b, 20, 1);
  put(b, 24, 188);
  put(b, 64, 22);
  put(b, 76, 0xffffffff);
  put(b, 188, 212);
  put(b, 192, 5);
  put(b, 196, 2);
  put(b, 204, 212);
  for (int i = 0; i < 2; ++i) {
    size_t p = 212 + i * 76;
    put(b, p, i ? 364 : 288);
    put(b, p + 4, 10);
    put(b, p + 8, i ? 0 : 0xffffffff);
    std::memcpy(b.data() + p + 12, i ? "hand" : "pelvis", i ? 5 : 7);
    number(b, p + 44, float(i * 10));
    number(b, p + 48, -10);
  }
  put(b, 368, 11);
  put(b, 372, 2);
  put(b, 376, 1);
  put(b, 380, 3);
  put(b, 384, 1);
  put(b, 404, 188);
  put(b, 408, 416);
  put(b, 412, 608);
  for (int i = 0; i < 3; ++i) {
    size_t v = 416 + i * 64;
    half(b, v, uint16_t(i == 1 ? 10 : 0));
    half(b, v + 2, uint16_t(i == 2 ? 10 : 0));
    half(b, v + 18, 4096);
    b[v + 26] = uint8_t(i == 1 ? 1 : 0);
    b[v + 27] = b[v + 28] = b[v + 29] = 255;
    half(b, v + 32, uint16_t(i == 2 ? 10 : 0));
  }
  put(b, 612, 16);
  put(b, 620, 3);
  put(b, 624, 628);
  half(b, 628, 0x8000);
  half(b, 630, 1);
  half(b, 632, 2);
  auto m = decode_model(b);
  require(m.bones.size() == 2 && m.bones[1].parent == 0 && m.bones[1].used &&
          m.parts[0].skin.size() == 3);
  require(m.origin[1] == 10 && m.parts[0].vertices[1].x == 10 &&
          m.parts[0].vertices[2].y == -10);
  require(m.grid_origin[0] == m.bones[0].bind[0] + m.origin[0] &&
          m.grid_origin[2] == m.bones[0].bind[2] + m.origin[2] &&
          m.grid_origin[1] == m.minimum[1]);
  auto grid = m.grid_origin;
  float grid_radius = m.grid_radius;
  for (auto &vertex : m.parts[0].vertices) {
    vertex.x += 100;
    vertex.y += 50;
  }
  m.bounds();
  require(m.grid_origin == grid && m.grid_radius == grid_radius);
  for (const char *name : {"body_cap", "l_bicep_b_cap", "HEAD_CAP"}) {
    auto capped = b;
    std::memcpy(capped.data() + 124, name, std::strlen(name) + 1);
    auto intact = decode_model(capped);
    // These vertices are inside the body's bounds and use its main skin.
    // Neither condition should enable wound caps in an intact pose.
    require(!intact.parts[0].visible && intact.parts[0].skin.size() == 3 &&
            intact.triangles == m.triangles &&
            intact.parts[0].vertices.size() == m.parts[0].vertices.size());
    intact.parts[0].visible = true;
    intact.pose(-1, 0);
    require(intact.parts[0].visible);
    require(decode_model(capped, true).parts[0].visible);
  }
  auto ordinary = b;
  std::memcpy(ordinary.data() + 124, "capstone", 9);
  require(decode_model(ordinary).parts[0].visible);
  auto bad = b;
  put(bad, 288 + 8, 8);
  rejects([&] { decode_model(bad); });
}
void legacy_packing_checks() {
  Bytes bank(128);
  put(bank, 0, 16);
  put(bank, 4, 112);
  put(bank, 16, 16);
  put(bank, 112, 4);
  std::memcpy(bank.data() + 116, "packed", 7);
  half(bank, 32, 6);
  half(bank, 34, 1);
  half(bank, 36, 1);
  put(bank, 40, 1);
  // Original type-6 extraction: signed high/middle/low ten-bit angles * 4.
  put(bank, 48, (17u << 20) | (uint32_t(-23) & 1023) << 10 | 31u);
  put(bank, 52, (1024u << 21) | (250u << 11) | 1024u);
  auto clip = decode_animations(bank).at(0);
  require(clip.encoding == 6 &&
          clip.keys[0].pose[0].rotation == std::array<float, 3>{68, -92, 124});
  require(clip.keys[0].pose[0].translation == std::array<float, 3>{0, -500, 0});
}
void root_checks() {
  auto close = [](float a, float b) { return std::abs(a - b) < .001f; };
  Model model;
  model.origin = {5, 7, -3};
  model.bones = {{"pelvis", -1, {100, -20, 200}},
                 {"foot", 0, {101, -20, 200}},
                 {"other_root", -1, {-50, -20, 400}}};
  Part part;
  part.vertices = {{107, 13, 197, 0, 0}};
  part.bind = part.vertices;
  Influence skin;
  skin.bone[0] = 1;
  skin.weight[0] = 1;
  skin.local[0] = {1, 0, 0};
  part.skin.push_back(skin);
  model.parts.push_back(part);
  Clip clip;
  // Tracks are deliberately not in skeleton order, and include two roots.
  clip.bones = {1, 2, 0};
  clip.frames = 21;
  std::array<std::array<float, 3>, 3> root = {
      {{1000, -30, -800}, {1015, -34, -780}, {1020, -30, -760}}};
  for (int i = 0; i < 3; ++i) {
    Key key;
    key.frame = float(i * 10);
    key.pose.resize(3);
    key.pose[0].translation =
        i == 1 ? std::array<float, 3>{-4, 4, 0} : std::array<float, 3>{1, 0, 0};
    key.pose[1].translation = {-100.f + i * 5, -20, 900.f + i * 10};
    key.pose[2].translation = root[i];
    for (auto &pose : key.pose)
      pose.translated = true;
    clip.keys.push_back(key);
  }
  model.clips.push_back(clip);
  for (float frame : {0.f, 5.f, 10.f, 15.f, 20.f, 5.f}) {
    model.pose(0, frame);
    auto &foot = model.parts[0].vertices[0];
    // Hip sway and bob are compensated by the planted leg. Freezing the
    // pelvis would instead slide this foot as the leg compensates.
    require(close(foot.x, 107) && close(foot.y, 23) && close(foot.z, 197));
    require(close(model.bones[2].position[0], -45) &&
            close(model.bones[2].position[2], 397));
  }
  model.pose(0, 10);
  require(close(model.bones[0].position[0], 110) &&
          close(model.bones[0].position[1], 27));
  model.pose(0, 20);
  require(close(model.bones[0].position[0], 105) &&
          close(model.bones[0].position[2], 197));
  for (float frame : {0.f, 10.f, 20.f, 10.f}) {
    model.pose(0, frame, true);
    auto &foot = model.parts[0].vertices[0];
    require(close(foot.x, 107 + frame) && close(foot.y, 23) &&
            close(foot.z, 197 + frame * 2));
  }
  model.pose(-1, 0);
  require(close(model.parts[0].vertices[0].x, 107) &&
          close(model.parts[0].vertices[0].y, 13));
}
int main() {
  try {
    legacy_packing_checks();
    animation_checks();
    skeleton_checks();
    root_checks();
    rejects([] { decode_model(Bytes{}); });
    rejects([] { decode_xpr(Bytes{}); });
    Bytes b(1024);
    std::memcpy(b.data(), "2EMP", 4);
    put(b, 20, 1);
    put(b, 24, 188);
    put(b, 64, 22);
    put(b, 76, 0xffffffff);
    auto at = 188u;
    put(b, at + 4, 3);
    put(b, at + 8, 0);
    put(b, at + 16, 4);
    put(b, at + 20, 0xffffffff);
    put(b, at + 64, 312);
    put(b, at + 68, 488);
    for (int i = 0; i < 4; ++i) {
      number(b, 312 + i * 44, float(i & 1));
      number(b, 316 + i * 44, float(i >> 1));
      put(b, 312 + i * 44 + 36, 0xffffffff);
    }
    put(b, 492, 16);
    put(b, 500, 4);
    put(b, 504, 508);
    uint16_t indices[] = {0x8000, 1, 2, 3};
    std::memcpy(b.data() + 508, indices, sizeof(indices));
    auto m = decode_model(b);
    require(m.triangles == 2 && m.parts.size() == 1 && m.maximum[0] == 1 &&
            m.minimum[1] == -1);
    auto invalid = b;
    put(invalid, at, at);
    rejects([&] { decode_model(invalid); });
    invalid = b;
    invalid[510] = 99;
    rejects([&] { decode_model(invalid); });
    invalid = b;
    put(invalid, at + 16, 0xffffffff);
    rejects([&] { decode_model(invalid); });
    Bytes xpr(44);
    std::memcpy(xpr.data(), "XPR0", 4);
    put(xpr, 8, 36);
    put(xpr, 24, (2 << 20) | (2 << 24) | (12 << 8));
    xpr[36] = 0x00;
    xpr[37] = 0xf8;
    auto image = decode_xpr(xpr);
    require(image.width == 4 && image.height == 4 && image.rgba[0] == 255 &&
            image.rgba[1] == 0 && image.rgba[2] == 0 && image.rgba[3] == 255);
    xpr.resize(40);
    rejects([&] { decode_xpr(xpr); });
    Bytes wad(40);
    std::memcpy(wad.data(), "PWF ", 4);
    put(wad, 12, 1);
    put(wad, 32, 39);
    put(wad, 36, 100);
    rejects([&] { members(wad); });
    Bytes ewdf(20);
    std::memcpy(ewdf.data(), "EWDF", 4);
    put(ewdf, 4, 0xffffffff);
    put(ewdf, 8, 20);
    rejects([&] { unpack(ewdf); });
    std::cout << "Viewer mesh, texture, compressed animation, skinning, and "
                 "corrupt-input checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what();
    return 1;
  }
}

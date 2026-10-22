#include "assets.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace shaolin::viewer;
namespace fs = std::filesystem;
namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class T> void put(Bytes &bytes, size_t at, T value) {
  std::memcpy(bytes.data() + at, &value, sizeof(value));
}
void text(Bytes &bytes, size_t at, const char *value) {
  std::memcpy(bytes.data() + at, value, std::strlen(value) + 1);
}
Bytes mesh() {
  Bytes bytes(1024);
  text(bytes, 0, "2EMP");
  put(bytes, 20, 2u);
  put(bytes, 24, 316u);
  for (size_t i = 0; i < 2; ++i) {
    size_t material = 60 + i * 128;
    put(bytes, material, i ? 0u : 188u);
    put(bytes, material + 4, 22u);
    put(bytes, material + 16, uint32_t(i));
    text(bytes, material + 64, i ? "TempTexure01" : "Scenery");
    size_t node = 316 + i * 228, vertices = node + 124;
    size_t indices = vertices + 48;
    put(bytes, node, i ? 0u : 544u);
    put(bytes, node + 4, 3u);
    put(bytes, node + 8, 6u);
    put(bytes, node + 16, 3u);
    put(bytes, node + 20, UINT32_MAX);
    put(bytes, node + 24, 10.f);
    put(bytes, node + 28, 20.f);
    put(bytes, node + 32, 30.f);
    put(bytes, node + 44, 1.57079632679f);
    put(bytes, node + 64, uint32_t(vertices));
    put(bytes, node + 68, uint32_t(indices));
    for (size_t v = 0; v < 3; ++v) {
      put(bytes, vertices + v * 16, int16_t(v == 0 ? 1 : 0));
      put(bytes, vertices + v * 16 + 2, int16_t(v == 1 ? 1 : 0));
      put(bytes, vertices + v * 16 + 4, int16_t(v == 2 ? 1 : 0));
      put(bytes, vertices + v * 16 + 6, uint16_t(i));
      put(bytes, vertices + v * 16 + 12, 0xffffffffu);
    }
    put(bytes, indices + 4, 16u);
    put(bytes, indices + 12, 3u);
    put(bytes, indices + 16, uint32_t(indices + 20));
    put(bytes, indices + 20, uint16_t(0x8000));
    put(bytes, indices + 22, uint16_t(1));
    put(bytes, indices + 24, uint16_t(2));
  }
  return bytes;
}
void write(const fs::path &path, const Bytes &bytes) {
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
  require(bool(file), "Cannot write synthetic stage fixture");
}
void material_checks() {
  auto bytes = mesh();
  put(bytes, 488, 768u);
  put(bytes, 772, 15u);
  put(bytes, 776, 33u);
  uint64_t mask = (uint64_t(1) << 0) | (uint64_t(1) << 7) |
                  (uint64_t(1) << 20) | (uint64_t(1) << 30);
  put(bytes, 784, mask);
  put(bytes, 792, 800u);
  put(bytes, 800, uint64_t(0x1000));
  put(bytes, 808, uint64_t(0x800));
  put(bytes, 816, uint64_t(0xc0400));
  put(bytes, 824, uint64_t(1));
  auto model = decode_model(bytes, true);
  require(model.parts[0].blend == BlendMode::Additive &&
              model.parts[0].emissive && !model.parts[0].depth_write &&
              model.parts[0].alpha_cutoff == 0 &&
              model.parts[1].blend == BlendMode::Alpha &&
              model.parts[1].depth_write,
          "Packed render attributes did not select the light-beam pass");
  put(bytes, 816, uint64_t(0x80000));
  model = decode_model(bytes, true);
  require(model.parts[0].blend == BlendMode::Alpha &&
              !model.parts[0].depth_write && model.parts[0].alpha_cutoff == 0 &&
              !model.parts[0].emissive,
          "Alpha-only effects lost their smooth fade");
  text(bytes, 124, "StatueEyes");
  model = decode_model(bytes, true);
  require(model.parts[0].emissive && model.parts[0].blend == BlendMode::Alpha &&
              !model.parts[0].depth_write,
          "The statue-eye preview lost its original alpha blend state");
  text(bytes, 124, "WindowGLOW");
  require(decode_model(bytes, true).parts[0].emissive,
          "Glow material recognition is not case insensitive");
  require(!decode_model(bytes).parts[0].emissive,
          "Stage glow inference changed ordinary prop inspection");
  put(bytes, 816, uint64_t(0x400));
  require(!decode_model(bytes, true).parts[0].emissive,
          "A material name alone classified an opaque surface as emission");
  text(bytes, 124, "Scenery");
  model = decode_model(bytes, true);
  require(model.parts[0].blend == BlendMode::Alpha &&
              model.parts[0].depth_write && model.parts[0].alpha_cutoff == .15f,
          "An ordinary scenery flag selected the light-beam pass");
  auto flicker = bytes;
  flicker.resize(1200);
  put(flicker, 768, 900u);
  const char *properties[]{"FLICKERMIN", "FLICKERMAX", "FLICKERSPEED"};
  const char *values[]{"21", "80", "1"};
  for (size_t i = 0; i < 3; ++i) {
    size_t p = 900 + i * 40;
    put(flicker, p, i < 2 ? uint32_t(p + 40) : 0u);
    put(flicker, p + 4, 19u);
    text(flicker, p + 8, properties[i]);
    text(flicker, p + 24, values[i]);
  }
  auto fm = decode_model(flicker, true);
  fm.prepare_materials();
  fm.bind_scenery();
  require(fm.parts[0].flicker && !fm.parts[0].depth_write &&
              fm.scenery.size() == 1,
          "Source opacity flicker did not get a translucent timeline");
  fm.animate_scenery(0, 2.5);
  require(std::abs(fm.parts[0].opacity - (21 + 59 * 1.28f) / 128) < .0001f,
          "Source flicker maximum changed");
  fm.animate_scenery(0, 7.5);
  require(std::abs(fm.parts[0].opacity - 21.f / 128) < .0001f,
          "Source flicker minimum changed");
  fm.animate_scenery(0, 2.5, false);
  require(std::abs(fm.parts[0].opacity - (21 + 59 * .64f) / 128) < .0001f,
          "Disabled material animation retained a stale opacity");
  put(bytes, 816, uint64_t(0x1000));
  model = decode_model(bytes, true);
  require(model.parts[0].role == MeshRole::ShadowReceiver &&
              model.parts[0].blend == BlendMode::Multiply &&
              !model.parts[0].depth_write && model.parts[0].alpha_cutoff == 0,
          "Shadow projection metadata selected an opaque white surface");
  put(bytes, 816, uint64_t(0x410));
  text(bytes, 124, "Sky dome");
  model = decode_model(bytes, true);
  require(model.parts[0].sky && model.parts[0].visible &&
              !model.parts[0].helper &&
              model.sky_origin == std::array<float, 3>{10, -20, 30},
          "The original sky flag did not identify a visible camera-relative "
          "background");
  require(!decode_model(bytes).parts[0].sky,
          "The stage sky flag changed ordinary prop decoding");
  put(bytes, 816, uint64_t(0x400));
  text(bytes, 124, "Scenery");
  put(bytes, 800, uint64_t(0x801));
  text(bytes, 252, "Scenery");
  model = decode_model(bytes, true);
  require(model.parts[0].helper && !model.parts[0].visible,
          "A hidden collision object was drawn as a solid wall");
  model = decode_model(bytes);
  require(model.parts[0].visible && !model.parts[0].helper,
          "Stage helper filtering changed ordinary prop inspection");
  // Attribute 30 can contain similar bits but is not a render mode.
  put(bytes, 784, mask & ~(uint64_t(1) << 20));
  put(bytes, 816, uint64_t(0xc0400));
  model = decode_model(bytes, true);
  require(model.parts[0].blend == BlendMode::Alpha &&
              model.parts[0].depth_write && model.parts[0].alpha_cutoff == .15f,
          "An absent render attribute read another attribute's flags");
  auto rejects = [&](const Bytes &bad) {
    bool rejected = false;
    try {
      decode_model(bad, true);
    } catch (const std::exception &) {
      rejected = true;
    }
    require(rejected, "Malformed render attributes were accepted");
  };
  auto bad = bytes;
  put(bad, 776, 65u);
  rejects(bad);
  bad = bytes;
  put(bad, 792, UINT32_MAX);
  rejects(bad);
  bad = bytes;
  put(bad, 792, 0u);
  rejects(bad);
  bad = bytes;
  put(bad, 768, 768u);
  rejects(bad);
}
struct Fixture {
  fs::path path =
      fs::temp_directory_path() /
      ("shaolin-viewer-stage-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  Fixture() { fs::create_directory(path); }
  ~Fixture() {
    // Remove only this test's exact synthetic files and empty directory.
    std::error_code error;
    fs::remove(path / "GAMEDATA.WAD", error);
    fs::remove(path / "default.xbe", error);
    fs::remove(path, error);
  }
};
void checks() {
  material_checks();
  auto bytes = mesh();
  auto stage = decode_model(bytes, true);
  require(stage.stage && stage.parts.size() == 2, "Stage geometry missing");
  require(stage.parts[0].mesh_id == 0 && stage.parts[1].mesh_id == 1 &&
              !stage.same_object(0, 1),
          "Separate source meshes share a selection ID");
  auto &v = stage.parts[0].vertices[0];
  require(std::abs(v.x - 10.f) < .001f && std::abs(v.y + 21.f) < .001f &&
              v.z == 30.f,
          "Static rotation and world translation were not applied");
  require(stage.parts[0].visible && !stage.parts[0].helper &&
              !stage.parts[1].visible && stage.parts[1].helper,
          "Stage helper visibility is incorrect");
  auto ordinary = decode_model(bytes);
  require(!ordinary.stage && ordinary.parts[0].vertices[0].x == 11.f &&
              ordinary.parts[0].vertices[0].y == -20.f &&
              ordinary.parts[1].visible,
          "Character and prop preview behavior changed");
  auto bad = bytes;
  put(bad, 316 + 44, std::numeric_limits<float>::quiet_NaN());
  bool rejected = false;
  try {
    decode_model(bad, true);
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "Non-finite stage rotation accepted");

  Fixture fixture;
  constexpr uint32_t count = 6028;
  Bytes wad(32 + count * 8);
  text(wad, 0, "PWF ");
  put(wad, 8, 2u);
  put(wad, 12, count);
  put(wad, 16, 0x100000u);
  for (uint32_t id : {913u, 933u, 1533u, 6027u}) {
    put(wad, 32 + id * 8, uint32_t(wad.size()));
    put(wad, 36 + id * 8, uint32_t(bytes.size()));
    wad.insert(wad.end(), bytes.begin(), bytes.end());
  }
  Bytes fog(140);
  put(fog, 0, 42u);
  put(fog, 4, 39u);
  put(fog, 8, 10u);
  put(fog, 12, 7000u);
  put(fog, 16, 70000u);
  put(fog, 20, 20u);
  put(fog, 24, 150u);
  put(wad, 32 + 915 * 8, uint32_t(wad.size()));
  put(wad, 36 + 915 * 8, uint32_t(fog.size()));
  wad.insert(wad.end(), fog.begin(), fog.end());
  write(fixture.path / "GAMEDATA.WAD", wad);
  Bytes executable(512);
  text(executable, 0, "XBEH");
  text(executable, 64, "This is map 1");
  text(executable, 80, "Tombs - Map 1");
  // Map 2 has no label and must remain selectable by its numeric map ID.
  text(executable, 160, "This is map 32");
  text(executable, 176, "Tombs - Map 32");
  write(fixture.path / "default.xbe", executable);
  Source source(fixture.path);
  auto catalog = source.catalog();
  require(catalog.size() == 4, "Synthetic stage catalog size changed");
  require(catalog[0].kind == Kind::Stage && catalog[0].stage_id == 1 &&
              catalog[0].name == "Tombs - Map 1  [913]",
          "Retail stage labels were not indexed");
  require(catalog[1].kind == Kind::Stage && catalog[1].stage_id == 2 &&
              catalog[1].name == "Stage 2  [933]",
          "An unnamed stage is unavailable");
  require(catalog[2].kind == Kind::Stage && catalog[2].stage_id == 32 &&
              catalog[3].kind != Kind::Stage && catalog[3].stage_id == 0,
          "Stage resource boundaries are incorrect");
  require(source.model(catalog[0]).stage && !source.model(catalog[3]).stage,
          "Stage selection did not select the correct decoding path");
  auto atmosphere = source.model(catalog[0]).atmosphere;
  require(atmosphere.available && atmosphere.fog[0].start == 7000 &&
              atmosphere.fog[0].color[0] == 42 / 255.f &&
              atmosphere.fog[0].maximum == 150 / 255.f &&
              !source.model(catalog[1]).atmosphere.available,
          "Stage loading did not associate its original atmosphere block");
  fs::remove(fixture.path / "default.xbe");
  Source without_labels(fixture.path);
  require(without_labels.catalog()[0].name == "Stage 1  [913]",
          "Stages require optional executable labels");
  text(bytes, 60 + 64, "TempTexure02");
  auto unused = decode_model(bytes, true);
  require(unused.parts[0].visible && unused.parts[1].visible,
          "A placeholder-only stage became invisible");
}
} // namespace
int main() {
  try {
    checks();
    std::cout
        << "Stage catalog, selection, placement, and helper checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

#include "renderer.h"
#include <iostream>
using namespace shaolin::viewer;
namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
Part quad(float z, int texture, float half = 2) {
  Part p;
  p.texture = texture;
  p.vertices = {{-half, -half, z, 0, 0}, {half, -half, z, 1, 0},
                {half, half, z, 1, 1},   {-half, -half, z, 0, 0},
                {half, half, z, 1, 1},   {-half, half, z, 0, 1}};
  p.bind = p.vertices;
  return p;
}
void checks() {
  Model metadata;
  metadata.stage = true;
  auto decal = quad(0, 0);
  for (size_t i = 0; i < decal.vertices.size(); ++i) {
    auto &v = decal.vertices[i];
    v.z = v.y;
    v.y = 0;
    v.color = uint32_t(i % 3 * 64) << 24;
  }
  decal.bind = decal.vertices;
  metadata.parts = {decal};
  metadata.prepare_materials();
  require(metadata.parts[0].role == MeshRole::ShadowOverlay &&
              !metadata.parts[0].depth_write,
          "A planar black vertex-alpha shadow was not identified");
  metadata.parts[0].role = MeshRole::Surface;
  for (auto &v : metadata.parts[0].bind)
    v.color |= 0x808080;
  metadata.prepare_materials();
  require(metadata.parts[0].role == MeshRole::Surface,
          "A colored transparent surface was mistaken for a shadow");
  Image mask{16, 4, Bytes(16 * 4 * 4)};
  for (size_t p = 0; p < mask.rgba.size(); p += 4) {
    unsigned value = 100 + (p / 4 % 16) * 10;
    mask.rgba[p] = mask.rgba[p + 1] = mask.rgba[p + 2] = uint8_t(value);
    mask.rgba[p + 3] = 255;
  }
  auto shaft = quad(0, 0);
  shaft.render_flags = 0x80000;
  shaft.depth_write = false;
  for (size_t i = 0; i < shaft.vertices.size(); ++i)
    shaft.vertices[i].color = uint32_t(i % 3 * 32) << 24 | 0x808080;
  shaft.bind = shaft.vertices;
  metadata.textures = {mask};
  metadata.parts = {shaft};
  metadata.prepare_materials();
  require(metadata.parts[0].role == MeshRole::LightShaft &&
              metadata.parts[0].emissive &&
              metadata.parts[0].blend == BlendMode::Additive,
          "An unnamed light-shaft mask was drawn as an opaque stripe");
  metadata.textures[0].rgba[2] = 0;
  metadata.parts = {shaft};
  metadata.prepare_materials();
  require(metadata.parts[0].role == MeshRole::Surface,
          "A colored diffuse map was mistaken for a light mask");
  auto solid = quad(0, 0);
  auto front = solid.vertices;
  auto back = solid.vertices;
  for (auto &v : back)
    v.z = 2;
  solid.vertices.insert(solid.vertices.end(), back.begin(), back.end());
  solid.vertices.insert(solid.vertices.end(), front.begin(), front.end());
  solid.vertices.insert(solid.vertices.end(), back.begin(), back.end());
  solid.bind = solid.vertices;
  solid.blend = BlendMode::Additive;
  solid.emissive = true;
  solid.render_flags = 0x40400;
  metadata.textures = {{1, 1, {100, 100, 100, 255}}};
  metadata.parts = {solid};
  metadata.prepare_materials();
  require(metadata.parts[0].role == MeshRole::SolidEffect &&
              metadata.parts[0].blend == BlendMode::Alpha &&
              !metadata.parts[0].emissive && metadata.parts[0].depth_write,
          "A solid effect-flagged prop remained additive and see-through");
  solid.depth_write = false;
  metadata.parts = {solid};
  metadata.prepare_materials();
  require(metadata.parts[0].blend == BlendMode::Additive,
          "A no-depth-write emission layer was made opaque");

  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                          D3D11_SDK_VERSION, &device, nullptr, &context));
  Renderer r(device.Get(), context.Get());
  require(r.lighting && r.bloom && r.bloom_strength == .8f &&
              r.bloom_threshold == 0,
          "Requested lighting and bloom defaults were not applied");
  r.bloom = false;
  r.bloom_strength = 1.1f;
  r.bloom_threshold = .45f;
  r.resize(128, 96);
  r.grid = r.joints = r.lighting = false;
  Camera c;
  c.pitch = c.yaw = 0;
  c.distance = 4;
  Model m;
  m.textures = {{1, 1, {20, 40, 60, 255}}, {1, 1, {255, 255, 255, 255}}};
  auto background = quad(0, 0);
  auto light = quad(.5f, 1, .14f);
  light.emissive = true;
  light.depth_write = false;
  light.alpha_cutoff = 0;
  m.parts = {background, light};
  auto render = [&](bool flat = false) {
    r.upload(m);
    r.draw(m, c, flat);
    return r.capture();
  };
  auto base = render();
  r.bloom = true;
  auto glow = render();
  r.lighting = true;
  auto lit = render();
  size_t center = (size_t(lit.height / 2) * lit.width + lit.width / 2) * 4;
  for (size_t k = 0; k < 3; ++k)
    require(lit.rgba[center + k] == glow.rgba[center + k],
            "Preview lighting dimmed an emissive alpha layer");
  r.lighting = false;
  size_t halo = 0;
  for (size_t p = 0; p < base.rgba.size(); p += 4)
    if (base.rgba[p] == 20 && base.rgba[p + 1] == 40 &&
        base.rgba[p + 2] == 60 && glow.rgba[p] > 21)
      ++halo;
  require(halo > 20, "Bloom did not extend beyond the emissive geometry");
  require(glow.rgba[0] == base.rgba[0],
          "Bloom wrapped across the image boundary");
  r.bloom_strength = 0;
  require(render().rgba == base.rgba, "Zero glow strength changed the scene");
  r.bloom_strength = 1.1f;
  r.bloom_threshold = 1;
  require(render().rgba == base.rgba,
          "The bloom threshold did not reject the source");
  r.bloom_threshold = .45f;
  m.parts[1].emissive = false;
  require(render().rgba == base.rgba,
          "Ordinary bright scenery was treated as emission");
  m.parts[1].emissive = true;
  r.selected_part = 1;
  auto selected = render();
  for (size_t p = 0; p < base.rgba.size(); p += 4)
    if (base.rgba[p] == 20 && base.rgba[p + 1] == 40 && base.rgba[p + 2] == 60)
      for (size_t k = 0; k < 3; ++k)
        require(selected.rgba[p + k] == glow.rgba[p + k],
                "Selection highlighting contaminated the glow mask");
  r.selected_part = -1;
  require(r.pick(m, c, 64, 48) == 1, "Bloom changed viewport picking");
  r.bloom = false;
  auto flat = render(true);
  r.bloom = true;
  require(render(true).rgba == flat.rgba, "Bloom changed a flat image preview");
  auto cover = quad(1, 0);
  m.parts = {background, light, cover};
  r.bloom = false;
  auto occluded = render();
  r.bloom = true;
  require(render().rgba == occluded.rgba,
          "Emission glowed through an opaque foreground object");
  m.textures.push_back({1, 1, {0, 0, 0, 128}});
  cover.texture = 2;
  cover.depth_write = false;
  cover.alpha_cutoff = 0;
  r.bloom_threshold = 0;
  auto glow_energy = [&] {
    r.bloom = false;
    auto scene = render();
    r.bloom = true;
    auto glowing = render();
    uint64_t energy = 0;
    for (size_t p = 0; p < scene.rgba.size(); p += 4)
      for (size_t k = 0; k < 3; ++k)
        energy +=
            std::max(0, int(glowing.rgba[p + k]) - int(scene.rgba[p + k]));
    return energy;
  };
  m.parts = {background, light};
  auto clear_energy = glow_energy();
  m.parts.push_back(cover);
  auto covered_energy = glow_energy();
  require(covered_energy < clear_energy * .65 &&
              covered_energy > clear_energy * .25,
          "A transparent foreground failed to attenuate the emission mask");
  m.parts = {background, light};
  m.textures[1].rgba[3] = 0;
  r.bloom = false;
  auto transparent = render();
  r.bloom = true;
  require(render().rgba == transparent.rgba,
          "Transparent source texels emitted bloom");
  m.textures[1].rgba[3] = 255;
  r.resize(67, 53);
  auto resized = render();
  require(resized.width == 67 && resized.height == 53,
          "Bloom did not resize with the viewport");

  r.bloom = false;
  m.stage = true;
  m.atmosphere.available = true;
  m.atmosphere.fog[0] = {{20 / 255.f, 40 / 255.f, 60 / 255.f}, 0, 8, 0, 1};
  auto shadow = quad(.5f, 1);
  shadow.role = MeshRole::ShadowOverlay;
  shadow.depth_write = false;
  shadow.alpha_cutoff = 0;
  for (auto &v : shadow.vertices)
    v.color = 0x40000000;
  m.parts = {background, shadow};
  auto image = render();
  size_t at = (size_t(image.height / 2) * image.width + image.width / 2) * 4;
  float opacity = 128.f / 255 * (1 - 3.5f / 8);
  for (size_t k = 0; k < 3; ++k)
    require(std::abs(image.rgba[at + k] - (20.f + 20 * k) * (1 - opacity)) < 2,
            "Fog painted a black shadow overlay with the stage color");
  r.vertex_colors = true;
  require(render().rgba == image.rgba,
          "The vertex RGB toggle changed shadow meaning");
  context->ClearState();
}
} // namespace
int main() {
  try {
    checks();
    std::cout << "Hidden WARP shadow recognition/fog, emission-only bloom, "
                 "threshold/strength, occlusion/alpha, picking, selection, "
                 "resize and flat preview checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

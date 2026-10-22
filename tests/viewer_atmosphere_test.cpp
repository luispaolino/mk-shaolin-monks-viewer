#include "renderer.h"
#include <array>
#include <iostream>

using namespace shaolin::viewer;
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
void put(Bytes &bytes, size_t at, int32_t value) {
  std::memcpy(bytes.data() + at, &value, sizeof(value));
}
Part quad(float z, int texture, float half = 2) {
  Part part;
  part.texture = texture;
  part.vertices = {{-half, -half, z, 0, 0}, {half, -half, z, 1, 0},
                   {half, half, z, 1, 1},   {-half, -half, z, 0, 0},
                   {half, half, z, 1, 1},   {-half, half, z, 0, 1}};
  return part;
}
void expect(const Image &image, std::array<float, 3> color,
            const char *message) {
  size_t offset =
      (size_t(image.height / 2) * image.width + image.width / 2) * 4;
  for (size_t k = 0; k < 3; ++k)
    require(std::abs(image.rgba[offset + k] - color[k]) < 2, message);
}
void checks() {
  Bytes block(140);
  int32_t values[] = {20, 40, 80, 2, 6, 0, 255};
  for (size_t i = 0; i < 7; ++i)
    put(block, i * 4, values[i]);
  put(block, 28 + 12, 3500);
  put(block, 28 + 16, 1000);
  put(block, 28 + 24, 128);
  auto atmosphere = decode_atmosphere(block);
  require(atmosphere.available && atmosphere.fog[0].active() &&
              atmosphere.fog[0].color[1] == 40 / 255.f &&
              atmosphere.fog[0].end == 6 && atmosphere.fog[0].maximum == 1 &&
              atmosphere.fog[1].start == 3500 &&
              atmosphere.fog[1].end == 1000 && !atmosphere.fog[4].active() &&
              !decode_atmosphere({}).available,
          "Original fog preset values or reversed ranges were lost");
  auto rejects = [&](const Bytes &bad) {
    bool rejected = false;
    try {
      decode_atmosphere(bad);
    } catch (const std::exception &) {
      rejected = true;
    }
    require(rejected, "Malformed fog settings were accepted");
  };
  auto bad = block;
  bad.pop_back();
  rejects(bad);
  bad = block;
  put(bad, 0, 256);
  rejects(bad);
  put(bad, 0, 20);
  put(bad, 12, -1);
  rejects(bad);
  bad = block;
  put(bad, 24, 256);
  rejects(bad);

  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                          D3D11_SDK_VERSION, &device, nullptr, &context));
  Renderer renderer(device.Get(), context.Get());
  renderer.resize(64, 64);
  renderer.grid = renderer.joints = renderer.lighting =
      renderer.cull_backfaces = false;
  Camera camera;
  camera.pitch = camera.yaw = 0;
  Model model;
  model.stage = true;
  model.atmosphere = atmosphere;
  model.textures = {{1, 1, {200, 100, 50, 255}}, {1, 1, {60, 90, 140, 255}}};
  model.parts = {quad(0, 0)};
  auto render = [&](bool flat = false) {
    renderer.upload(model);
    renderer.draw(model, camera, flat);
    return renderer.capture();
  };
  expect(render(), {110, 70, 65},
         "Linear distance fog uses the wrong depth or color");
  camera.distance = 1;
  expect(render(), {200, 100, 50}, "Fog affected geometry before its start");
  camera.distance = 10;
  expect(render(), {20, 40, 80}, "Far fog did not reach its authored limit");
  renderer.fog = false;
  expect(render(), {200, 100, 50},
         "Fog toggle did not restore the original surface");
  renderer.fog = true;
  expect(render(true), {200, 100, 50},
         "Atmosphere changed a flat image preview");
  camera.distance = 4;
  auto &fog = model.atmosphere.fog[0];
  fog.minimum = .2f;
  fog.maximum = .6f;
  expect(render(), {128, 76, 62}, "Fog ignored its minimum/maximum opacity");
  fog = atmosphere.fog[0];
  std::swap(fog.start, fog.end);
  expect(render(), {110, 70, 65},
         "An authored reversed fog range was reordered");
  fog.start = fog.end;
  expect(render(), {200, 100, 50},
         "An empty fog range produced invalid output");
  fog = atmosphere.fog[0];
  renderer.selected_part = 0;
  expect(render(),
         {110 * .62f + 255 * .38f, 70 * .62f + 229.5f * .38f, 65 * .62f},
         "Fog obscured the selection highlight");
  require(renderer.pick(model, camera, 32, 32) == 0,
          "Fog or stage color changed an object-picking ID");
  renderer.selected_part = -1;
  model.parts.clear();
  expect(render(), {20, 40, 80},
         "Stage fog color was not used as the background fallback");
  renderer.custom_background = true;
  renderer.background_color = {.1f, .2f, .3f};
  expect(render(), {25.5f, 51, 76.5f},
         "Custom sky/background color was ignored");
  renderer.background_color = {};
  auto beam = quad(0, 0);
  beam.blend = BlendMode::Additive;
  beam.depth_write = false;
  model.parts = {beam};
  expect(render(), {100, 50, 25},
         "Additive effects acquired a colored fog halo");
  renderer.custom_background = false;

  auto dome = quad(-4, 1);
  dome.sky = true;
  model.sky_origin = {100, 10, 200};
  for (auto &vertex : dome.vertices) {
    vertex.x += 100;
    vertex.y += 10;
    vertex.z += 200;
  }
  model.parts = {dome};
  renderer.lighting = true;
  auto sky = render();
  expect(sky, {60, 90, 140},
         "Sky was lit, fogged or left at its stored world position");
  camera.target = {1000, -100, 500};
  require(sky.rgba == render().rgba,
          "Sky moved with world-space camera translation");
  camera.yaw = 3.141592654f;
  expect(render(), {20, 40, 80},
         "Sky rotated with the camera instead of retaining world orientation");
  camera.yaw = 0;
  require(renderer.pick(model, camera, 32, 32) == -1,
          "A sky background blocked empty-space deselection");
  renderer.sky = false;
  expect(render(), {20, 40, 80},
         "Sky toggle did not reveal the stage background color");
  renderer.sky = true;
  camera.target = {};
  auto glow = dome;
  glow.texture = 0;
  glow.blend = BlendMode::Additive;
  glow.depth_write = false;
  for (auto &vertex : glow.vertices)
    vertex.color = 0x40808080;
  renderer.vertex_colors = true;
  model.parts = {glow, dome};
  float alpha = 128 / 255.f;
  auto layered = render();
  expect(layered, {60 + 200 * alpha, 90 + 100 * alpha, 140 + 50 * alpha},
         "Sky glow drew before the dome or acquired distance fog");
  model.parts = {dome, glow};
  require(layered.rgba == render().rgba,
          "Sky effect ordering depends on source order");
  model.textures.push_back({2, 1, {255, 0, 0, 255, 0, 0, 255, 255}});
  model.parts = {dome};
  model.parts[0].texture = 2;
  auto still = render();
  model.parts[0].uv_offset = {.25f, 0};
  require(still.rgba != render().rgba, "Sky texture scrolling was ignored");
  renderer.vertex_colors = false;
  renderer.lighting = renderer.fog = false;
  model.parts = {dome, quad(0, 0, .8f)};
  model.bounds();
  require(model.minimum[0] == -.8f && model.maximum[0] == .8f,
          "A detached sky dome expanded the map framing bounds");
  expect(render(), {200, 100, 50},
         "Sky covered solid scenery or wrote foreground depth");
  require(renderer.pick(model, camera, 32, 32) == 1,
          "Sky changed the selected foreground object");
  model.parts = {dome};
  model.parts[0].visible = false;
  expect(render(), {20, 40, 80},
         "Hierarchy visibility did not hide a sky object");
  context->ClearState();
}
} // namespace
int main() {
  try {
    checks();
    std::cout
        << "Original fog decoding, hidden WARP distance/opacity/color fog, "
           "additive attenuation, camera-relative sky, occlusion, framing, "
           "layering, scrolling, visibility and picking checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

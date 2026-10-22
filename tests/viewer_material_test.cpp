#include "renderer.h"
#include <array>
#include <iostream>

using namespace shaolin::viewer;
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
Part quad(float z, int texture, uint32_t color = 0x80808080) {
  Part part;
  part.texture = texture;
  part.vertices = {{-2, -2, z, 0, 0, color}, {2, -2, z, 1, 0, color},
                   {2, 2, z, 1, 1, color},   {-2, -2, z, 0, 0, color},
                   {2, 2, z, 1, 1, color},   {-2, 2, z, 0, 1, color}};
  return part;
}
std::array<int, 3> pixel(const Image &image) {
  size_t offset =
      (size_t(image.height / 2) * image.width + image.width / 2) * 4;
  return {image.rgba[offset], image.rgba[offset + 1], image.rgba[offset + 2]};
}
void expect_pixel(const Image &image, std::array<float, 3> expected,
                  const char *message) {
  auto actual = pixel(image);
  for (size_t k = 0; k < 3; ++k)
    require(std::abs(actual[k] - expected[k]) < 2, message);
}
void checks() {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  // WARP exercises the actual shader and pipeline without a window, physical
  // input, audio, or a dependency on the machine's graphics adapter.
  check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                          D3D11_SDK_VERSION, &device, nullptr, &context));
  Renderer renderer(device.Get(), context.Get());
  renderer.resize(64, 64);
  renderer.grid = renderer.joints = renderer.lighting = false;
  renderer.vertex_colors = true;
  Camera camera;
  camera.pitch = 0;
  camera.distance = 4;
  Model model;
  model.textures = {{1, 1, {40, 80, 120, 255}},
                    {1, 1, {200, 100, 50, 255}},
                    {1, 1, {10, 160, 20, 255}}};
  auto background = quad(0, 0);
  auto beam = quad(.5f, 1, 0x10808080);
  beam.blend = BlendMode::Additive;
  beam.depth_write = false;
  beam.alpha_cutoff = 0;
  auto render = [&] {
    renderer.upload(model);
    renderer.draw(model, camera);
    return renderer.capture();
  };
  model.parts = {background};
  expect_pixel(render(), {40, 80, 120}, "Solid geometry changed color");
  float alpha = 32.f / 255;
  model.parts = {beam, background};
  auto additive = render();
  expect_pixel(additive, {40 + 200 * alpha, 80 + 100 * alpha, 120 + 50 * alpha},
               "Light beam lost low-alpha fading or additive blending");
  model.parts = {background, beam};
  require(additive.rgba == render().rgba,
          "Effect pass depends on the source part order");
  model.parts = {beam};
  auto emission = render();
  renderer.lighting = true;
  require(emission.rgba == render().rgba,
          "Preview lighting darkens an additive light source");
  renderer.lighting = false;
  model.parts = {beam, quad(1, 2), background};
  expect_pixel(render(), {10, 160, 20},
               "Light leaks through a foreground solid");
  auto second_beam = beam;
  for (auto &vertex : second_beam.vertices)
    vertex.z += .5f;
  model.parts = {second_beam, background, beam};
  expect_pixel(render(),
               {40 + 400 * alpha, 80 + 200 * alpha, 120 + 100 * alpha},
               "An effect writes depth and clips another light beam");
  // Ordinary translucent effects still need far-to-near ordering, even when
  // every layer has depth writes disabled.
  model.textures[1].rgba = {200, 0, 0, 255};
  model.textures[2].rgba = {0, 200, 0, 255};
  beam = quad(.5f, 1, 0x20808080);
  beam.depth_write = false;
  beam.alpha_cutoff = 0;
  second_beam = quad(1, 2, 0x20808080);
  second_beam.depth_write = false;
  second_beam.alpha_cutoff = 0;
  model.parts = {second_beam, beam, background};
  auto sorted = render();
  model.parts = {background, beam, second_beam};
  require(sorted.rgba == render().rgba,
          "Alpha effects are not sorted by depth");
  alpha = 64.f / 255;
  float inverse = 1 - alpha;
  expect_pixel(sorted,
               {(40 * inverse + 200 * alpha) * inverse,
                80 * inverse * inverse + 200 * alpha, 120 * inverse * inverse},
               "Translucent effects use the wrong compositing order");
  // Moving effects must update their sort position along with the GPU data.
  for (auto &v : model.parts[1].vertices)
    v.z = 1.5f;
  renderer.update_pose(model);
  renderer.draw(model, camera);
  expect_pixel(renderer.capture(),
               {40 * inverse * inverse + 200 * alpha,
                (80 * inverse + 200 * alpha) * inverse,
                120 * inverse * inverse},
               "Animated effects retained their previous sort position");
  model.textures[1].rgba = {255, 255, 255, 255};
  auto shadow = quad(.5f, 1);
  shadow.blend = BlendMode::Multiply;
  shadow.depth_write = false;
  shadow.alpha_cutoff = 0;
  model.parts = {shadow, background};
  expect_pixel(render(), {40, 80, 120},
               "White shadow receiver obscures the floor");
  model.textures[1].rgba = {128, 128, 128, 255};
  expect_pixel(render(),
               {40 * 128.f / 255, 80 * 128.f / 255, 120 * 128.f / 255},
               "Baked shadow texture does not multiply the floor");
  renderer.vertex_colors = false;
  beam = quad(.5f, 1, 0x08808080);
  beam.depth_write = false;
  beam.alpha_cutoff = 0;
  model.textures[1].rgba = {200, 0, 0, 255};
  model.parts = {background, beam};
  alpha = 16.f / 255;
  expect_pixel(
      render(),
      {40 * (1 - alpha) + 200 * alpha, 80 * (1 - alpha), 120 * (1 - alpha)},
      "Disabling vertex RGB also disabled transparency");
  Model classified;
  beam.opacity = .25f;
  model.parts = {background, beam};
  alpha *= .25f;
  expect_pixel(
      render(),
      {40 * (1 - alpha) + 200 * alpha, 80 * (1 - alpha), 120 * (1 - alpha)},
      "Source per-object opacity was ignored");
  classified.textures = {{1, 1, {255, 255, 255, 64}}};
  classified.parts = {quad(0, 0)};
  classified.prepare_materials();
  require(classified.parts[0].alpha_cutoff == 0 &&
              !classified.parts[0].depth_write,
          "Gradient transparency is still alpha-tested or writes depth");
  // Sparse dark diffuse detail must not open holes in a solid wall. A real
  // mask or an explicitly translucent layer must retain its alpha channel.
  classified.stage = true;
  classified.textures = {{20, 1, Bytes(80, 255)}};
  for (size_t k = 0; k < 4; ++k)
    classified.textures[0].rgba[k] = 0;
  auto wall = quad(.5f, 0);
  wall.bind = wall.vertices;
  classified.parts = {wall};
  classified.prepare_materials();
  require(!classified.parts[0].texture_alpha &&
              classified.parts[0].inferred_solid_alpha &&
              classified.parts[0].depth_write,
          "Sparse black diffuse texels still cut through solid scenery");
  classified.textures[0].rgba[7] = 0;
  classified.parts = {wall};
  classified.prepare_materials();
  require(classified.parts[0].texture_alpha,
          "A substantial binary cutout mask was made solid");
  classified.textures[0].rgba[7] = 255;
  wall.depth_write = false;
  classified.parts = {wall};
  classified.prepare_materials();
  require(classified.parts[0].texture_alpha,
          "An explicit transparency layer lost its sparse alpha mask");
  model.textures[1] = {1, 1, {0, 0, 0, 0}};
  wall = quad(.5f, 1);
  wall.texture_alpha = false;
  model.parts = {background, wall};
  expect_pixel(render(), {0, 0, 0},
               "Ignoring texture alpha still leaves a hole in the shader");
  require(renderer.pick(model, camera, 32, 32) == 1,
          "Solid diffuse detail is invisible to picking");
  for (auto &v : wall.vertices)
    v.color = 0x40808080;
  wall.alpha_cutoff = 0;
  model.parts = {background, wall};
  alpha = 128.f / 255;
  expect_pixel(render(),
               {40 * (1 - alpha), 80 * (1 - alpha), 120 * (1 - alpha)},
               "Ignoring texture alpha also disabled vertex transparency");
  // Invisible magenta RGB must not bleed into a filtered mask edge.
  model.textures[1] = {2, 1, {0, 200, 0, 255, 255, 0, 255, 0}};
  wall = quad(.5f, 1);
  wall.alpha_cutoff = 0;
  for (auto &v : wall.vertices)
    v.u = .5f;
  model.parts = {background, wall};
  expect_pixel(render(), {20, 140, 60},
               "Transparent RGB contaminated a filtered material edge");
  model.stage = true;
  renderer.stage_background = false;
  wall = quad(.5f, 1);
  for (size_t t = 0; t < wall.vertices.size(); t += 3)
    std::swap(wall.vertices[t + 1], wall.vertices[t + 2]);
  model.parts = {background, wall};
  renderer.upload(model);
  require(renderer.pick(model, camera, 32, 32) == 0,
          "A normal one-sided material stopped culling its back face");
  model.parts[1].two_sided = true;
  require(renderer.pick(model, camera, 32, 32) == 1,
          "Two-sided legacy preview still loses reversed faces when picking");
  model.stage = false;
  Image robe{10, 10, Bytes(400)};
  for (size_t i = 0; i < 100; ++i) {
    robe.rgba[i * 4] = 200;
    robe.rgba[i * 4 + 1] = 100;
    robe.rgba[i * 4 + 2] = 50;
    robe.rgba[i * 4 + 3] = i < 10 ? 0 : i == 10 ? 128 : 255;
  }
  model.textures[1] = robe;
  wall = quad(.5f, 1);
  wall.opaque = true;
  for (auto &v : wall.vertices)
    v.u = v.v = .05f;
  wall.bind = wall.vertices;
  model.parts = {background, wall};
  model.prepare_materials();
  require(!model.parts[1].opaque && model.parts[1].depth_write &&
              model.parts[1].alpha_cutoff == .5f,
          "A filtered character cutout was forced opaque or made translucent");
  expect_pixel(render(), {40, 80, 120},
               "Character cutout still rendered its masked texels");
  require(model.textures[1].rgba == robe.rgba,
          "Cutout classification changed the original texture");
  for (size_t i = 3; i < model.textures[1].rgba.size(); i += 4)
    model.textures[1].rgba[i] = 128;
  wall.opaque = true;
  model.parts = {background, wall};
  model.prepare_materials();
  require(model.parts[1].opaque,
          "A character lighting alpha channel became a cutout");
  expect_pixel(render(), {200, 100, 50},
               "Character lighting alpha incorrectly dimmed an opaque surface");
  context->ClearState();
}
} // namespace
int main() {
  try {
    checks();
    std::cout << "Hidden WARP blending, fades, depth/sorting, sparse diffuse "
                 "alpha, filtered mask edges, vertex alpha and two-sided "
                 "picking checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

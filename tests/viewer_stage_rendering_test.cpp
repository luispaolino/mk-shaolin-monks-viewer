#include "renderer.h"
#include <iostream>
using namespace shaolin::viewer;
namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
template <class T> void put(Bytes &b, size_t at, T value) {
  std::memcpy(b.data() + at, &value, sizeof(value));
}
Bytes fixture() {
  Bytes b(4096);
  std::memcpy(b.data(), "2EMP", 4);
  put(b, 24, 1024u);
  put(b, 64, 22u);
  put(b, 68, 3u);
  put(b, 76, 7u);
  put(b, 92, 1u);
  put(b, 104, 1.f);
  put(b, 108, 1.f);
  put(b, 120, 256u);
  std::memcpy(b.data() + 124, "Source material", 16);
  put(b, 260, 32u);
  put(b, 264, 2u);
  put(b, 276, 512u);
  put(b, 288, 576u);
  put(b, 308, 640u);
  for (size_t i = 0; i < 3; ++i) {
    size_t p = 512 + i * 64;
    put(b, p + 12, uint32_t(i + 1));
    put(b, p + 32, .25f);
    put(b, p + 36, .5f);
    put(b, p + 40, 2.f);
    put(b, p + 44, 3.f);
  }
  put(b, 1024, 2048u);
  put(b, 1028, 3u);
  put(b, 1032, 6u);
  put(b, 1040, 3u);
  put(b, 1044, UINT32_MAX);
  put(b, 1088, 1200u);
  put(b, 1092, 1260u);
  for (size_t i = 0; i < 3; ++i) {
    put(b, 1200 + i * 16, int16_t(i == 1 ? 2 : 0));
    put(b, 1202 + i * 16, int16_t(i == 2 ? 2 : 0));
    put(b, 1212 + i * 16, 0x80808080u);
  }
  put(b, 1264, 16u);
  put(b, 1272, 3u);
  put(b, 1276, 1280u);
  put(b, 1280, uint16_t(0x8000));
  put(b, 1282, uint16_t(1));
  put(b, 1284, uint16_t(2));
  put(b, 2052, 21u);
  put(b, 2056, 1u);
  put(b, 2060, 42u);
  put(b, 2064, 10.f);
  put(b, 2068, -20.f);
  put(b, 2072, 30.f);
  put(b, 2100, 0x8055aaffu);
  put(b, 2128, 5.f);
  put(b, 2132, 15.f);
  put(b, 2140, 2304u);
  put(b, 2308, 15u);
  put(b, 2312, 1u);
  put(b, 2320, uint64_t(1));
  put(b, 2328, 2340u);
  put(b, 2340, uint64_t(0x1800));
  return b;
}
Part quad(int texture) {
  Part p;
  p.texture = texture;
  p.alpha_cutoff = 0;
  p.two_sided = true;
  p.vertices = {{-2, -2, 0, 1.25f, .5f, 0x80808080, 0, 0, 1},
                {2, -2, 0, 1.25f, .5f, 0x80808080, 0, 0, 1},
                {2, 2, 0, 1.25f, .5f, 0x80808080, 0, 0, 1},
                {-2, -2, 0, 1.25f, .5f, 0x80808080, 0, 0, 1},
                {2, 2, 0, 1.25f, .5f, 0x80808080, 0, 0, 1},
                {-2, 2, 0, 1.25f, .5f, 0x80808080, 0, 0, 1}};
  p.bind = p.vertices;
  return p;
}
std::array<int, 3> pixel(const Image &image) {
  size_t at = (size_t(image.height / 2) * image.width + image.width / 2) * 4;
  return {image.rgba[at], image.rgba[at + 1], image.rgba[at + 2]};
}
void checks() {
  auto b = fixture();
  auto m = decode_model(b, true);
  require(m.parts.size() == 1 && m.parts[0].source_material &&
              m.parts[0].prelit && m.parts[0].texture == 1 &&
              m.parts[0].gloss.texture == 2 &&
              m.parts[0].reflection.texture == 3 &&
              m.parts[0].diffuse.offset[0] == .25f &&
              m.parts[0].reflection.scale[1] == 3.f &&
              m.parts[0].material_flags == 1,
          "Source material channels or sampler/UV settings were lost");
  require(m.lights.size() == 1 && m.lights[0].id == 42 &&
              m.lights[0].position[1] == 20 && !m.lights[0].enabled &&
              !m.lights[0].affects_meshes && m.lights[0].near_range == 5 &&
              std::abs(m.lights[0].color[2] - .5f) < 1e-6f,
          "Light coordinates, source color scale, flags or ranges changed");
  put(b, 2128, -1.f);
  require(decode_model(b, true).lights[0].far_range == 1000001,
          "The native unlimited light range was lost");
  put(b, 120, 4090u);
  bool rejected = false;
  try {
    decode_model(b, true);
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected, "A truncated material channel table was accepted");
  put(b, 68, 2u);
  require(!decode_model(b, true).parts[0].source_material,
          "A legacy material was interpreted as a modern channel table");

  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                          D3D11_SDK_VERSION, &device, nullptr, &context));
  Renderer r(device.Get(), context.Get());
  r.grid = r.joints = r.bloom = r.lighting = false;
  r.resize(64, 64);
  Camera camera;
  camera.pitch = camera.yaw = 0;
  camera.distance = 4;
  m = {};
  m.textures = {{2, 1, {200, 0, 0, 255, 0, 0, 200, 255}},
                {1, 1, {100, 100, 100, 255}},
                {1, 1, {200, 0, 0, 255}},
                {1, 1, {0, 0, 0, 255}}};
  m.parts = {quad(0)};
  auto render = [&] {
    r.upload(m);
    r.draw(m, camera);
    return pixel(r.capture());
  };
  require(render() == std::array<int, 3>{200, 0, 0},
          "Wrapped U sampled the wrong texel");
  m.parts[0].material_flags = 2;
  require(render() == std::array<int, 3>{0, 0, 200},
          "Clamped U borrowed the opposite edge");
  m.parts[0].diffuse.offset = {.25f, .5f};
  m.parts[0].diffuse.scale = {0, 0};
  require(render() == std::array<int, 3>{200, 0, 0},
          "Source UV transform was ignored");
  m.parts[0] = quad(1);
  m.parts[0].reflection.texture = 2;
  auto reflected = render();
  require(std::abs(reflected[0] - 150) <= 1 && std::abs(reflected[1] - 50) <= 1,
          "The separate reflection channel was not composited");
  m.parts[0].gloss.texture = 3;
  require(render() == std::array<int, 3>{100, 100, 100},
          "The gloss mask did not suppress reflection");
  m.parts[0] = quad(1);
  m.parts[0].prelit = true;
  for (auto &v : m.parts[0].vertices)
    v.color = 0x80404040;
  m.stage = true;
  r.lighting = true;
  auto baked = render();
  r.light_azimuth = 150;
  r.light_elevation = 15;
  require(render() == baked && std::abs(baked[0] - 50) <= 1,
          "A generic preview light relit baked compact scenery");
  m.parts[0] = quad(1);
  StageLight light;
  light.type = 1;
  light.position = {0, 0, 1};
  light.color = {1, 0, 0};
  light.far_range = 10;
  m.lights = {light};
  auto source = render();
  require(source[0] > 90 && source[1] == 0 && source[2] == 0,
          "Source point light position/color did not reach the shader");
  r.source_lights = false;
  require(render() == std::array<int, 3>{100, 100, 100},
          "Disabling source lights left stale constants");
  r.lighting = false;
  auto subtract = quad(1);
  for (auto &v : subtract.vertices)
    v.z = 1;
  subtract.blend = BlendMode::Subtractive;
  subtract.depth_write = false;
  m.parts.push_back(subtract);
  require(render() == std::array<int, 3>{0, 0, 0},
          "Subtractive effects used an additive blend equation");
  context->ClearState();
}
} // namespace
int main() {
  try {
    checks();
    std::cout << "Source material/light decoding and hidden WARP UV, sampler, "
                 "reflection, gloss, prelit, point-light and subtractive "
                 "rendering passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

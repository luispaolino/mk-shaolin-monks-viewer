#include "renderer.h"
#include <cstring>
#include <iostream>
#include <limits>
using namespace shaolin::viewer;
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
  bool failed = false;
  try {
    action();
  } catch (const std::runtime_error &) {
    failed = true;
  }
  require(failed, "Invalid particle input was accepted");
}
ParticleCommand parse(const std::string &text) {
  return decode_particle(
      {reinterpret_cast<const uint8_t *>(text.data()), text.size()});
}
std::shared_ptr<ParticleLibrary> library() {
  auto out = std::make_shared<ParticleLibrary>();
  auto c = parse("PARTICLE SETTINGS:\r\n\"Texture\" <0>\n\"Life\" <60>"
                 "\"Delay\" <10>\"Max\" <8>\"Multiple\" <1>"
                 "\"StartSize\" <1>\"EndSize\" <2>\"Speed\" <0>"
                 "\"R\" <255>\"G\" <100>\"B\" <0>\"A\" <255>"
                 "\"R2\" <255>\"G2\" <100>\"B2\" <0>\"A2\" <0>");
  out->commands = {c, c};
  out->commands[0].flags = 5;
  out->commands[1].flags = 0x405;
  out->commands[1].texture = 1;
  out->textures = {{1, 1, {255, 255, 255, 255}}, {1, 1, {0, 255, 255, 255}}};
  return out;
}
Part quad(float z = 0, float half = .1f) {
  Part p;
  p.vertices = {{-half, -half, z, 0, 0, 0x80808080, 0, 0, 1},
                {half, -half, z, 1, 0, 0x80808080, 0, 0, 1},
                {half, half, z, 1, 1, 0x80808080, 0, 0, 1},
                {-half, -half, z, 0, 0, 0x80808080, 0, 0, 1},
                {half, half, z, 1, 1, 0x80808080, 0, 0, 1},
                {-half, half, z, 0, 1, 0x80808080, 0, 0, 1}};
  p.bind = p.vertices;
  p.texture = 0;
  p.opaque = true;
  p.two_sided = true;
  return p;
}
Model model() {
  Model m;
  m.stage = true;
  m.parts = {quad()};
  m.textures = {{1, 1, {0, 0, 0, 255}}};
  m.bounds();
  m.bind_particles(library());
  return m;
}
bool identical(const std::vector<Vertex> &a, const std::vector<Vertex> &b) {
  return a.size() == b.size() &&
         std::memcmp(a.data(), b.data(), a.size() * sizeof(Vertex)) == 0;
}
uint64_t brightness(const Image &image) {
  uint64_t sum = 0;
  for (size_t p = 0; p < image.rgba.size(); p += 4)
    sum += image.rgba[p] + image.rgba[p + 1] + image.rgba[p + 2];
  return sum;
}
void checks() {
  for (const char *text : {"BAD", "PART\"Life\"<1>", "PART\"Life\"<nan>",
                           "PART\"Life\"<1junk>", "PART\"Life\"<1>\"Life\"<2>",
                           "PART\"Texture\"<-1>\"Life\"<1>\"Max\"<1>"
                           "\"StartSize\"<1>\"EndSize\"<1>",
                           "PART\"Texture\"<0>\"Life\"<1>\"Max\"<1.5>"
                           "\"StartSize\"<1>\"EndSize\"<1>",
                           "PART\"Texture\"<0>\"Life\"<1>\"Max\"<1>"
                           "\"StartSize\"<1000001>\"EndSize\"<1>"})
    rejects([&] { parse(text); });
  auto m = model();
  {
    auto settings = library();
    settings->commands[0].flags = 0;
    settings->commands[0].delay = 100;
    auto fixed = model();
    fixed.particle_library = settings;
    fixed.add_particle_emitter(0, 0);
    Camera view;
    view.pitch = view.yaw = 0;
    view.animate_particles(fixed, .25);
    const auto &v = fixed.parts.back().vertices;
    require((v[0].color >> 24) == 128 &&
                std::abs(std::abs(v[1].x - v[0].x) - 1) < 1e-5f,
            "Unflagged particles acquired color fades or size interpolation");
    settings->commands[0].flags = 0x25;
    settings->commands[0].weight = 100;
    settings->commands[0].speed = 100;
    view.animate_particles(fixed, 10. / 60);
    float center = (v[0].y + v[2].y) * .5f;
    require(
        std::abs(center - (.1f - 6.5f)) < .001f,
        "Source speed units or discrete gravity did not match the native step");
    settings->commands[0].flags = 0x400000;
    fixed.set_particle_command(0, 0);
    require(fixed.parts.back().blend == BlendMode::Subtractive &&
                !fixed.parts.back().emissive,
            "Subtractive particle flags selected emission");
    settings->commands[0].flags = 0x400400;
    fixed.set_particle_command(0, 0);
    require(fixed.parts.back().blend == BlendMode::Alpha,
            "Combined native blend flags failed to select normal alpha");
  }
  auto minimum = m.minimum, maximum = m.maximum;
  require(m.particle_emitters.empty(),
          "A solid surface was inferred as an emitter");
  m.add_particle_emitter(0, 0);
  require(m.parts.size() == 2 && m.scenery.size() == 1,
          "Manual emitter or its timeline is missing");
  m.bounds();
  require(m.minimum == minimum && m.maximum == maximum && m.triangles == 2,
          "Particles changed source geometry counts or framing");
  std::array<float, 3> lo, hi;
  require(m.object_bounds(1, lo, hi) && lo == minimum && hi == maximum,
          "An emitter did not frame its anchor");
  Camera camera;
  camera.pitch = 0;
  camera.animate_particles(m, .25);
  require(m.particle_emitters[0].alive == 2,
          "Source delay or burst count was ignored");
  const auto &v = m.parts[1].vertices;
  require(v[6].color >> 24 == 96,
          "Source alpha endpoints were not interpolated");
  require(std::abs(std::abs(v[7].x - v[6].x) - 1.25f) < .001f,
          "Source start/end sizes were not interpolated");
  auto snapshot = v;
  for (int step = 0; step < 61; ++step)
    camera.animate_particles(m, step / 60.);
  camera.animate_particles(m, .25);
  require(identical(snapshot, v), "Seeking changed deterministic spawn state");
  camera.animate_particles(m, .25);
  require(identical(snapshot, v), "Paused particles advanced");
  camera.yaw = 1.570796327f;
  camera.animate_particles(m, .25);
  require(std::abs(v[1].x - v[0].x) < .001f && std::abs(v[1].z - v[0].z) > 1,
          "Billboards did not follow the camera while paused");
  m.particles_enabled = false;
  camera.animate_particles(m, .25);
  require(!m.particle_emitters[0].alive, "Disabled particles remained active");
  for (const auto &vertex : v)
    require(!vertex.color, "Disabled particle vertices retained opacity");
  m.particles_enabled = true;
  m.parts[0].visible = false;
  camera.animate_particles(m, .25);
  require(!m.particle_emitters[0].alive, "Hidden anchor continued emitting");
  m.parts[0].visible = true;
  camera.yaw = 0;
  camera.animate_particles(m, .25);
  float x = v[0].x;
  for (auto &vertex : m.parts[0].vertices)
    vertex.x += 10;
  camera.animate_particles(m, .25);
  require(std::abs(v[0].x - x - 10) < .001f, "Particles lost a moving anchor");
  rejects([&] { m.add_particle_emitter(1, 0); });
  rejects([&] { m.add_particle_emitter(0, 20); });
  rejects([&] { m.set_particle_command(8, 0); });
  rejects([&] { camera.animate_particles(m, -1); });
  rejects([&] {
    camera.animate_particles(m, std::numeric_limits<double>::infinity());
  });
  auto random = library();
  random->commands[0].emitter_flags = 0x200e000;
  random->commands[0].dev_origin = 20;
  random->commands[0].dev_angle = 30;
  random->commands[0].dev_speed = 40;
  random->commands[0].dev_size = .1f;
  random->commands[0].dev_life = 5;
  auto varied = model();
  varied.particle_library = random;
  varied.add_particle_emitter(0, 0);
  camera.animate_particles(varied, .75);
  snapshot = varied.parts[1].vertices;
  camera.animate_particles(varied, 20);
  camera.animate_particles(varied, .75);
  require(identical(snapshot, varied.parts[1].vertices),
          "Randomized commands were not seekable");
  auto sparse = library();
  sparse->commands[0].delay = 100;
  sparse->commands[0].multiple = 3;
  sparse->commands[0].maximum = 2;
  auto limited = model();
  limited.particle_library = sparse;
  limited.add_particle_emitter(0, 0);
  camera.animate_particles(limited, .1);
  require(limited.particle_emitters[0].alive == 2,
          "Burst emission exceeded the source maximum");
  camera.animate_particles(limited, 1.1);
  require(!limited.particle_emitters[0].alive,
          "Particles survived their source lifetime");
  camera.animate_particles(limited, 1.8);
  require(limited.particle_emitters[0].alive == 2,
          "Emitter did not restart after its next delay");
  for (size_t i = 1; i < 64; ++i)
    limited.add_particle_emitter(0, 0);
  rejects([&] { limited.add_particle_emitter(0, 0); });
  // Only compact warm emissive surfaces qualify for automatic preview anchors.
  Model inferred;
  inferred.textures = {{1, 1, {255, 100, 0, 255}}};
  auto fire = quad(0, 10);
  fire.emissive = true;
  fire.blend = BlendMode::Additive;
  fire.opaque = false;
  inferred.parts.assign(5, fire);
  inferred.parts[1].sky = true;
  inferred.parts[2].helper = true;
  inferred.parts[3].role = MeshRole::LightShaft;
  inferred.parts[4].visible = false;
  auto warm = library();
  warm->commands[1].start_size = 18;
  inferred.bind_particles(warm);
  require(inferred.particle_emitters.size() == 1 &&
              inferred.particle_emitters[0].inferred,
          "Automatic flame inference included helpers, skies, shafts or hidden "
          "meshes");

  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                          D3D11_SDK_VERSION, &device, nullptr, &context));
  Renderer renderer(device.Get(), context.Get());
  renderer.grid = renderer.joints = renderer.bloom = false;
  renderer.stage_background = false;
  renderer.resize(128, 96);
  m = model();
  m.add_particle_emitter(0, 0);
  camera.distance = 3;
  camera.animate_particles(m, .25);
  renderer.upload(m);
  renderer.draw(m, camera);
  auto lit = renderer.capture();
  m.particles_enabled = false;
  camera.animate_particles(m, .25);
  renderer.update_pose(m, true);
  renderer.draw(m, camera);
  auto dark = renderer.capture();
  require(brightness(lit) > brightness(dark) + 10000,
          "Source-colored alpha particles did not reach the GPU");
  m.particles_enabled = true;
  camera.animate_particles(m, .25);
  renderer.update_pose(m, true);
  renderer.lighting = false;
  renderer.draw(m, camera);
  require(renderer.capture().rgba == lit.rgba,
          "Particle sprites acquired mesh lighting");
  m.set_particle_command(0, 1);
  camera.animate_particles(m, .25);
  renderer.update_pose(m, true);
  renderer.draw(m, camera);
  auto additive = renderer.capture();
  require(additive.rgba != lit.rgba && m.parts[1].emissive,
          "Changing command did not update its sprite and blend mode");
  {
    auto animated = library();
    animated->commands[0].flags = 0x8000000;
    animated->commands[0].texture_end = 1;
    animated->commands[0].texture_animation = 10;
    animated->commands[0].delay = 100;
    animated->commands[0].color = {255, 255, 255, 255};
    animated->textures[0].rgba = {255, 0, 0, 255};
    animated->textures[1].rgba = {0, 255, 0, 255};
    Model frames;
    frames.parts = {quad()};
    frames.bind_particles(animated);
    frames.add_particle_emitter(0, 0);
    camera.animate_particles(frames, 0);
    renderer.upload(frames);
    renderer.draw(frames, camera);
    auto first = renderer.capture();
    camera.animate_particles(frames, .75);
    renderer.update_pose(frames, true);
    renderer.draw(frames, camera);
    auto second = renderer.capture();
    size_t middle =
        (size_t(first.height / 2) * first.width + first.width / 2) * 4;
    require(first.rgba[middle] > 200 && first.rgba[middle + 1] < 3 &&
                second.rgba[middle] < 3 && second.rgba[middle + 1] > 200,
            "Per-particle source texture animation did not change GPU sprites");
    camera.animate_particles(frames, 0);
    renderer.update_pose(frames, true);
    renderer.draw(frames, camera);
    require(renderer.capture().rgba == first.rgba,
            "Seeking did not restore the original sprite frame");
    renderer.upload(m);
    renderer.draw(m, camera);
  }
  renderer.bloom = true;
  renderer.draw(m, camera);
  require(brightness(renderer.capture()) > brightness(additive),
          "Additive particles did not contribute to emission bloom");
  auto blocker = quad(.4f, 2);
  m.parts.push_back(blocker);
  renderer.upload(m);
  renderer.draw(m, camera);
  auto blocked = renderer.capture();
  m.particles_enabled = false;
  camera.animate_particles(m, .25);
  renderer.update_pose(m, true);
  renderer.draw(m, camera);
  require(renderer.capture().rgba == blocked.rgba,
          "Particles or glow leaked through opaque geometry");
  m.parts.pop_back();
  // Picking passes through transient particles to the original stage geometry.
  m.parts[0].vertices.assign(6, Vertex{0, 0, 0, 0, 0, 0x80808080, 0, 1, 0});
  m.particles_enabled = true;
  camera.animate_particles(m, .25);
  renderer.upload(m);
  require(renderer.pick(m, camera, 64, 48) == -1,
          "Particles intercepted viewport selection");
}
} // namespace
int main() {
  try {
    checks();
    std::cout << "Particle command, lifecycle, billboard, anchor, alpha, depth "
                 "and bloom checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

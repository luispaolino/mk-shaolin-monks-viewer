#include "assets.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>

namespace shaolin::viewer {
namespace {
constexpr size_t emitter_limit = 64, particle_limit = 64;
BlendMode particle_blend(uint32_t flags) {
  switch (flags & 0x400400) {
  case 0x400:
    return BlendMode::Additive;
  case 0x400000:
    return BlendMode::Subtractive;
  default:
    return BlendMode::Alpha;
  }
}
uint32_t hash(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352d;
  x ^= x >> 15;
  x *= 0x846ca68b;
  return x ^ (x >> 16);
}
float random(uint32_t seed) { return float(hash(seed) >> 8) / 8388608.f - 1; }
float deviation(uint32_t seed, float value) {
  auto bound = int64_t(std::floor(std::abs(value)));
  return bound ? float(int64_t(hash(seed) % (uint64_t(bound) * 2)) - bound) : 0;
}
bool tag(std::span<const uint8_t> bytes, const char *value) {
  return bytes.size() >= 4 && std::memcmp(bytes.data(), value, 4) == 0;
}
void part_bounds(const Part &part, std::array<float, 3> &lo,
                 std::array<float, 3> &hi) {
  lo.fill(1e30f);
  hi.fill(-1e30f);
  for (const auto &v : part.vertices) {
    float p[]{v.x, v.y, v.z};
    for (size_t k = 0; k < 3; ++k) {
      lo[k] = std::min(lo[k], p[k]);
      hi[k] = std::max(hi[k], p[k]);
    }
  }
}
} // namespace
ParticleCommand decode_particle(std::span<const uint8_t> bytes) {
  if (bytes.size() > 16384 || !tag(bytes, "PART"))
    throw std::runtime_error("Unsupported particle command");
  std::string text(bytes.begin(), bytes.end());
  std::map<std::string, double> values;
  size_t at = 0;
  while ((at = text.find('"', at)) != std::string::npos) {
    auto end = text.find('"', at + 1);
    if (end == std::string::npos || end - at > 64)
      throw std::runtime_error("Invalid particle property name");
    auto begin = text.find('<', end + 1), close = text.find('>', begin);
    if (begin == std::string::npos || close == std::string::npos ||
        close - begin > 64)
      throw std::runtime_error("Invalid particle property value");
    auto name = text.substr(at + 1, end - at - 1);
    auto token = text.substr(begin + 1, close - begin - 1);
    auto first = token.find_first_not_of(" \t\r\n");
    auto last = token.find_last_not_of(" \t\r\n");
    if (first == std::string::npos)
      throw std::runtime_error("Empty particle property");
    double value = 0;
    auto result =
        std::from_chars(token.data() + first, token.data() + last + 1, value);
    if (result.ec != std::errc{} || result.ptr != token.data() + last + 1 ||
        !std::isfinite(value) || std::abs(value) > 4294967295.)
      throw std::runtime_error("Invalid particle number");
    if (!values.emplace(name, value).second)
      throw std::runtime_error("Duplicate particle property");
    at = close + 1;
  }
  auto number = [&](const char *key, double fallback = 0.) {
    auto it = values.find(key);
    return it == values.end() ? fallback : it->second;
  };
  auto integer = [&](const char *key, uint32_t maximum) {
    double n = number(key);
    if (n < 0 || n > maximum || std::floor(n) != n)
      throw std::runtime_error("Invalid particle integer");
    return uint32_t(n);
  };
  for (auto key : {"Texture", "Life", "Max", "StartSize", "EndSize"})
    if (!values.contains(key))
      throw std::runtime_error("Missing required particle property");
  ParticleCommand c;
  c.texture = integer("Texture", 4095);
  c.texture_end = integer("TexEnd", 4095);
  c.texture_animation = integer("TexAnim", 255);
  c.maximum = integer("Max", 65535);
  c.multiple = integer("Multiple", 255);
  c.flags = integer("Flags", UINT32_MAX);
  c.emitter_flags = integer("EmitterFlags", UINT32_MAX);
  c.life = float(number("Life"));
  c.delay = float(number("Delay"));
  c.start_size = float(number("StartSize"));
  c.end_size = float(number("EndSize"));
  c.speed = float(number("Speed"));
  c.rotation_speed = float(number("RotSpeed"));
  c.dev_origin = float(number("DevOrigin"));
  c.dev_angle = float(number("DevAngle"));
  c.dev_size = float(number("DevSize"));
  c.dev_life = float(number("DevLife"));
  c.dev_speed = float(number("DevSpeed"));
  c.dev_rotation = float(number("DevRot"));
  c.dev_scale = float(number("DevScale"));
  c.dev_fade = float(number("DevFade"));
  c.weight = float(integer("Weight", 65535));
  c.lerp_delay = float(integer("LerpDelay", 255));
  c.ease_in = float(integer("EaseIn", 255));
  c.bounce = float(number("Bounce"));
  c.friction = float(number("Friction"));
  if (c.life < 0 || c.life > 100000 || c.delay < 0 || c.delay > 65535 ||
      c.start_size < 0 || c.end_size < 0 || c.start_size > 1e6f ||
      c.end_size > 1e6f || std::abs(c.speed) > 32768)
    throw std::runtime_error("Particle command exceeds preview limits");
  const char *color[]{"R", "G", "B", "A"};
  const char *end_color[]{"R2", "G2", "B2", "A2"};
  for (size_t k = 0; k < 4; ++k) {
    c.color[k] = float(integer(color[k], 255));
    c.end_color[k] = float(integer(end_color[k], 255));
  }
  return c;
}
std::shared_ptr<const ParticleLibrary> Source::particle_library() const {
  if (particle_library_scanned_)
    return particle_library_;
  // The supported disc stores its shared effect banks before the map block.
  // Discover by contents rather than importing textures from a mesh bank or
  // relying on the truncated filenames in the archive.
  for (uint32_t id = 200; id < std::min<size_t>(count(), 300); ++id) {
    auto data = load(id);
    if (!tag(data, "PWF "))
      continue;
    auto files = members(data);
    if (files.empty() || !tag(files.front(), "PART"))
      continue;
    auto library = std::make_shared<ParticleLibrary>();
    library->resource = id;
    for (const auto &file : files)
      library->commands.push_back(decode_particle(file));
    for (uint32_t next = id + 1; next < std::min<size_t>(count(), id + 5);
         ++next) {
      auto bank = load(next);
      if (!tag(bank, "PWF "))
        continue;
      auto images = members(bank);
      if (images.empty() || !tag(images.front(), "XPR0"))
        continue;
      for (const auto &file : images)
        library->textures.push_back(decode_xpr(file));
      break;
    }
    if (library->textures.empty())
      throw std::runtime_error("Particle texture bank is unavailable");
    for (const auto &command : library->commands)
      if (command.texture >= library->textures.size())
        throw std::runtime_error("Particle texture index outside shared bank");
    particle_library_ = library;
    break;
  }
  particle_library_scanned_ = true;
  return particle_library_;
}
size_t Model::add_particle_emitter(size_t anchor, size_t command,
                                   bool inferred) {
  if (!particle_library || anchor >= parts.size() ||
      command >= particle_library->commands.size() ||
      parts[anchor].role == MeshRole::ParticleEmitter ||
      parts[anchor].vertices.empty() ||
      particle_emitters.size() >= emitter_limit)
    throw std::runtime_error("Invalid particle preview anchor or command");
  const auto &c = particle_library->commands[command];
  if (!c.maximum || c.life <= 0)
    throw std::runtime_error("Particle command has no live particles");
  ParticleEmitter emitter{
      parts.size(), anchor, command,
      hash(uint32_t(anchor) + uint32_t(particle_emitters.size()) * 7919),
      inferred};
  Part p;
  p.name = "Particle emitter " + std::to_string(particle_emitters.size() + 1) +
           " / command " + std::to_string(command) +
           (inferred ? " (inferred anchor)" : " (preview anchor)");
  p.role = MeshRole::ParticleEmitter;
  p.texture = particle_sprites.at(command);
  p.material_flags = 0;
  p.blend = particle_blend(c.flags);
  p.emissive = p.blend == BlendMode::Additive;
  p.depth_write = false;
  p.two_sided = true;
  p.alpha_cutoff = 0;
  p.render_flags = c.flags;
  std::array<float, 3> lo, hi;
  part_bounds(parts[anchor], lo, hi);
  Vertex v{
      (lo[0] + hi[0]) * .5f, hi[1], (lo[2] + hi[2]) * .5f, 0, 0, 0, 0, 1, 0};
  p.vertices.assign(particle_limit * 6, v);
  p.bind = p.vertices;
  parts.push_back(std::move(p));
  particle_emitters.push_back(emitter);
  if (scenery.empty())
    scenery.push_back({"Ambient", 0, {}, 30});
  return particle_emitters.size() - 1;
}
void Model::set_particle_command(size_t emitter, size_t command) {
  if (!particle_library || emitter >= particle_emitters.size() ||
      command >= particle_library->commands.size())
    throw std::runtime_error("Invalid particle preview command");
  const auto &c = particle_library->commands[command];
  if (!c.maximum || c.life <= 0)
    throw std::runtime_error("Particle command has no live particles");
  auto &e = particle_emitters[emitter];
  e.command = command;
  auto &p = parts.at(e.part);
  p.name = "Particle emitter " + std::to_string(emitter + 1) + " / command " +
           std::to_string(command) +
           (e.inferred ? " (inferred anchor)" : " (preview anchor)");
  p.texture = particle_sprites.at(command);
  p.blend = particle_blend(c.flags);
  p.emissive = p.blend == BlendMode::Additive;
  p.render_flags = c.flags;
}
void Model::bind_particles(std::shared_ptr<const ParticleLibrary> library) {
  if (particle_library)
    throw std::runtime_error("Particle preview is already bound");
  if (library) {
    if (library->commands.size() > 4096 || library->textures.size() > 4096)
      throw std::runtime_error("Particle bank exceeds preview limits");
    for (const auto &c : library->commands)
      if (c.texture >= library->textures.size())
        throw std::runtime_error("Invalid particle sprite index");
  }
  particle_library = std::move(library);
  if (!particle_library || particle_library->commands.empty())
    return;
  particle_texture_base = textures.size();
  textures.insert(textures.end(), particle_library->textures.begin(),
                  particle_library->textures.end());
  particle_sprites.clear();
  for (const auto &c : particle_library->commands) {
    if (!(c.flags & 0x8000000) || !c.texture_end) {
      particle_sprites.push_back(int(particle_texture_base + c.texture));
      continue;
    }
    // Frame gutters prevent linear filtering from borrowing an adjacent
    // sprite. Each particle can choose its own frame without extra draw calls.
    size_t frames = size_t(c.texture_end) + 1;
    const auto &first = particle_library->textures.at(c.texture);
    if (frames > particle_library->textures.size() - c.texture ||
        (uint64_t(first.width) + 2) * frames > 16384 || first.height > 16382 ||
        !first.width || !first.height ||
        (uint64_t(first.width) + 2) * frames * (first.height + 2) * 4 >
            128 * 1024 * 1024)
      throw std::runtime_error("Invalid particle animation frames");
    Image atlas{uint32_t((first.width + 2) * frames), first.height + 2, {}};
    atlas.rgba.resize(size_t(atlas.width) * atlas.height * 4);
    for (size_t f = 0; f < frames; ++f) {
      const auto &image = particle_library->textures[c.texture + f];
      if (image.width != first.width || image.height != first.height ||
          image.rgba.size() != size_t(image.width) * image.height * 4)
        throw std::runtime_error("Inconsistent particle animation dimensions");
      for (uint32_t y = 0; y < atlas.height; ++y)
        for (uint32_t x = 0; x < first.width + 2; ++x) {
          size_t source =
              (size_t(std::clamp(int(y) - 1, 0, int(image.height) - 1)) *
                   image.width +
               std::clamp(int(x) - 1, 0, int(image.width) - 1)) *
              4;
          size_t target =
              (size_t(y) * atlas.width + f * (first.width + 2) + x) * 4;
          std::copy_n(image.rgba.data() + source, 4,
                      atlas.rgba.data() + target);
        }
    }
    particle_sprites.push_back(int(textures.size()));
    textures.push_back(std::move(atlas));
  }
  // Automatic placement is a preview inference, not a recovered script event.
  // Only warm, source-emissive flame meshes qualify; eyes, skies, portals and
  // light shafts must not silently become particle generators.
  size_t preset = SIZE_MAX;
  float score = 1e30f;
  for (size_t i = 0; i < particle_library->commands.size(); ++i) {
    const auto &c = particle_library->commands[i];
    if (!(c.flags & 0x400) || c.color[0] < c.color[1] * 1.2f ||
        c.color[0] < c.color[2] * 1.2f || c.life < 30 || c.life > 180 ||
        c.start_size < 8 || c.start_size > 100 || !c.maximum)
      continue;
    float candidate = c.start_size + std::abs(c.life - 70);
    if (candidate < score) {
      score = candidate;
      preset = i;
    }
  }
  if (preset == SIZE_MAX)
    return;
  std::set<uint32_t> objects;
  size_t original = parts.size();
  for (size_t p = 0; p < original && particle_emitters.size() < emitter_limit;
       ++p) {
    const auto &part = parts[p];
    if (!part.visible || part.sky || part.helper || !part.emissive ||
        part.role != MeshRole::Surface || part.blend != BlendMode::Additive ||
        part.texture < 0 || size_t(part.texture) >= particle_texture_base ||
        (part.mesh_id != UINT32_MAX && objects.contains(part.mesh_id)))
      continue;
    const auto &image = textures[part.texture];
    double r = 0, g = 0, b = 0;
    for (size_t k = 0; k < image.rgba.size(); k += 4) {
      double a = image.rgba[k + 3] / 255.;
      r += image.rgba[k] * a;
      g += image.rgba[k + 1] * a;
      b += image.rgba[k + 2] * a;
    }
    std::array<float, 3> lo, hi;
    part_bounds(part, lo, hi);
    float height = hi[1] - lo[1];
    if (r <= g * 1.2 || g <= b * 1.2 || height < 1 ||
        std::max(hi[0] - lo[0], hi[2] - lo[2]) > height * 3)
      continue;
    objects.insert(part.mesh_id);
    add_particle_emitter(p, preset, true);
  }
}
void Model::animate_particles(double seconds, const std::array<float, 3> &right,
                              const std::array<float, 3> &up) {
  if (!std::isfinite(seconds) || seconds < 0 || seconds > 1e9)
    throw std::runtime_error("Invalid particle preview time");
  for (float n : right)
    if (!std::isfinite(n))
      throw std::runtime_error("Invalid particle camera basis");
  for (float n : up)
    if (!std::isfinite(n))
      throw std::runtime_error("Invalid particle camera basis");
  if (!particle_library)
    return;
  for (auto &emitter : particle_emitters) {
    auto &part = parts.at(emitter.part);
    const auto &anchor = parts.at(emitter.anchor);
    const auto &c = particle_library->commands.at(emitter.command);
    emitter.alive = 0;
    part.vertices = part.bind;
    if (!particles_enabled || !anchor.visible || !part.visible)
      continue;
    std::array<float, 3> lo, hi;
    part_bounds(anchor, lo, hi);
    std::array<float, 3> origin{(lo[0] + hi[0]) * .5f, hi[1],
                                (lo[2] + hi[2]) * .5f};
    // Evaluate from time and spawn ID: pause, seeking and frame rate cannot
    // change random samples or accumulate integration error.
    double frame = seconds * 60, delay = std::max(1.f, c.delay);
    uint64_t last = uint64_t(std::floor(frame / delay));
    size_t burst = std::max(1u, c.multiple);
    size_t capacity = std::min<size_t>(c.maximum, part.vertices.size() / 6);
    size_t attempts = std::min<size_t>(
        4096,
        size_t(std::ceil((c.life + std::abs(c.dev_life) * .1) / delay) + 1) *
            burst);
    for (size_t n = 0; n < attempts && emitter.alive < capacity; ++n) {
      uint64_t back = n / burst;
      if (back > last)
        break;
      uint64_t event = last - back;
      uint32_t seed =
          hash(emitter.seed ^ uint32_t(event) ^ hash(uint32_t(n % burst)));
      float age = float(frame - double(event) * delay);
      float life =
          std::max(1.f, c.life + ((c.emitter_flags & 0x4000)
                                      ? deviation(seed + 1, c.dev_life) * .1f
                                      : 0));
      if (age >= life)
        continue;
      float t = age / life;
      float lerp = t;
      if (c.flags & 0x8000) {
        float delay = std::clamp(c.lerp_delay * .01f, 0.f, 1.f);
        lerp = delay < 1 ? std::clamp((t - delay) / (1 - delay), 0.f, 1.f) : 0;
      }
      float start_size = c.start_size, end_size = c.end_size;
      if (c.emitter_flags & 0x2000000)
        start_size =
            std::max(0.f, start_size + deviation(seed + 2, c.dev_size));
      if (c.emitter_flags & 0x20000000)
        end_size =
            std::max(1.f, end_size + deviation(seed + 15, c.dev_scale) * .1f);
      float size =
          start_size + ((c.flags & 4) ? (end_size - start_size) * lerp : 0);
      size = std::clamp(size, 0.f, 1e6f);
      float speed = c.speed;
      if (c.emitter_flags & 0x8000)
        speed = float(int16_t(uint16_t(
            int(c.speed) + int(deviation(seed + 3, c.dev_speed) * .1f))));
      float spread = (c.emitter_flags & 0x20000) ? c.dev_angle * .01f : 0;
      float dx = random(seed + 4) * spread, dz = random(seed + 5) * spread;
      float dy = 1 + random(seed + 6) * spread;
      if (c.emitter_flags & 8) {
        dx = random(seed + 4);
        dy = random(seed + 6);
        dz = random(seed + 5);
      }
      if (c.emitter_flags & 0x20)
        dx = 0;
      if (c.emitter_flags & 0x40)
        dz = 0;
      if (c.emitter_flags & 0x10000000)
        dy = 0;
      float length = std::max(1e-6f, std::sqrt(dx * dx + dy * dy + dz * dz));
      // The native particle step adds velocity every 60 Hz tick. Speed is
      // stored in hundredths; gravity is (0, .3, 0) in the game's Y-down space.
      // Sum the discrete steps analytically so scrubbing remains deterministic.
      float ticks = std::floor(age), fraction = age - ticks;
      float acceleration_steps =
          ticks * (ticks + 1) * .5f + fraction * (ticks + 1);
      float gravity = (c.flags & 0x20)     ? -.003f * c.weight
                      : (c.flags & 0x4000) ? .003f * c.weight
                                           : 0;
      std::array<float, 3> pos{origin[0] + dx / length * speed * age * .01f,
                               origin[1] + dy / length * speed * age * .01f +
                                   gravity * acceleration_steps,
                               origin[2] + dz / length * speed * age * .01f};
      if (c.emitter_flags & 0x2000)
        for (size_t k = 0; k < 3; ++k)
          if (!((k == 0 && (c.emitter_flags & 0x20)) ||
                (k == 1 && (c.emitter_flags & 0x10000000)) ||
                (k == 2 && (c.emitter_flags & 0x40))))
            pos[k] += random(seed + 10 + uint32_t(k)) * c.dev_origin * .01f;
      uint32_t color = 0;
      for (size_t k = 0; k < 4; ++k) {
        float endpoint = c.end_color[k];
        if (k == 3 && (c.emitter_flags & 0x10000))
          endpoint =
              std::max(1.f, endpoint + deviation(seed + 18, c.dev_fade) * .1f);
        float value =
            c.color[k] + ((c.flags & 1) ? (endpoint - c.color[k]) * lerp : 0);
        if (k == 3 && (c.flags & 0x10000))
          value *= std::min(1.f, c.ease_in * lerp);
        // The mesh shader uses the original 128-scale vertex color path.
        color |= uint32_t(std::clamp(value * (128.f / 255), 0.f, 128.f))
                 << ((k == 0   ? 2
                      : k == 2 ? 0
                               : k) *
                     8);
      }
      float rotation = c.rotation_speed;
      if (c.emitter_flags & 0x800000)
        rotation += deviation(seed + 16, c.dev_rotation) * .1f;
      float angle =
          ((c.flags & 0x200) ? rotation * age * .1f : 0) * .01745329252f;
      if (c.flags & 0x100)
        angle += (c.flags & 0x200 ? random(seed + 17) * 180.f
                                  : float(hash(seed + 17) % 4) * 90.f) *
                 .01745329252f;
      float ca = std::cos(angle), sa = std::sin(angle);
      std::array<float, 3> x{}, y{};
      for (size_t k = 0; k < 3; ++k) {
        x[k] = (right[k] * ca + up[k] * sa) * size * .5f;
        y[k] = (up[k] * ca - right[k] * sa) * size * .5f;
      }
      const int corners[6][2]{{-1, -1}, {1, -1}, {1, 1},
                              {-1, -1}, {1, 1},  {-1, 1}};
      size_t at = emitter.alive++ * 6;
      float u0 = 0, u1 = 1, v0 = 0, v1 = 1;
      if ((c.flags & 0x8000000) && c.texture_end) {
        auto frames = c.texture_end + 1;
        auto frame =
            uint32_t(std::max(
                0.f, std::floor((c.texture + frames * t) *
                                std::max(1u, c.texture_animation) * .1f))) %
            frames;
        const auto &atlas = textures.at(size_t(part.texture));
        const auto &image = particle_library->textures.at(c.texture);
        u0 = float(frame * (image.width + 2) + 1) / atlas.width;
        u1 = float(frame * (image.width + 2) + image.width + 1) / atlas.width;
        v0 = 1.f / atlas.height;
        v1 = float(image.height + 1) / atlas.height;
      }
      for (size_t j = 0; j < 6; ++j) {
        float sx = float(corners[j][0]), sy = float(corners[j][1]);
        part.vertices[at + j] = {pos[0] + sx * x[0] + sy * y[0],
                                 pos[1] + sx * x[1] + sy * y[1],
                                 pos[2] + sx * x[2] + sy * y[2],
                                 sx < 0 ? u0 : u1,
                                 sy > 0 ? v0 : v1,
                                 color,
                                 0,
                                 1,
                                 0};
      }
    }
    if (part.blend == BlendMode::Alpha && emitter.alive > 1) {
      std::array<size_t, particle_limit> order{};
      std::array<float, 3> toward_camera{right[1] * up[2] - right[2] * up[1],
                                         right[2] * up[0] - right[0] * up[2],
                                         right[0] * up[1] - right[1] * up[0]};
      auto depth = [&](size_t i) {
        const auto &v = part.vertices[i * 6];
        return v.x * toward_camera[0] + v.y * toward_camera[1] +
               v.z * toward_camera[2];
      };
      for (size_t i = 0; i < emitter.alive; ++i)
        order[i] = i;
      std::stable_sort(order.begin(), order.begin() + emitter.alive,
                       [&](size_t a, size_t b) { return depth(a) < depth(b); });
      std::array<Vertex, particle_limit * 6> sorted;
      for (size_t i = 0; i < emitter.alive; ++i)
        std::copy_n(part.vertices.begin() + order[i] * 6, 6,
                    sorted.begin() + i * 6);
      std::copy_n(sorted.begin(), emitter.alive * 6, part.vertices.begin());
    }
  }
}
} // namespace shaolin::viewer

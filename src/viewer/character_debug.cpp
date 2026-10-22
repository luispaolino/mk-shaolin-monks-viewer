#include "character_debug.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace shaolin::viewer {
namespace {
uint32_t u32(std::span<const uint8_t> bytes, size_t offset) {
  if (offset > bytes.size() || bytes.size() - offset < 4)
    throw std::runtime_error("Truncated character debug metadata");
  uint32_t value;
  std::memcpy(&value, bytes.data() + offset, 4);
  return value;
}
bool tag(std::span<const uint8_t> b, const char *s) {
  return b.size() >= 4 && std::memcmp(b.data(), s, 4) == 0;
}
std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return value;
}
std::vector<std::string> common_bones(std::span<const uint8_t> exe,
                                      size_t required = 23) {
  if (!tag(exe, "XBEH"))
    return {};
  uint32_t base = u32(exe, 0x104), count = u32(exe, 0x11c),
           table = u32(exe, 0x120);
  if (count > 256 || table < base)
    return {};
  auto address = [&](uint32_t va) -> size_t {
    for (uint32_t s = 0; s < count; ++s) {
      size_t at = size_t(table - base) + s * 56;
      uint32_t start = u32(exe, at + 4), raw = u32(exe, at + 12),
               size = u32(exe, at + 16);
      if (va >= start && va - start < size &&
          size_t(raw) + va - start < exe.size())
        return size_t(raw) + va - start;
    }
    return exe.size();
  };
  // The common bank uses the executable's 23-bone humanoid name table.
  // Read that table from the supplied game rather than shipping a game table.
  for (size_t at = 0; at + 4 <= exe.size(); at += 4) {
    size_t p = address(u32(exe, at));
    if (p + 7 > exe.size() || std::memcmp(exe.data() + p, "pelvis\0", 7))
      continue;
    std::vector<std::string> names;
    for (size_t i = 0; i <= required && at + i * 4 + 4 <= exe.size(); ++i) {
      p = address(u32(exe, at + i * 4));
      std::string name;
      for (size_t k = 0; k < 32 && p + k < exe.size() && exe[p + k]; ++k) {
        unsigned char c = exe[p + k];
        if (!std::isalnum(c) && c != '_') {
          name.clear();
          break;
        }
        name += char(c);
      }
      if (name.empty() || (i && name == "pelvis"))
        break;
      names.push_back(name);
    }
    if (names.size() == required ||
        (required > 23 && names.size() > required)) {
      names.resize(required);
      return names;
    }
  }
  return {};
}
bool checker(const Image &image) {
  if (image.width < 32 || image.height < 32 || image.width > 512 ||
      image.height > 512 ||
      image.rgba.size() != size_t(image.width) * image.height * 4)
    return false;
  auto pixel = [&](unsigned x, unsigned y, unsigned c) {
    return int(image.rgba[(size_t(y) * image.width + x) * 4 + c]);
  };
  int contrast = 0, error = 0;
  for (unsigned c = 0; c < 3; ++c) {
    int a = pixel(image.width / 4, image.height / 4, c);
    int b = pixel(image.width * 3 / 4, image.height / 4, c);
    contrast += (a - b) * (a - b);
    int d = a - pixel(image.width * 3 / 4, image.height * 3 / 4, c);
    int e = b - pixel(image.width / 4, image.height * 3 / 4, c);
    error += d * d + e * e;
    if (std::abs(a - pixel(image.width / 4, image.height / 4, (c + 1) % 3)) > 5)
      return false;
  }
  return contrast > 20000 && error < 100;
}
float duration(const Model &m, int clip) {
  return clip >= 0 && size_t(clip) < m.clips.size()
             ? std::max(.1f,
                        m.clips[clip].frames / std::max(1.f, m.clips[clip].fps))
             : .6f;
}
std::array<float, 2> root_at(const Model &model, int index, float frame) {
  const auto &clip = model.clips[index];
  if (clip.keys.empty())
    return {};
  auto next =
      std::upper_bound(clip.keys.begin(), clip.keys.end(), frame,
                       [](float f, const Key &key) { return f < key.frame; });
  size_t a =
      next == clip.keys.begin() ? 0 : size_t(next - clip.keys.begin() - 1);
  size_t b = std::min(a + 1, clip.keys.size() - 1);
  float t = a == b ? 0
                   : std::clamp((frame - clip.keys[a].frame) /
                                    std::max(.001f, clip.keys[b].frame -
                                                        clip.keys[a].frame),
                                0.f, 1.f);
  for (size_t i = 0; i < clip.bones.size(); ++i)
    if (clip.bones[i] >= 0 && size_t(clip.bones[i]) < model.bones.size() &&
        model.bones[clip.bones[i]].parent < 0 &&
        clip.keys[a].pose[i].translated && clip.keys[b].pose[i].translated) {
      const auto &x = clip.keys[a].pose[i].translation;
      const auto &y = clip.keys[b].pose[i].translation;
      return {x[0] + t * (y[0] - x[0]), x[2] + t * (y[2] - x[2])};
    }
  return {};
}
float gait_speed(Model &model, int index, float fallback) {
  if (index < 0)
    return fallback;
  const auto &clip = model.clips[index];
  if (clip.keys.size() < 2)
    return fallback;
  // Prefer displacement authored into the root. In-place loops can instead
  // supply a speed estimate from the backward motion of their planted feet.
  for (size_t i = 0; i < clip.bones.size(); ++i)
    if (clip.bones[i] >= 0 && model.bones[clip.bones[i]].parent < 0) {
      const auto &a = clip.keys.front().pose[i], &b = clip.keys.back().pose[i];
      float x = b.translation[0] - a.translation[0],
            z = b.translation[2] - a.translation[2];
      float speed = std::hypot(x, z) / duration(model, index);
      if (a.translated && b.translated && speed > model.debug_meter * .2f)
        return std::clamp(speed, model.debug_meter * .2f,
                          model.debug_meter * 20);
    }
  std::vector<size_t> feet;
  for (size_t i = 0; i < model.bones.size(); ++i)
    if (model.bones[i].name == "l_foot" || model.bones[i].name == "r_foot")
      feet.push_back(i);
  if (feet.empty())
    return fallback;
  std::vector<float> speeds;
  std::vector<std::array<float, 3>> previous;
  for (int step = 0; step <= 20; ++step) {
    model.pose(index, float(clip.frames - 1) * step / 20);
    std::vector<std::array<float, 3>> current;
    for (size_t foot : feet)
      current.push_back(model.bones[foot].position);
    if (step) {
      size_t planted = 0;
      for (size_t k = 1; k < feet.size(); ++k)
        if (current[k][1] < current[planted][1])
          planted = k;
      float x = current[planted][0] - previous[planted][0];
      float z = current[planted][2] - previous[planted][2];
      float speed = std::hypot(x, z) * 20 / duration(model, index);
      if (speed > model.debug_meter * .2f)
        speeds.push_back(speed);
    }
    previous = std::move(current);
  }
  if (speeds.empty())
    return fallback;
  std::sort(speeds.begin(), speeds.end());
  return std::clamp(speeds[speeds.size() / 2], model.debug_meter * .2f,
                    model.debug_meter * 20);
}
float foot_height(const Model &model) {
  float contact = std::numeric_limits<float>::max();
  for (const auto &part : model.parts) {
    if (!part.visible || part.skin.size() != part.vertices.size())
      continue;
    for (size_t i = 0; i < part.vertices.size(); ++i)
      for (size_t k = 0; k < 4; ++k) {
        const auto &skin = part.skin[i];
        if (skin.weight[k] < .25f || skin.bone[k] >= model.bones.size())
          continue;
        const auto &name = model.bones[skin.bone[k]].name;
        if (name == "l_foot" || name == "r_foot" || name == "l_toe" ||
            name == "r_toe")
          contact = std::min(contact, part.vertices[i].y);
      }
  }
  return contact;
}
float ground_offset(Model &model, int index) {
  if (index < 0)
    return 0;
  float lowest = std::numeric_limits<float>::max();
  for (int step = 0; step <= 20; ++step) {
    model.pose(index, float(model.clips[index].frames - 1) * step / 20);
    float foot = foot_height(model);
    if (foot != std::numeric_limits<float>::max()) {
      lowest = std::min(lowest, foot);
      continue;
    }
    for (const auto &part : model.parts)
      if (part.visible && !part.skin.empty())
        for (const auto &v : part.vertices)
          lowest = std::min(lowest, v.y);
  }
  return lowest == std::numeric_limits<float>::max()
             ? 0
             : model.grid_origin[1] - lowest;
}
} // namespace
CharacterControls decode_character_controls(std::span<const uint8_t> exe) {
  CharacterControls out;
  try {
    if (!tag(exe, "XBEH"))
      return out;
    uint32_t base = u32(exe, 0x104), count = u32(exe, 0x11c),
             table = u32(exe, 0x120);
    if (count > 256 || table < base)
      return out;
    auto read = [&](uint32_t va, size_t size) -> std::span<const uint8_t> {
      for (uint32_t i = 0; i < count; ++i) {
        size_t at = size_t(table - base) + i * 56;
        uint32_t start = u32(exe, at + 4), raw = u32(exe, at + 12),
                 length = u32(exe, at + 16);
        if (va < start || uint64_t(va) - start + size > length)
          continue;
        uint64_t offset = uint64_t(raw) + va - start;
        if (offset + size <= exe.size())
          return exe.subspan(size_t(offset), size);
      }
      throw std::runtime_error("Missing original jump metadata");
    };
    auto number = [&](uint32_t va) {
      float f;
      auto bytes = read(va, 4);
      std::memcpy(&f, bytes.data(), 4);
      if (!std::isfinite(f) || std::abs(f) > 10000)
        throw std::runtime_error("Invalid original jump parameter");
      return f;
    };
    // Supported retail initializer: release window, then 48-byte jump rows.
    // Read the owner's executable data rather than packaging a tuning table.
    auto init = read(0x3c11d0, 42);
    auto step = read(0x1c30e0, 6), release = read(0x74d23, 6);
    if (init[0] != 0x56 || init[1] != 0x6a || init[13] != 0xbe ||
        step[0] != 0xd8 || step[1] != 0x0d || release[0] != 0xd8 ||
        release[1] != 0x0d)
      return out;
    out.release_ticks = init[2];
    out.position_step = number(u32(step, 2));
    out.release_scale = number(u32(release, 2));
    out.landing_cancel_frame = u32(read(0x514ac4, 4), 0);
    auto cap = read(0x1c891f, 5);
    if (cap[0] != 0x68)
      return {};
    out.air_limit_drag = -number(0x1c8920);
    auto turn = read(0xd5995, 10);
    if (turn[0] != 0xc7 || turn[1] != 0x05)
      return {};
    out.ground_turn = number(0xd599b) * 6.28318530718f / 4096;
    if (out.ground_turn <= 0 || out.ground_turn > 3.141592654f)
      return {};
    uint32_t rows = u32(init, 14);
    if (rows > UINT32_MAX - 14 * 48)
      return {};
    for (size_t i = 0; i < out.jump.size(); ++i) {
      uint32_t id = i ? 11 : 2, va = rows + id * 48;
      if (u32(read(va, 4), 0) != id)
        return {};
      auto &p = out.jump[i];
      p.turn = number(va + 4) * 6.28318530718f / 4096;
      p.speed = number(va + 8);
      p.maximum_speed = number(va + 12);
      p.drag = number(va + 16);
      for (size_t k = 0; k < 4; ++k)
        p.acceleration[k] = number(va + 20 + uint32_t(k) * 4);
      p.launch = -number(va + 36);
      int16_t gravity;
      auto bytes = read(va + 40, 2);
      std::memcpy(&gravity, bytes.data(), 2);
      // Original SAR discards the low bit before adding gravity each update.
      p.gravity = float(gravity >> 1);
      if (p.launch <= 0 || p.gravity <= 0 || p.speed < 0 || p.turn < 0 ||
          p.turn > 3.141592654f || p.maximum_speed < p.speed || p.drag < 0 ||
          p.drag > 1 ||
          std::any_of(p.acceleration.begin(), p.acceleration.end(),
                      [](float a) { return a < 0 || a > 100; }))
        return {};
    }
    out.available = out.release_ticks > 0 && out.release_ticks < 60 &&
                    out.landing_cancel_frame < 120 && out.position_step > 0 &&
                    out.position_step <= 1 && out.release_scale > 0 &&
                    out.release_scale <= 1 && out.air_limit_drag >= 0 &&
                    out.air_limit_drag <= 1;
  } catch (const std::exception &) {
    return {};
  }
  return out;
}
std::vector<Clip> Source::common_motion(const Model &model) const {
  auto names = common_bones(executable_);
  if (names.empty())
    return {};
  std::vector<int> mapping;
  for (const auto &name : names) {
    auto b = std::find_if(model.bones.begin(), model.bones.end(),
                          [&](const auto &bone) { return bone.name == name; });
    mapping.push_back(b == model.bones.end() ? -1
                                             : int(b - model.bones.begin()));
  }
  if (std::count_if(mapping.begin(), mapping.end(),
                    [](int b) { return b >= 0; }) < 18)
    return {};
  auto extended = common_bones(executable_, 35);
  bool extended_valid =
      extended.size() == 35 &&
      std::equal(names.begin(), names.end(), extended.begin());
  if (!common_motion_) {
    auto clips = std::make_shared<std::vector<Clip>>();
    for (uint32_t id = 230; id < std::min<size_t>(count(), 260); ++id) {
      auto data = load(id);
      if (data.size() < 16 || u32(data, 0) != 16)
        continue;
      uint32_t table = u32(data, 0), names_at = u32(data, 4);
      std::vector<std::pair<uint32_t, uint32_t>> selected;
      bool jump_bank = false;
      for (size_t i = 0; i < 4096; ++i) {
        auto offset = u32(data, size_t(table) + i * 4);
        if (!offset)
          break;
        auto name_offset = u32(data, size_t(names_at) + i * 4);
        size_t p = size_t(names_at) + name_offset;
        if (p >= data.size())
          throw std::runtime_error("Invalid shared motion name");
        std::string name;
        for (size_t k = 0; k < 128 && p + k < data.size() && data[p + k]; ++k)
          name += char(data[p + k]);
        jump_bank |= name == "a30_jump";
        if (name == "a10_nrun" || name == "a30_jump" || name == "a33_jump")
          selected.emplace_back(offset, name_offset);
      }
      if (!jump_bank)
        continue;
      // Decode only locomotion entries. Unused common-bank actions include
      // other encodings which are outside the debug controller's scope.
      for (size_t i = 0; i < selected.size(); ++i) {
        std::memcpy(data.data() + table + i * 4, &selected[i].first, 4);
        std::memcpy(data.data() + names_at + i * 4, &selected[i].second, 4);
      }
      uint32_t zero = 0;
      std::memcpy(data.data() + table + selected.size() * 4, &zero, 4);
      auto bank = decode_animations(data);
      if (std::none_of(bank.begin(), bank.end(),
                       [](const auto &c) { return c.name == "a30_jump"; }))
        continue;
      for (auto &c : bank)
        if ((c.bones.size() == names.size() ||
             (extended_valid && c.bones.size() == 35)) &&
            (c.name == "a10_nrun" || c.name == "a30_jump" ||
             c.name == "a33_jump"))
          clips->push_back(std::move(c));
      break;
    }
    // y00/y01 in the shared bank are weapon poses, not unarmed locomotion.
    // Obtain missing idle/walk previews from a compatible authored humanoid
    // bank, verifying its complete track order against the executable names.
    if (extended_valid)
      for (const auto &asset : catalog()) {
        if (asset.kind != Kind::Character || asset.shared_animation ||
            asset.animation_id == UINT32_MAX)
          continue;
        auto donor = decode_model(load(asset.id));
        if (donor.bones.empty())
          continue;
        donor.clips = decode_animations(load(asset.animation_id));
        map_animation_bones(donor, executable_, asset.id);
        auto compatible = [&](const Clip &c, const char *name) {
          if (c.name != name || c.encoding != 11 || c.frames < 12 ||
              c.bones.size() != extended.size())
            return false;
          for (size_t i = 0; i < c.bones.size(); ++i)
            if (c.bones[i] < 0 || donor.bones[c.bones[i]].name != extended[i])
              return false;
          return true;
        };
        auto idle = std::find_if(
            donor.clips.begin(), donor.clips.end(),
            [&](const auto &c) { return compatible(c, "a00_stnd"); });
        auto walk = std::find_if(
            donor.clips.begin(), donor.clips.end(),
            [&](const auto &c) { return compatible(c, "a10_walk"); });
        if (idle != donor.clips.end() && walk != donor.clips.end()) {
          clips->push_back(*idle);
          clips->push_back(*walk);
          for (auto c : donor.clips) {
            auto name = lower(c.name);
            if (c.encoding != 11 || c.frames < 2 || name.size() < 4 ||
                name[0] < 'g' || name[0] > 'p' || name[3] != '_')
              continue;
            size_t tracks = std::min(c.bones.size(), extended.size());
            bool mapped = tracks >= names.size();
            for (size_t i = 0; i < tracks; ++i)
              mapped &= c.bones[i] >= 0 &&
                        donor.bones[c.bones[i]].name == extended[i];
            if (!mapped)
              continue;
            c.bones.resize(tracks);
            for (auto &key : c.keys)
              key.pose.resize(tracks);
            clips->push_back(std::move(c));
          }
          break;
        }
      }
    common_motion_ = clips;
  }
  auto out = *common_motion_;
  for (auto &clip : out) {
    size_t count = clip.bones.size();
    clip.bones = mapping;
    if (count > mapping.size()) {
      clip.bones.resize(count, -1);
      for (size_t i = mapping.size(); extended_valid && i < count; ++i) {
        auto b = std::find_if(
            model.bones.begin(), model.bones.end(),
            [&](const auto &bone) { return bone.name == extended[i]; });
        if (b != model.bones.end())
          clip.bones[i] = int(b - model.bones.begin());
      }
    }
    clip.common = true;
  }
  return out;
}
Image Source::checker_texture() const {
  if (!checker_texture_.rgba.empty())
    return checker_texture_;
  // Stage texture banks retain the original labeled gray checker as slot zero.
  // Verify its quadrant pattern before using it as the debug floor texture.
  for (uint32_t id = 900; id < std::min<size_t>(count(), 1100); ++id) {
    auto bank = load(id);
    if (!tag(bank, "PWF "))
      continue;
    auto files = members(bank);
    if (files.empty() || !tag(files.front(), "XPR0"))
      continue;
    auto image = decode_xpr(files.front());
    if (checker(image)) {
      checker_texture_ = image;
      return image;
    }
  }
  throw std::runtime_error("Original game checker texture is unavailable");
}
const char *CharacterDebug::action_name(Action value) {
  constexpr const char *names[]{"Idle", "Walk", "Run", "Jump", "Land"};
  return value >= Idle && value < Count ? names[value] : "Unknown";
}
const char *CharacterDebug::attack_name(int kind) {
  constexpr const char *names[]{"Quick (J)", "Power (K)", "Launch (L)",
                                "Throw (U)", "Special (I)"};
  return kind >= 0 && kind < AttackCount ? names[kind] : "Attack";
}
void CharacterDebug::request_attack(int kind, bool kombo) {
  if (kind >= 0 && kind < AttackCount) {
    requested_attack = kind;
    requested_kombo = kombo;
  }
}
void CharacterDebug::request_move(int clip) {
  if (std::find(moves.begin(), moves.end(), clip) != moves.end())
    requested_move = clip;
}
std::array<int, CharacterDebug::Count>
CharacterDebug::resolve(const Model &model) {
  std::array<int, Count> result{-1, -1, -1, -1, -1};
  constexpr const char *preferred[Count][4]{
      {"a00_stnd", "idle", "y00_stnd", ""},
      {"a10_walk", "a20_walk", "y01_walk", "walk"},
      {"a20_nrun", "a80_srun", "a10_nrun", "run"},
      {"a30_jump", "jump", "", ""},
      {"a33_jump", "land", "", ""}};
  for (int action = 0; action < Count; ++action) {
    int best = 100000;
    for (size_t i = 0; i < model.clips.size(); ++i) {
      const auto &clip = model.clips[i];
      if (!clip.frames || clip.keys.empty() ||
          (action == Idle && clip.frames <= 1))
        continue;
      auto name = lower(clip.name);
      int score = 100000;
      for (int p = 0; p < 4; ++p)
        if (*preferred[action][p] && name == preferred[action][p])
          score = std::min(score, p);
      if (score == 100000 &&
          ((action == Idle && name.find("stnd") != name.npos) ||
           (action == Walk && name.find("walk") != name.npos) ||
           (action == Run && (name.find("nrun") != name.npos ||
                              name.find("srun") != name.npos)) ||
           (action == Jump && name.find("jump") != name.npos) ||
           (action == Land && name.find("land") != name.npos)))
        score = (action == Walk || action == Run) ? 120 : 20;
      if (score == 100000)
        continue;
      score += clip.common ? 100 : 0;
      // Short walk entries can be start/stop transitions, not a gait loop.
      if ((action == Walk || action == Run) && clip.frames < 12)
        score += 200;
      // Legacy banks may be earlier exports with an incompatible actor track
      // order. Prefer the named, retargeted shared rig for debug locomotion.
      if (!clip.common && clip.encoding != 11)
        score += 200;
      if (score < best) {
        best = score;
        result[action] = int(i);
      }
    }
  }
  return result;
}
void CharacterDebug::reset() {
  action = Idle;
  position = {};
  heading = velocity = frame = previous_frame = 0;
  action_time = blend_time = 0;
  previous_clip = -1;
  vertical_velocity = 0;
  air_velocity = {};
  jump_ticks = 0;
  jump_accumulator = 0;
  running_jump = shortened_jump = false;
  attack_clip = requested_move = requested_attack = -1;
  attack_time = 0;
  requested_kombo = false;
  attack_queue.clear();
  control_accumulator = 0;
  previous_buttons = latched_buttons = 0;
  latched_jump = false;
  graph_cursor = 0;
  graph_age = 0;
  graph_state = -1;
  graph_queue.clear();
  attack_blend = .16f;
}
void CharacterDebug::begin(Model &model, const Image &image,
                           std::vector<Clip> common) {
  if (active || model.bones.empty() || !std::isfinite(model.debug_meter) ||
      model.debug_meter <= 0 || model.debug_meter > 1e6f || !image.width ||
      !image.height ||
      image.rgba.size() != size_t(image.width) * image.height * 4)
    throw std::runtime_error(
        "Character debug needs a rig and a checker texture");
  original_parts = model.parts.size();
  original_textures = model.textures.size();
  original_clips = model.clips.size();
  model.clips.insert(model.clips.end(), common.begin(), common.end());
  clips = resolve(model);
  moves.clear();
  for (auto &group : attacks)
    group.clear();
  for (size_t i = 0; i < model.clips.size(); ++i) {
    const auto &clip = model.clips[i];
    auto name = lower(clip.name);
    if (clip.frames <= 1 || clip.keys.empty() || name.size() < 4 ||
        name[0] < 'g' || name[0] > 'p' || !std::isdigit(name[1]) ||
        !std::isdigit(name[2]) || name[3] != '_')
      continue;
    if (name.find("reac") != name.npos || name.find("brut") != name.npos)
      continue;
    moves.push_back(int(i));
    int group = name[0] == 'g'   ? Quick
                : name[0] == 'i' ? Power
                : name[0] == 'j' ? Throw
                : name[0] == 'k' ? Launch
                                 : Special;
    attacks[group].push_back(int(i));
  }
  auto order = [&](int a, int b) {
    return lower(model.clips[a].name) < lower(model.clips[b].name);
  };
  std::sort(moves.begin(), moves.end(), order);
  for (auto &group : attacks)
    std::sort(group.begin(), group.end(), order);
  for (int clip : clips)
    if (clip >= 0)
      model.clips[clip].debug_ground_offset = ground_offset(model, clip);
  // A jump keeps the ground pose's origin; independently grounding its tucked
  // feet introduces a second vertical displacement on top of actor physics.
  if (clips[Idle] >= 0)
    for (int action : {Jump, Land})
      if (clips[action] >= 0)
        model.clips[clips[action]].debug_ground_offset =
            model.clips[clips[Idle]].debug_ground_offset;
  for (int move : moves)
    model.clips[move].debug_ground_offset =
        clips[Idle] >= 0 ? model.clips[clips[Idle]].debug_ground_offset : 0;
  walk_speed = gait_speed(model, clips[Walk], model.debug_meter * 2);
  run_speed = std::max(walk_speed * 1.25f,
                       gait_speed(model, clips[Run], model.debug_meter * 5));
  if (model.action_graph.available()) {
    // The bank's viewer FPS is independent of the actor's 60 Hz frame clock.
    if (clips[Walk] >= 0)
      walk_speed *= 60 / std::max(1.f, model.clips[clips[Walk]].fps);
    if (clips[Run] >= 0)
      run_speed *= 60 / std::max(1.f, model.clips[clips[Run]].fps);
    locomotion_rate = 1;
  }
  model.pose(-1, 0);
  model.textures.push_back(image);
  Part floor;
  floor.name = "Character Debug floor / 1 meter squares";
  floor.role = MeshRole::DebugFloor;
  floor.texture = int(original_textures);
  floor.opaque = floor.two_sided = true;
  floor.vertices.resize(20 * 20 * 6);
  floor.bind = floor.vertices;
  model.parts.push_back(std::move(floor));
  model.character_debug = active = true;
  reset();
  update(model, {}, 0, 0);
}
void CharacterDebug::end(Model &model) {
  if (!active)
    return;
  model.parts.resize(original_parts);
  model.textures.resize(original_textures);
  model.clips.resize(original_clips);
  for (auto &clip : model.clips)
    clip.debug_ground_offset = 0;
  model.character_debug = active = false;
  model.pose(-1, 0);
  reset();
}
void CharacterDebug::update(Model &model, const DebugInput &input,
                            double seconds, float camera_yaw, bool secondary) {
  if (!model.action_graph.available()) {
    update_step(model, input, seconds, camera_yaw, secondary);
    return;
  }
  if (!active || !model.character_debug)
    return;
  latched_jump |= input.jump;
  latched_buttons |= input.buttons & ~previous_buttons;
  if (input.attack >= 0)
    request_attack(input.attack, input.kombo);
  control_accumulator +=
      std::isfinite(seconds) ? std::clamp(seconds, 0., .25) : 0;
  if (seconds == 0) {
    update_step(model, {}, 0, camera_yaw, secondary);
    return;
  }
  while (control_accumulator + 1e-8 >= 1. / 60) {
    control_accumulator = std::max(0., control_accumulator - 1. / 60);
    auto tick = input;
    tick.jump = latched_jump;
    tick.buttons |= latched_buttons;
    tick.attack = -1;
    tick.kombo = false;
    update_step(model, tick, 1. / 60, camera_yaw, secondary);
    latched_jump = false;
    latched_buttons = 0;
  }
}
void CharacterDebug::update_step(Model &model, const DebugInput &input,
                                 double seconds, float camera_yaw,
                                 bool secondary) {
  if (!active || !model.character_debug)
    return;
  if (!std::isfinite(camera_yaw))
    camera_yaw = 0;
  float dt = std::isfinite(seconds) ? float(std::clamp(seconds, 0., .25)) : 0;
  float x = std::isfinite(input.right) ? std::clamp(input.right, -1.f, 1.f) : 0;
  float z =
      std::isfinite(input.forward) ? std::clamp(input.forward, -1.f, 1.f) : 0;
  float length = std::hypot(x, z);
  bool moving = length > .001f;
  if (moving) {
    x /= length;
    z /= length;
  }
  float cy = std::cos(camera_yaw), sy = std::sin(camera_yaw);
  float dx = -cy * x - sy * z, dz = sy * x - cy * z;
  bool source_graph = model.action_graph.available();
  if (moving && action != Jump && (!source_graph || attack_clip < 0)) {
    float desired = std::atan2(-dx, -dz);
    float turn = std::remainder(desired - heading, 6.28318530718f);
    heading += source_graph ? std::clamp(turn, -model.controls.ground_turn,
                                         model.controls.ground_turn)
                            : turn * (1 - std::exp(-18 * dt));
  }
  auto change = [&](Action next) {
    previous_clip = attack_clip >= 0 ? attack_clip : clips[action];
    previous_frame = frame;
    action = next;
    action_time = blend_time = 0;
    frame = 0;
  };
  auto start_attack = [&](int clip, double time = 0) {
    previous_clip = attack_clip >= 0 ? attack_clip : clips[action];
    previous_frame = frame;
    attack_clip = clip;
    attack_time = time;
    blend_time = 0;
  };
  if (input.attack >= 0)
    request_attack(input.attack, input.kombo);
  if (source_graph) {
    const auto &g = model.action_graph;
    const uint16_t masks[]{0x80, 0x20, 0x10, 0x02, 0x88};
    uint16_t buttons = input.buttons;
    if (requested_attack >= 0 && requested_attack < AttackCount)
      buttons |= masks[requested_attack];
    requested_attack = -1;
    // Kombo is a convenience in clip-preview mode only. Original input nodes
    // must receive the actual successive button presses, including holds.
    requested_kombo = false;
    uint16_t changed = buttons ^ previous_buttons;
    previous_buttons = buttons;
    int state = action == Jump ? 3 : 0;
    if (graph_state != state) {
      graph_cursor = g.roots[state];
      graph_queue.clear();
      graph_age = 0;
      graph_state = state;
    }
    if (graph_cursor == 0)
      graph_cursor = g.roots[action == Jump ? 3 : 0];
    ++graph_age;
    auto clip_for = [&](int action_id) {
      for (size_t i = 0; i < original_clips; ++i)
        if (model.clips[i].source_action == action_id &&
            model.clips[i].frames > 1 && !model.clips[i].keys.empty())
          return int(i);
      return -1;
    };
    uint32_t candidate = graph_cursor;
    for (size_t scanned = 0; candidate && scanned < g.nodes.size(); ++scanned) {
      const auto &n = g.nodes[candidate];
      if (n.timeout && graph_age > unsigned(n.timeout)) {
        graph_cursor = g.roots[action == Jump ? 3 : 0];
        graph_age = 0;
        break;
      }
      // 783E0 matches new presses exactly, held requirements and forbidden
      // buttons. Unknown world/target predicates fail closed on this floor.
      bool match = action_buttons_match(n, buttons, changed);
      const uint16_t condition_keys[]{0x2000, 0x8000, 0x1000, 0x4000,
                                      0x80,   0x40,   0x10,   0x20,
                                      0x08,   0x02,   0x04,   0x01};
      // 77810 and its retail callbacks inspect input bits 8..19 and world
      // bits 20..27. Other bits in this word carry action behavior, not gates.
      int current_action =
          attack_clip >= 0 ? model.clips[attack_clip].source_action : -1;
      bool predicate =
          !n.predicate || (n.predicate >= 17 && n.predicate < 3027 &&
                           n.predicate - 17 == current_action);
      if ((n.conditions & 0x0ff00000) || n.conditions2 || !predicate ||
          n.start < 0)
        match = false;
      for (size_t k = 0; k < std::size(condition_keys); ++k)
        if ((n.conditions & (0x100u << k)) && !(buttons & condition_keys[k]))
          match = false;
      if (match && (n.pressed || n.held || n.released) &&
          clip_for(n.action) >= 0 && graph_queue.size() < 9) {
        graph_queue.push_back(candidate);
        graph_cursor = n.child;
        graph_age = 0;
        break;
      }
      candidate = n.sibling;
    }
    if (!graph_cursor && graph_queue.empty()) {
      graph_cursor = g.roots[action == Jump ? 3 : 0];
      graph_age = 0;
    }
    if (!graph_queue.empty()) {
      const auto &n = g.nodes[graph_queue.front()];
      // 7A1B3 keeps root entries (timeout 5000) pending until the previous
      // attack state finishes; only continuation nodes can cancel it early.
      if (attack_clip < 0 ||
          (n.timeout != 5000 && blend_time + 1e-7 >= attack_blend &&
           attack_time * 60 + 1e-5 >= n.earliest)) {
        int next = clip_for(n.action);
        if (next >= 0) {
          start_attack(next, std::max(0, n.start) / 60.);
          attack_blend = std::max(6, n.blend) / 60.f;
        }
        graph_queue.erase(graph_queue.begin());
      }
    }
  } else if (requested_attack >= 0) {
    auto &group = attacks[requested_attack];
    std::vector<int> available;
    for (int clip : group) {
      auto name = lower(model.clips[clip].name);
      bool air = name.find("jp_") != name.npos;
      // Directional/air variants remain accessible in the move browser.
      if (air == (action == Jump) &&
          (air || name.find("_st_") != name.npos ||
           name.find("_a0_") != name.npos || name.find("_sp_") != name.npos))
        available.push_back(clip);
    }
    if (available.empty() && action != Jump)
      available = group;
    if (!available.empty()) {
      auto rank = [&](int c) {
        return model.clips[c].common           ? 100
               : model.clips[c].encoding == 11 ? 0
                                               : 200;
      };
      int best = 200;
      for (int c : available)
        best = std::min(best, rank(c));
      available.erase(std::remove_if(available.begin(), available.end(),
                                     [&](int c) { return rank(c) != best; }),
                      available.end());
      auto family = lower(model.clips[available.front()].name).substr(0, 2);
      available.erase(
          std::remove_if(available.begin(), available.end(),
                         [&](int c) {
                           return lower(model.clips[c].name).substr(0, 2) !=
                                  family;
                         }),
          available.end());
      int last = attack_queue.empty() ? attack_clip : attack_queue.back();
      auto at = std::find(available.begin(), available.end(), last);
      size_t next =
          at == available.end()
              ? 0
              : (size_t(at - available.begin()) + 1) % available.size();
      size_t count = requested_kombo ? available.size() : 1;
      for (size_t i = 0; i < count && attack_queue.size() < 8; ++i)
        attack_queue.push_back(available[(next + i) % available.size()]);
    }
    requested_attack = -1;
    requested_kombo = false;
  }
  if (requested_move >= 0) {
    attack_queue.clear();
    attack_queue.push_back(requested_move);
    requested_move = -1;
  }
  if (attack_clip < 0 && !attack_queue.empty()) {
    int next = attack_queue.front();
    attack_queue.erase(attack_queue.begin());
    start_attack(next);
  }
  Action ground = moving ? (input.run ? Run : Walk) : Idle;
  if (action == Land &&
      (input.jump ||
       (moving && action_time * 60 >= model.controls.landing_cancel_frame)))
    change(ground);
  if (action != Jump && action != Land && attack_clip < 0) {
    if (input.jump && clips[Jump] >= 0 && model.controls.available) {
      change(Jump);
      running_jump = moving && input.run;
      const auto &p = model.controls.jump[running_jump ? 1 : 0];
      vertical_velocity = p.launch;
      air_velocity = {-std::sin(heading) * p.speed,
                      -std::cos(heading) * p.speed};
      jump_ticks = 0;
      jump_accumulator = 0;
      shortened_jump = false;
    } else if (action != ground)
      change(ground);
  }
  float cadence = std::isfinite(locomotion_rate)
                      ? std::clamp(locomotion_rate, .25f, 3.f)
                      : 2.f;
  float target_speed = moving && attack_clip < 0
                           ? (input.run ? run_speed : walk_speed) * cadence
                           : 0;
  float old_speed = velocity;
  if (source_graph && action != Jump)
    velocity = target_speed;
  else if (action != Jump)
    velocity += (target_speed - velocity) * (1 - std::exp(-12 * dt));
  float distance = source_graph
                       ? target_speed * dt
                       : target_speed * dt + (old_speed - target_speed) *
                                                 (1 - std::exp(-12 * dt)) / 12;
  if (moving && action != Jump && action != Land && attack_clip < 0) {
    position[0] += dx * distance;
    position[2] += dz * distance;
  }
  action_time += dt;
  blend_time += dt;
  if (attack_clip >= 0) {
    float before = float(attack_time * 60);
    if (!source_graph || blend_time > attack_blend + 1e-7)
      attack_time += dt;
    if (source_graph && action != Jump) {
      const auto &c = model.clips[attack_clip];
      auto a =
          root_at(model, attack_clip, std::min(before, float(c.frames - 1)));
      auto b = root_at(model, attack_clip,
                       std::min(float(attack_time * 60), float(c.frames - 1)));
      float x = b[0] - a[0], z = b[1] - a[1];
      position[0] += x * std::cos(heading) + z * std::sin(heading);
      position[2] += -x * std::sin(heading) + z * std::cos(heading);
    }
    auto attack_duration = [&] {
      return source_graph ? model.clips[attack_clip].frames / 60.f
                          : duration(model, attack_clip);
    };
    while (attack_clip >= 0 && attack_time >= attack_duration()) {
      double remainder = attack_time - attack_duration();
      if (!attack_queue.empty()) {
        int next = attack_queue.front();
        attack_queue.erase(attack_queue.begin());
        start_attack(next, remainder);
      } else {
        previous_clip = attack_clip;
        previous_frame = float(model.clips[attack_clip].frames - 1);
        attack_clip = -1;
        blend_time = 0;
        if (action != Jump && action != Land) {
          action = ground;
          action_time = 0;
        }
      }
    }
  }
  if (action == Jump) {
    const auto &controls = model.controls;
    const auto &p = controls.jump[running_jump ? 1 : 0];
    jump_accumulator += dt;
    // Original actor updates use discrete velocity-before-gravity integration.
    // Keeping those updates at 60 Hz preserves short-hop behavior across host
    // FPS.
    while (jump_accumulator + 1e-8 >= 1. / 60 && action == Jump) {
      jump_accumulator = std::max(0., jump_accumulator - 1. / 60);
      if (!running_jump && !shortened_jump && jump_ticks > 0 &&
          jump_ticks < controls.release_ticks &&
          !(input.jump_held || input.jump)) {
        vertical_velocity *= controls.release_scale;
        shortened_jump = true;
      }
      if (jump_ticks > 0) {
        if (moving) {
          float desired = std::atan2(-dx, -dz);
          float angle = std::remainder(desired - heading, 6.28318530718f);
          unsigned quadrant = unsigned(
              int(std::floor((angle + .7853981634f) / 1.570796327f)) & 3);
          air_velocity[0] += dx * p.acceleration[quadrant];
          air_velocity[1] += dz * p.acceleration[quadrant];
          heading += std::clamp(angle, -p.turn, p.turn);
        } else {
          air_velocity[0] *= 1 - p.drag;
          air_velocity[1] *= 1 - p.drag;
        }
        float speed = std::hypot(air_velocity[0], air_velocity[1]);
        if (speed > p.maximum_speed) {
          float scale = 1 - controls.air_limit_drag;
          air_velocity[0] *= scale;
          air_velocity[1] *= scale;
        }
      }
      position[1] += vertical_velocity * controls.position_step;
      vertical_velocity -= p.gravity;
      position[0] += air_velocity[0];
      position[2] += air_velocity[1];
      ++jump_ticks;
      velocity = std::hypot(air_velocity[0], air_velocity[1]) * 60;
      if (position[1] <= 0 && vertical_velocity < 0) {
        position[1] = vertical_velocity = 0;
        jump_accumulator = 0;
        change(clips[Land] >= 0 ? Land : ground);
      }
    }
  } else {
    position[1] = 0;
    if (action == Land && action_time * 60 >= model.clips[clips[Land]].frames)
      change(ground);
  }
  int clip = attack_clip >= 0 ? attack_clip : clips[action];
  if (clip < 0)
    clip = clips[Idle];
  if (clip >= 0) {
    const auto &c = model.clips[clip];
    double value = attack_clip >= 0 ? attack_time * (source_graph ? 60 : c.fps)
                   : action == Jump ? double(jump_ticks ? jump_ticks - 1 : 0)
                   : action == Land
                       ? action_time * 60
                       : action_time * (source_graph ? 60 : c.fps) *
                             (action == Walk || action == Run ? cadence : 1.f);
    frame = attack_clip >= 0 || action == Jump || action == Land
                ? float(std::min(value, double(c.frames - 1)))
                : float(std::fmod(value, std::max(1., double(c.frames))));
  }
  float blend =
      std::clamp(float(blend_time) /
                     (source_graph && attack_clip >= 0 ? attack_blend : .16f),
                 0.f, 1.f);
  blend = blend * blend * (3 - 2 * blend);
  if (action == Jump || action == Land)
    blend = 1;
  for (size_t p = 0; p < original_parts; ++p)
    if (model.parts[p].skin.empty() && !model.parts[p].bind.empty())
      model.parts[p].vertices = model.parts[p].bind;
  model.joint_translation = {position[0], -position[1], position[2]};
  model.joint_heading = heading;
  model.pose(clip, frame, false, secondary, dt, previous_clip, previous_frame,
             blend, true, attack_clip < 0 && action != Jump && action != Land);
  if (source_graph) {
    // Actor travel consumes the authored root displacement. Remove its
    // remaining in-place sway from the displayed attack to avoid applying that
    // motion twice.
    auto residual = [&](int c, float f) -> std::array<float, 2> {
      if (c < 0 || std::find(moves.begin(), moves.end(), c) == moves.end())
        return {};
      const auto &keys = model.clips[c].keys;
      auto a = root_at(model, c, keys.front().frame),
           b = root_at(model, c, keys.back().frame);
      auto p = root_at(model, c, f);
      float t = std::clamp(
          (f - keys.front().frame) /
              std::max(.001f, keys.back().frame - keys.front().frame),
          0.f, 1.f);
      return {p[0] - a[0] - (b[0] - a[0]) * t, p[1] - a[1] - (b[1] - a[1]) * t};
    };
    auto a = residual(previous_clip, previous_frame), b = residual(clip, frame);
    float x = a[0] + (b[0] - a[0]) * blend, z = a[1] + (b[1] - a[1]) * blend;
    for (auto &bone : model.bones) {
      bone.position[0] -= x;
      bone.position[2] -= z;
    }
    for (size_t p = 0; p < original_parts; ++p)
      for (auto &v : model.parts[p].vertices) {
        v.x -= x;
        v.z -= z;
      }
  }
  if (attack_clip < 0 && action != Jump && action != Land) {
    float contact = foot_height(model);
    // Clothing and hanging accessories must not lift the actor off its feet.
    if (contact != std::numeric_limits<float>::max()) {
      float correction = model.grid_origin[1] - contact;
      for (auto &bone : model.bones)
        bone.position[1] += correction;
      for (size_t p = 0; p < original_parts; ++p)
        for (auto &v : model.parts[p].vertices)
          v.y += correction;
    }
  }
  float s = std::sin(heading), c = std::cos(heading);
  auto transform = [&](float &px, float &py, float &pz, bool direction) {
    float rx = px - (direction ? 0 : model.grid_origin[0]);
    float rz = pz - (direction ? 0 : model.grid_origin[2]);
    px = rx * c + rz * s + (direction ? 0 : model.grid_origin[0] + position[0]);
    pz =
        -rx * s + rz * c + (direction ? 0 : model.grid_origin[2] + position[2]);
    if (!direction)
      py += position[1];
  };
  for (auto &bone : model.bones)
    transform(bone.position[0], bone.position[1], bone.position[2], false);
  for (size_t p = 0; p < original_parts; ++p)
    for (auto &v : model.parts[p].vertices) {
      transform(v.x, v.y, v.z, false);
      transform(v.nx, v.ny, v.nz, true);
    }
  // The floor follows in whole-meter increments, preserving world-space UVs.
  // Black fog reaches full opacity well before any edge, including after
  // travel.
  auto &floor = model.parts[original_parts];
  float meter = model.debug_meter;
  float center_x = std::floor(position[0] / meter) * meter;
  float center_z = std::floor(position[2] / meter) * meter;
  const int corners[6][2]{{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
  // Small tiles avoid unstable depth interpolation across enormous triangles.
  // The 80-meter half-width stays beyond the full-black 60-meter fog distance.
  for (int z = 0; z < 20; ++z)
    for (int x = 0; x < 20; ++x)
      for (size_t i = 0; i < 6; ++i) {
        float px = model.grid_origin[0] + center_x +
                   (x * 8 - 76 + corners[i][0] * 4) * meter;
        float pz = model.grid_origin[2] + center_z +
                   (z * 8 - 76 + corners[i][1] * 4) * meter;
        floor.vertices[(z * 20 + x) * 6 + i] = {px,
                                                model.grid_origin[1],
                                                pz,
                                                px / (meter * 2),
                                                pz / (meter * 2),
                                                0x80808080,
                                                0,
                                                1,
                                                0};
      }
}
} // namespace shaolin::viewer

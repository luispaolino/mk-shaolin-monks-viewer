#include "assets.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace shaolin::viewer {
namespace {
template <class T> T get(std::span<const uint8_t> b, size_t at) {
  if (at > b.size() || sizeof(T) > b.size() - at)
    throw std::runtime_error("Truncated animation data");
  T v;
  std::memcpy(&v, b.data() + at, sizeof(v));
  return v;
}
int32_t bits(std::span<const uint8_t> b, size_t start, uint32_t bit,
             uint32_t width) {
  if (!width)
    return 0;
  if (width > 24)
    throw std::runtime_error("Invalid animation channel width");
  uint64_t v = 0;
  size_t at = start + bit / 8;
  unsigned shift = bit % 8;
  for (unsigned i = 0; i < (width + shift + 7) / 8; ++i)
    v |= uint64_t(get<uint8_t>(b, at + i)) << (8 * i);
  uint32_t q = uint32_t(v >> shift) & ((1u << width) - 1);
  return int32_t(q ^ (1u << (width - 1))) - int32_t(1u << (width - 1));
}
} // namespace
float advance_animation_frame(const Clip &clip, float frame, double seconds,
                              float rate) {
  if (!std::isfinite(seconds) || seconds < 0 || !std::isfinite(rate) ||
      rate < 0 || !std::isfinite(frame) || !std::isfinite(clip.fps))
    return frame;
  return float(std::fmod(double(frame) + seconds * clip.fps * rate,
                         std::max(1., double(clip.frames))));
}
JointDynamics decode_joint_dynamics(std::span<const uint8_t> exe) {
  JointDynamics out;
  try {
    if (get<uint32_t>(exe, 0) != 0x48454258)
      return out;
    uint32_t base = get<uint32_t>(exe, 0x104),
             count = get<uint32_t>(exe, 0x11c),
             table = get<uint32_t>(exe, 0x120);
    if (count > 256 || table < base)
      return out;
    auto read = [&](uint32_t va, size_t size) {
      for (uint32_t i = 0; i < count; ++i) {
        size_t at = size_t(table - base) + i * 56;
        auto start = get<uint32_t>(exe, at + 4),
             raw = get<uint32_t>(exe, at + 12),
             length = get<uint32_t>(exe, at + 16);
        if (va >= start && uint64_t(va) - start + size <= length) {
          size_t p = size_t(raw) + va - start;
          if (p <= exe.size() && size <= exe.size() - p)
            return exe.subspan(p, size);
        }
      }
      throw std::runtime_error("Missing original joint metadata");
    };
    auto immediate = [&](uint32_t va, uint8_t field) {
      auto code = read(va, 7);
      if (code[0] != 0xc7 || code[1] != 0x41 || code[2] != field)
        throw std::runtime_error("Unsupported original joint initializer");
      return get<float>(code, 3);
    };
    // cSpringNode defaults and cSpringWorld environment from the supplied XBE.
    out.stiffness = immediate(0x14d805, 0x60);
    out.damping = immediate(0x14d80c, 0x64);
    out.gravity = immediate(0x14d7fe, 0x6c);
    out.stretch = immediate(0x14d813, 0x74);
    auto mass = read(0x14d7d2, 5);
    if (mass[0] != 0xba)
      return {};
    out.mass = get<float>(mass, 1);
    auto world = read(0x14cf56, 7);
    if (world[0] != 0xc7 || world[1] != 0x40 || world[2] != 0x18)
      return {};
    auto env = read(get<uint32_t>(world, 3), 20);
    if (env[16] != 1)
      return {};
    out.tension_scale = get<float>(env, 0);
    out.damping_scale = get<float>(env, 4);
    out.gravity_scale = get<float>(env, 8);
    out.stretch_scale = get<float>(env, 12);
    auto rate = read(0x14cf4f, 7);
    if (rate[0] != 0xc7 || rate[1] != 0x40 || rate[2] != 0x0c)
      return {};
    out.hz = get<float>(rate, 3);
    for (float v : {out.stiffness, out.damping, out.gravity, out.stretch,
                    out.mass, out.tension_scale, out.damping_scale,
                    out.gravity_scale, out.stretch_scale, out.hz})
      if (!std::isfinite(v) || v < 0 || v > 1000)
        return {};
    out.available = out.mass > 0 && out.hz >= 30 && out.hz <= 120 &&
                    out.stretch >= 1 && out.stretch <= 2 &&
                    out.stretch_scale > 0 && out.stretch_scale <= 2 &&
                    out.damping * out.damping_scale <= 1;
  } catch (const std::exception &) {
    return {};
  }
  return out;
}
size_t Model::secondary_joints() const {
  return std::count_if(bones.begin(), bones.end(), [](const auto &b) {
    return b.name.find("flap") != std::string::npos;
  });
}
std::vector<Clip> decode_animations(std::span<const uint8_t> b) {
  auto table = get<uint32_t>(b, 0), names = get<uint32_t>(b, 4);
  if (table != 16 || names >= b.size())
    throw std::runtime_error("Unsupported animation bank");
  std::vector<Clip> clips;
  size_t pose_count = 0;
  for (size_t i = 0; i < 10000; ++i) {
    auto offset = get<uint32_t>(b, table + i * 4);
    if (!offset)
      return clips;
    size_t at = size_t(table) + offset;
    if (at >= b.size())
      throw std::runtime_error("Invalid animation clip offset");
    auto type = get<uint16_t>(b, at);
    auto d = b.subspan(at);
    Clip clip;
    clip.encoding = type;
    clip.frames = get<uint16_t>(d, 2);
    size_t name_at = size_t(names) + get<uint32_t>(b, names + i * 4);
    for (size_t k = 0; k < 128; ++k) {
      auto c = get<uint8_t>(b, name_at + k);
      if (!c)
        break;
      if (c < 32 || c > 126)
        throw std::runtime_error("Invalid animation name");
      clip.name += char(c);
    }
    if (clip.name.empty())
      clip.name = "Clip " + std::to_string(i);
    if (type == 1 || type == 3 || type == 6) {
      uint32_t count = get<uint16_t>(d, 4),
               keys = type == 6 ? get<uint32_t>(d, 8) : clip.frames;
      if (!count || count > 512 || !keys || keys > 100000)
        throw std::runtime_error("Invalid legacy animation dimensions");
      pose_count += size_t(keys) * count;
      if (pose_count > 4000000)
        throw std::runtime_error("Animation bank exceeds pose limit");
      clip.bones.resize(count, -1);
      size_t data = type == 6 ? 12 : 8;
      size_t stride = type == 1   ? count * 12
                      : type == 3 ? (count + 1) * 6
                                  : count * 4 + 8;
      if (data > d.size() || size_t(keys) * stride > d.size() - data)
        throw std::runtime_error("Truncated legacy animation keys");
      auto signed10 = [](uint32_t v) {
        return int32_t((v & 1023) ^ 512) - 512;
      };
      for (uint32_t k = 0; k < keys; ++k) {
        Key key;
        size_t p = data + size_t(k) * stride;
        key.frame = type == 6 ? float(get<uint32_t>(d, p)) : float(k);
        key.pose.resize(count);
        for (uint32_t j = 0; j < count; ++j) {
          auto &pose = key.pose[j];
          if (type == 6) {
            auto packed = get<uint32_t>(d, p + 4 + (j ? j + 1 : 0) * 4);
            for (int c = 0; c < 3; ++c)
              pose.rotation[c] = float(signed10(packed >> ((2 - c) * 10)) * 4);
            if (!j) {
              auto q = get<uint32_t>(d, p + 8);
              pose.translation = {float(int32_t(q >> 21) * 2 - 2048),
                                  -float((q >> 11) & 1023) * 2,
                                  float(int32_t(q & 2047) * 2 - 2048)};
              pose.translated = true;
            }
          } else {
            size_t v = p + (type == 1 ? j * 12 : (j ? j + 1 : 0) * 6);
            for (int c = 0; c < 3; ++c)
              pose.rotation[c] = float(get<int16_t>(d, v + c * 2));
            if (type == 1 || !j) {
              for (int c = 0; c < 3; ++c)
                pose.translation[c] = float(get<int16_t>(d, v + 6 + c * 2));
              pose.translated = true;
            }
          }
        }
        if (!clip.keys.empty() && key.frame < clip.keys.back().frame)
          throw std::runtime_error("Legacy animation keys are not ordered");
        clip.keys.push_back(std::move(key));
      }
      clips.push_back(std::move(clip));
      continue;
    }
    if (type != 11)
      throw std::runtime_error("Unsupported animation encoding: " +
                               std::to_string(type));
    auto count = get<uint16_t>(d, 4), animated = get<uint16_t>(d, 6),
         stride = get<uint16_t>(d, 8);
    auto keys = get<uint32_t>(d, 12);
    bool v1 = get<uint8_t>(d, 18) == 1;
    if (!count || count > 512 || animated > count || !keys || keys > 100000 ||
        !clip.frames)
      throw std::runtime_error("Invalid animation dimensions");
    pose_count += size_t(keys) * count;
    if (pose_count > 4000000)
      throw std::runtime_error("Animation bank exceeds pose limit");
    size_t map = v1 ? 20 : 32,
           desc = map + (count < 256 ? ((count + 3) & ~3u)
                                     : (((count + 1) & ~1u) * 2));
    uint32_t first = v1 ? stride : get<uint16_t>(d, 24),
             tracks = v1 ? animated : get<uint16_t>(d, 26);
    if (tracks > 512 || !first || !stride)
      throw std::runtime_error("Invalid animation track table");
    size_t data = desc + size_t(tracks) * 8;
    if (data > d.size() || first > d.size() - data ||
        size_t(keys - 1) * stride > d.size() - data - first)
      throw std::runtime_error("Animation keys exceed bank");
    clip.bones.resize(count, -1);
    std::vector<uint32_t> remap;
    for (uint32_t j = 0; j < count; ++j) {
      auto track = count < 256 ? get<uint8_t>(d, map + j)
                               : get<uint16_t>(d, map + j * 2);
      remap.push_back(track);
    }
    for (uint32_t k = 0; k < keys; ++k) {
      size_t key_at = data + (k ? first + size_t(k - 1) * stride : 0);
      Key key;
      key.frame = float(get<uint32_t>(d, key_at));
      key.pose.resize(count);
      if (!clip.keys.empty() && key.frame < clip.keys.back().frame)
        throw std::runtime_error("Animation keys are not ordered");
      for (uint32_t j = 0; j < count; ++j) {
        auto t = remap[j];
        if (t == 255 || t == 65535)
          continue;
        if (t >= tracks)
          throw std::runtime_error("Invalid animation track remap");
        size_t p = desc + t * 8;
        auto bit = get<uint16_t>(d, p);
        size_t sample = t >= animated ? data : key_at;
        auto &pose = key.pose[j];
        for (int channel = 0; channel < 6; ++channel) {
          uint32_t width = get<uint8_t>(d, p + 2 + channel);
          if (channel == 1)
            width &= 127;
          auto bytes = sample == data ? first : stride;
          if (bytes < 4 || uint32_t(bit) + width > (bytes - 4) * 8)
            throw std::runtime_error("Animation channel exceeds key stride");
          int32_t v = bits(d, sample + 4, bit, width);
          bit += width;
          if (channel < 3)
            pose.rotation[channel] = float(v);
          else {
            pose.translation[channel - 3] = float(v);
            pose.translated |= width != 0;
          }
        }
      }
      clip.keys.push_back(std::move(key));
    }
    clips.push_back(std::move(clip));
  }
  throw std::runtime_error("Animation clip table is not terminated");
}
void Model::pose(int index, float frame, bool root_motion, bool secondary,
                 float seconds, int from_clip, float from_frame,
                 float transition, bool rebase_height, bool loop) {
  using namespace DirectX;
  if (index < 0 || size_t(index) >= clips.size()) {
    secondary_positions.clear();
    secondary_previous.clear();
    secondary_rest.clear();
    secondary_clip = -2;
    secondary_velocity.clear();
    secondary_accumulator = 0;
    for (auto &p : parts)
      if (!p.bind.empty())
        p.vertices = p.bind;
    for (auto &b : bones)
      for (int k = 0; k < 3; ++k)
        b.position[k] = (b.bind[k] + origin[k]) * (k == 1 ? -1.f : 1.f);
    return;
  }
  const auto &clip = clips[index];
  if (clip.keys.empty())
    return;
  auto resolve = [&](const Clip &clip, float frame) {
    auto next =
        std::upper_bound(clip.keys.begin(), clip.keys.end(), frame,
                         [](float f, const Key &k) { return f < k.frame; });
    size_t a = next == clip.keys.begin() ? 0
                                         : size_t(next - clip.keys.begin() - 1),
           z = std::min(a + 1, clip.keys.size() - 1);
    bool wrap = loop && a + 1 == clip.keys.size() &&
                frame > clip.keys.back().frame &&
                clip.keys.back().frame < clip.frames;
    if (wrap)
      z = 0;
    float end_frame = clip.keys[z].frame + (wrap ? float(clip.frames) : 0);
    float blend =
        z == a ? 0
               : std::clamp((frame - clip.keys[a].frame) /
                                std::max(.001f, end_frame - clip.keys[a].frame),
                            0.f, 1.f);
    std::vector<Pose> poses(bones.size());
    std::vector<int> root_tracks(bones.size(), -1);
    for (size_t i = 0; i < clip.bones.size(); ++i) {
      int bone = clip.bones[i];
      if (bone < 0 || size_t(bone) >= bones.size())
        continue;
      if (secondary && joint_dynamics.available &&
          bones[bone].name.find("flap") != std::string::npos)
        continue;
      if (bones[bone].parent < 0)
        root_tracks[bone] = int(i);
      auto &p = poses[bone];
      auto &x = clip.keys[a].pose[i];
      auto &y = clip.keys[z].pose[i];
      // Retarget shared motions with the selected rig's local limb lengths.
      // Translations authored for another skeleton otherwise stretch clothing
      // and shorten/lengthen limbs even when bone names match.
      p.translated = (x.translated || y.translated) &&
                     (!clip.common || bones[bone].parent < 0);
      for (int k = 0; k < 3; ++k) {
        float delta = std::remainder(y.rotation[k] - x.rotation[k], 4096.f);
        p.rotation[k] = x.rotation[k] + blend * delta;
        p.translation[k] =
            x.translation[k] + blend * (y.translation[k] - x.translation[k]);
        if (wrap && bones[bone].parent < 0 && k != 1)
          p.translation[k] +=
              blend * (clip.keys.back().pose[i].translation[k] -
                       clip.keys.front().pose[i].translation[k]);
      }
    }
    float travel = std::clamp(
        (frame - clip.keys.front().frame) /
            std::max(.001f, clip.keys.back().frame - clip.keys.front().frame),
        0.f, 1.f);
    for (size_t i = 0; i < bones.size(); ++i) {
      auto &p = poses[i];
      const auto &bone = bones[i];
      for (int k = 0; k < 3; ++k) {
        if (!p.translated)
          p.translation[k] =
              bone.bind[k] -
              (bone.parent >= 0 ? bones[bone.parent].bind[k] : 0);
        if (root_tracks[i] >= 0 && p.translated && k == 1 && rebase_height)
          p.translation[k] -= clip.debug_ground_offset;
        if (root_tracks[i] >= 0 && p.translated && k != 1) {
          // The pelvis also carries hip sway. Preserve it while rebasing the
          // clip start and removing only the net horizontal travel.
          const auto &first = clip.keys.front().pose[root_tracks[i]];
          const auto &last = clip.keys.back().pose[root_tracks[i]];
          float start = first.translated ? first.translation[k] : bone.bind[k];
          float end = last.translated ? last.translation[k] : bone.bind[k];
          p.translation[k] += bone.bind[k] - start;
          if (!root_motion && k != 1)
            p.translation[k] -= (end - start) * (wrap ? 1.f : travel);
        }
      }
      p.translated = true;
    }
    return poses;
  };
  auto poses = resolve(clip, frame);
  std::vector<Pose> previous;
  float weight = std::clamp(transition, 0.f, 1.f);
  if (from_clip >= 0 && size_t(from_clip) < clips.size() &&
      !clips[from_clip].keys.empty() && weight < 1) {
    previous = resolve(clips[from_clip], from_frame);
    for (size_t i = 0; i < poses.size(); ++i)
      for (int k = 0; k < 3; ++k)
        poses[i].translation[k] =
            previous[i].translation[k] +
            weight * (poses[i].translation[k] - previous[i].translation[k]);
  }
  std::vector<XMMATRIX> matrices(bones.size());
  for (size_t i = 0; i < bones.size(); ++i) {
    auto &bone = bones[i];
    auto &p = poses[i];
    const auto &t = p.translation;
    constexpr float radians = 6.28318530718f / 4096.f;
    auto r = XMMatrixRotationZ(p.rotation[2] * radians) *
             XMMatrixRotationY(p.rotation[1] * radians) *
             XMMatrixRotationX(p.rotation[0] * radians);
    if (!previous.empty()) {
      const auto &old = previous[i].rotation;
      auto before = XMMatrixRotationZ(old[2] * radians) *
                    XMMatrixRotationY(old[1] * radians) *
                    XMMatrixRotationX(old[0] * radians);
      r = XMMatrixRotationQuaternion(
          XMQuaternionSlerp(XMQuaternionRotationMatrix(before),
                            XMQuaternionRotationMatrix(r), weight));
    }
    matrices[i] = r * XMMatrixTranslation(t[0], t[1], t[2]);
    if (bone.parent >= 0)
      matrices[i] *= matrices[bone.parent];
    XMFLOAT3 v;
    XMStoreFloat3(&v, XMVector3TransformCoord(XMVectorZero(), matrices[i]));
    bone.position = {v.x + origin[0], -v.y - origin[1], v.z + origin[2]};
  }
  if (secondary && secondary_joints()) {
    std::vector<std::array<float, 3>> rest(bones.size());
    std::vector<bool> flap(bones.size());
    std::vector<int> child(bones.size(), -1);
    std::vector<int> root(bones.size(), -1);
    auto load = [](const std::array<float, 3> &p) {
      return XMVectorSet(p[0], p[1], p[2], 0);
    };
    for (size_t i = 0; i < bones.size(); ++i) {
      XMFLOAT3 p;
      XMStoreFloat3(&p, matrices[i].r[3]);
      rest[i] = {p.x, p.y, p.z};
      flap[i] = bones[i].name.find("flap") != std::string::npos;
      if (flap[i]) {
        int parent = bones[i].parent;
        root[i] = parent >= 0 && flap[parent] ? root[parent] : int(i);
        if (parent >= 0 && flap[parent])
          child[parent] = int(i);
      }
    }
    bool reset =
        secondary_positions.size() != bones.size() ||
        secondary_rest.size() != bones.size() ||
        (secondary_clip != index &&
         (!joint_dynamics.available || seconds == 0)) ||
        !std::isfinite(seconds) || seconds < 0 || seconds > .1f ||
        (seconds == 0 && frame != secondary_frame) ||
        ((secondary_clip == index || !joint_dynamics.available) &&
         std::abs((loop || joint_dynamics.available)
                      ? std::remainder(frame - secondary_frame,
                                       float(std::max(1u, clip.frames)))
                      : frame - secondary_frame) >
             clip.fps * std::max(seconds, .001f) * 2 + 2);
    if (!reset && seconds == 0 && rest != secondary_rest)
      reset = true;
    if (reset) {
      secondary_positions = secondary_previous = rest;
      secondary_velocity.assign(bones.size(), {});
      secondary_accumulator = 0;
    } else if (joint_dynamics.available &&
               (joint_translation != secondary_translation ||
                joint_heading != secondary_heading)) {
      // Simulate in actor-local coordinates while retaining world inertia when
      // Character Debug translates or turns the actor.
      float old_s = std::sin(secondary_heading),
            old_c = std::cos(secondary_heading);
      float s = std::sin(joint_heading), c = std::cos(joint_heading);
      for (size_t i = 0; i < bones.size(); ++i) {
        auto &p = secondary_positions[i];
        float x = p[0] * old_c + p[2] * old_s + secondary_translation[0] -
                  joint_translation[0];
        float z = -p[0] * old_s + p[2] * old_c + secondary_translation[2] -
                  joint_translation[2];
        p = {x * c - z * s,
             p[1] + secondary_translation[1] - joint_translation[1],
             x * s + z * c};
        auto &v = secondary_velocity[i];
        float vx = v[0] * old_c + v[2] * old_s;
        float vz = -v[0] * old_s + v[2] * old_c;
        v = {vx * c - vz * s, v[1], vx * s + vz * c};
      }
    } else if (!joint_dynamics.available && seconds > 0) {
      // Follow most of the animated attachment motion instead of leaving a
      // world-space chain behind a running or turning head.
      for (size_t i = 0; i < bones.size(); ++i)
        if (flap[i])
          for (size_t k = 0; k < 3; ++k) {
            float carry = (rest[i][k] - secondary_rest[i][k]) * .95f;
            secondary_positions[i][k] += carry;
            secondary_previous[i][k] += carry;
          }
    }
    secondary_translation = joint_translation;
    secondary_heading = joint_heading;
    secondary_rest = rest;
    secondary_clip = index;
    secondary_frame = frame;
    std::vector<float> lengths(bones.size()), clearance(bones.size());
    std::vector<XMFLOAT3> sideways(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
      if (!flap[i] || bones[i].parent < 0 || !flap[bones[i].parent])
        continue;
      lengths[i] = XMVectorGetX(
          XMVector3Length(load(rest[i]) - load(rest[bones[i].parent])));
      int anchor = root[i];
      float nearest = 1e30f;
      for (size_t j = 0; j < bones.size(); ++j)
        if (flap[j] && root[j] == int(j) && int(j) != anchor && child[j] >= 0 &&
            child[anchor] >= 0 && bones[j].parent == bones[anchor].parent) {
          auto offset = load(rest[anchor]) - load(rest[j]);
          float gap = XMVectorGetX(XMVector3Length(offset));
          if (gap > .00001f && gap < nearest) {
            auto side = offset / gap;
            auto direction = load(rest[i]) - load(rest[bones[i].parent]);
            auto first = load(rest[child[anchor]]) - load(rest[anchor]);
            auto other = load(rest[child[j]]) - load(rest[j]);
            if (XMVectorGetX(XMVector3Length(first)) < .00001f ||
                XMVectorGetX(XMVector3Length(other)) < .00001f ||
                lengths[i] < .00001f)
              continue;
            // Only parallel ribbons have a common separation corridor.
            // Other flap chains can fan out or curve around the same joint.
            first = XMVector3Normalize(first);
            other = XMVector3Normalize(other);
            if (XMVectorGetX(XMVector3Dot(first, other)) < .98f ||
                std::abs(XMVectorGetX(XMVector3Dot(first, side))) > .001f ||
                std::abs(XMVectorGetX(XMVector3Dot(other, side))) > .001f ||
                std::abs(XMVectorGetX(
                    XMVector3Dot(direction / lengths[i], side))) > .001f)
              continue;
            nearest = gap;
            clearance[i] = gap * .2f;
            XMStoreFloat3(&sideways[i], offset / gap);
          }
        }
    }
    if (joint_dynamics.available) {
      const auto &settings = joint_dynamics;
      secondary_accumulator += reset ? 0 : seconds;
      int ticks = int((secondary_accumulator + 1e-8) * settings.hz);
      secondary_accumulator =
          std::max(0., secondary_accumulator - ticks / settings.hz);
      std::vector<XMFLOAT3> forces(bones.size());
      auto pinned = [&](size_t i) {
        int parent = bones[i].parent;
        return !flap[i] || parent < 0 || !flap[parent];
      };
      for (int tick = 0; tick < ticks; ++tick) {
        std::fill(forces.begin(), forces.end(), XMFLOAT3{});
        for (size_t i = 0; i < bones.size(); ++i)
          if (pinned(i))
            secondary_positions[i] = rest[i];
        // Original graph: equal/opposite Hooke forces along hierarchy edges.
        for (size_t i = 0; i < bones.size(); ++i) {
          int parent = bones[i].parent;
          if (pinned(i))
            continue;
          auto delta =
              load(secondary_positions[parent]) - load(secondary_positions[i]);
          float distance = XMVectorGetX(XMVector3Length(delta));
          if (distance < .00001f)
            continue;
          auto force = delta * (settings.tension_scale * settings.stiffness *
                                (distance - lengths[i]) / distance);
          XMStoreFloat3(&forces[i], XMLoadFloat3(&forces[i]) + force);
          XMStoreFloat3(&forces[parent], XMLoadFloat3(&forces[parent]) - force);
        }
        for (size_t i = 0; i < bones.size(); ++i) {
          if (pinned(i)) {
            secondary_velocity[i] = {};
            continue;
          }
          auto force =
              XMLoadFloat3(&forces[i]) +
              XMVectorSet(0, settings.gravity * settings.gravity_scale, 0, 0);
          auto velocity = load(secondary_velocity[i]) + force / settings.mass;
          XMFLOAT3 p, v;
          XMStoreFloat3(&p, load(secondary_positions[i]) + velocity);
          XMStoreFloat3(
              &v, velocity * (1 - settings.damping * settings.damping_scale));
          secondary_positions[i] = {p.x, p.y, p.z};
          secondary_velocity[i] = {v.x, v.y, v.z};
        }
        // The original pass visits both directed links of each graph edge.
        // Its environment scale triggers the correction, and the node's
        // stretch factor sets the corrected length (they are separate values).
        for (size_t i = 0; i < bones.size(); ++i) {
          if (!flap[i])
            continue;
          auto constrain = [&](size_t other, float length) {
            auto anchor = load(secondary_positions[i]);
            auto delta = load(secondary_positions[other]) - anchor;
            float distance = XMVectorGetX(XMVector3Length(delta));
            float threshold = length * settings.stretch_scale;
            if (distance > threshold && distance > .00001f) {
              XMFLOAT3 p;
              XMStoreFloat3(&p, anchor + delta * (threshold * settings.stretch /
                                                  distance));
              secondary_positions[other] = {p.x, p.y, p.z};
            }
          };
          int parent = bones[i].parent;
          if (parent >= 0 && flap[parent])
            constrain(size_t(parent), lengths[i]);
          for (size_t other = i + 1; other < bones.size(); ++other)
            if (flap[other] && bones[other].parent == int(i))
              constrain(other, lengths[other]);
        }
        // The game's manager writes simulated child transforms but preserves
        // each animated root attachment, and repins roots before the next step.
        for (size_t i = 0; i < bones.size(); ++i)
          if (pinned(i))
            secondary_positions[i] = rest[i];
      }
    }
    int steps = !joint_dynamics.available && seconds > 0 && !reset
                    ? int(std::ceil(seconds * 120))
                    : 0;
    float dt = steps ? seconds / steps : 0;
    for (int step = 0; step < steps; ++step) {
      for (size_t i = 0; i < bones.size(); ++i) {
        int parent = bones[i].parent;
        if (!flap[i] || parent < 0 || !flap[parent]) {
          secondary_positions[i] = secondary_previous[i] = rest[i];
          continue;
        }
        auto previous = secondary_positions[i];
        auto velocity =
            (load(previous) - load(secondary_previous[i])) * std::exp(-24 * dt);
        float speed = XMVectorGetX(XMVector3Length(velocity));
        float limit = lengths[i] * 2 * dt;
        if (speed > limit)
          velocity *= limit / speed;
        auto acceleration = (load(rest[i]) - load(previous)) * 180 +
                            XMVectorSet(0, lengths[i] * 4, 0, 0);
        XMFLOAT3 p;
        XMStoreFloat3(&p, load(previous) + velocity + acceleration * dt * dt);
        secondary_positions[i] = {p.x, p.y, p.z};
        secondary_previous[i] = previous;
      }
      for (size_t i = 0; i < bones.size(); ++i) {
        int parent = bones[i].parent;
        if (!flap[i] || parent < 0 || !flap[parent])
          continue;
        float length = lengths[i];
        auto anchor = load(secondary_positions[parent]);
        auto authored = load(rest[i]) - load(rest[parent]);
        if (length < .00001f) {
          secondary_positions[i] = secondary_positions[parent];
          continue;
        }
        authored /= length;
        auto direction = load(secondary_positions[i]) - anchor;
        if (XMVectorGetX(XMVector3Length(direction)) < .00001f)
          direction = authored;
        direction = XMVector3Normalize(direction);
        // Keep each segment near its authored direction so a long ribbon
        // cannot fold back through the head. Preserve its original length.
        constexpr float cosine = .93969262f, sine = .34202014f; // 20 degrees
        float alignment = XMVectorGetX(XMVector3Dot(direction, authored));
        if (alignment < cosine) {
          auto tangent = direction - authored * alignment;
          if (XMVectorGetX(XMVector3Length(tangent)) < .00001f)
            tangent = XMVector3Orthogonal(authored);
          direction = authored * cosine + XMVector3Normalize(tangent) * sine;
        }
        // Parallel tails share an attachment. Restrict sideways displacement
        // to a fraction of their authored spacing, without stretching joints.
        if (clearance[i] > 0) {
          auto side = XMLoadFloat3(&sideways[i]);
          float offset =
              XMVectorGetX(XMVector3Dot(anchor - load(rest[i]), side));
          float low = std::clamp((-clearance[i] - offset) / length, -1.f, 1.f);
          float high = std::clamp((clearance[i] - offset) / length, -1.f, 1.f);
          float old = XMVectorGetX(XMVector3Dot(direction, side));
          float component = std::clamp(old, low, high);
          if (component != old) {
            auto tangent = direction - side * old;
            if (XMVectorGetX(XMVector3Length(tangent)) < .00001f)
              tangent = XMVector3Orthogonal(side);
            direction = side * component +
                        XMVector3Normalize(tangent) *
                            std::sqrt(std::max(0.f, 1 - component * component));
          }
        }
        XMFLOAT3 p;
        XMStoreFloat3(&p, anchor + direction * length);
        secondary_positions[i] = {p.x, p.y, p.z};
      }
    }
    // Apply simulated transforms to the original weighted skin.
    // The executable-backed path uses recovered integration and skin bases;
    // unrecognized executables retain the bounded legacy preview instead.
    for (size_t i = 0; i < bones.size(); ++i)
      if (flap[i]) {
        int anchor = root[i];
        // The original manager (0x1366c0) leaves animated roots intact and
        // copies their basis to simulated children before writing positions.
        // Rotating a root toward its child swings scalp-side skin offsets
        // away from the head even though the root's position stays pinned.
        if (anchor == int(i)) {
          secondary_positions[i] = rest[i];
          continue;
        }
        if (joint_dynamics.available) {
          matrices[i] = matrices[anchor];
          matrices[i].r[3] =
              XMVectorSet(secondary_positions[i][0], secondary_positions[i][1],
                          secondary_positions[i][2], 1);
          bones[i].position = {secondary_positions[i][0] + origin[0],
                               -secondary_positions[i][1] - origin[1],
                               secondary_positions[i][2] + origin[2]};
          continue;
        }
        int other = child[i], base = int(i);
        if (other < 0) {
          other = int(i);
          base = bones[i].parent;
        }
        if (base < 0 || other < 0)
          continue;
        auto a = load(rest[other]) - load(rest[base]);
        auto b =
            load(secondary_positions[other]) - load(secondary_positions[base]);
        if (XMVectorGetX(XMVector3Length(a)) < .00001f ||
            XMVectorGetX(XMVector3Length(b)) < .00001f)
          continue;
        a = XMVector3Normalize(a);
        b = XMVector3Normalize(b);
        float dot = std::clamp(XMVectorGetX(XMVector3Dot(a, b)), -1.f, 1.f);
        auto axis = XMVector3Cross(a, b);
        if (XMVectorGetX(XMVector3Length(axis)) < .00001f)
          axis = XMVector3Orthogonal(a);
        matrices[i] =
            matrices[i] *
            XMMatrixTranslation(-rest[i][0], -rest[i][1], -rest[i][2]) *
            XMMatrixRotationAxis(axis, std::acos(dot)) *
            XMMatrixTranslation(secondary_positions[i][0],
                                secondary_positions[i][1],
                                secondary_positions[i][2]);
        bones[i].position = {secondary_positions[i][0] + origin[0],
                             -secondary_positions[i][1] - origin[1],
                             secondary_positions[i][2] + origin[2]};
      }
  } else {
    secondary_positions.clear();
    secondary_previous.clear();
    secondary_rest.clear();
    secondary_clip = -2;
    secondary_velocity.clear();
    secondary_accumulator = 0;
  }
  for (auto &part : parts) {
    if (part.skin.size() != part.vertices.size())
      continue;
    for (size_t i = 0; i < part.vertices.size(); ++i) {
      auto &skin = part.skin[i];
      XMVECTOR v = XMVectorZero();
      XMVECTOR normal = XMVectorZero();
      const auto &bind = i < part.bind.size() ? part.bind[i] : part.vertices[i];
      // Bone matrices use the game's Y-down coordinates; directions must use
      // the same basis as positions, without applying bone translations.
      auto bind_normal = XMVectorSet(bind.nx, -bind.ny, bind.nz, 0);
      float sum = 0;
      for (int k = 0; k < 4; ++k) {
        if (skin.weight[k] <= 0 || skin.bone[k] >= matrices.size())
          continue;
        auto &p = skin.local[k];
        v += XMVector3TransformCoord(XMVectorSet(p[0], p[1], p[2], 1),
                                     matrices[skin.bone[k]]) *
             skin.weight[k];
        normal +=
            XMVector3TransformNormal(bind_normal, matrices[skin.bone[k]]) *
            skin.weight[k];
        sum += skin.weight[k];
      }
      if (sum > 0) {
        XMFLOAT3 p;
        XMStoreFloat3(&p, v / sum);
        part.vertices[i].x = p.x + origin[0];
        part.vertices[i].y = -p.y - origin[1];
        part.vertices[i].z = p.z + origin[2];
        float length = XMVectorGetX(XMVector3Length(normal));
        if (std::isfinite(length) && length > .00001f) {
          XMStoreFloat3(&p, normal / length);
          part.vertices[i].nx = p.x;
          part.vertices[i].ny = -p.y;
          part.vertices[i].nz = p.z;
        } else {
          part.vertices[i].nx = bind.nx;
          part.vertices[i].ny = bind.ny;
          part.vertices[i].nz = bind.nz;
        }
      }
    }
  }
}
} // namespace shaolin::viewer

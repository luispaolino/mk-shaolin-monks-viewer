#include "assets.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace shaolin::viewer {
namespace {
using namespace DirectX;
template <class T> T read(std::span<const uint8_t> data, size_t at) {
  if (at > data.size() || sizeof(T) > data.size() - at)
    throw std::runtime_error("Truncated scenery animation");
  T result;
  std::memcpy(&result, data.data() + at, sizeof(result));
  return result;
}
float number(std::span<const uint8_t> data, size_t at) {
  float value = read<float>(data, at);
  if (!std::isfinite(value) || std::abs(value) > 1e9f)
    throw std::runtime_error("Invalid scenery animation value");
  return value;
}
XMMATRIX rotation(const Part &part) {
  return XMMatrixRotationZ(part.scene_rotation[2]) *
         XMMatrixRotationY(part.scene_rotation[1]) *
         XMMatrixRotationX(part.scene_rotation[0]);
}
XMMATRIX static_matrix(const Part &part) {
  return rotation(part) * XMMatrixTranslation(part.scene_position[0],
                                              part.scene_position[1],
                                              part.scene_position[2]);
}
float track_frame(const SceneryTrack &track, double seconds) {
  double frame = std::max(0., seconds) * 60;
  double end = track.frames, start = std::min(track.loop_start, track.frames);
  if (frame <= end)
    return float(frame);
  if (track.playback == 0)
    return float(std::fmod(frame, end + 1));
  if (track.playback == 2 || track.playback == 4) {
    if (track.playback == 2)
      start = 0;
    double span = end - start;
    if (span <= 0)
      return float(end);
    double phase = std::fmod(frame - start, span * 2);
    return float(start + (phase <= span ? phase : span * 2 - phase));
  }
  if (track.playback == 3)
    return float(start + std::fmod(frame - start, end - start + 1));
  return float(end);
}
SceneryKey sample(const SceneryTrack &track, double seconds) {
  float frame = track_frame(track, seconds);
  auto next =
      std::upper_bound(track.keys.begin(), track.keys.end(), frame,
                       [](float f, auto &key) { return f < key.frame; });
  auto &a = next == track.keys.begin() ? track.keys.front() : *(next - 1);
  auto &b = next == track.keys.end() ? track.keys.back() : *next;
  float blend =
      b.frame > a.frame
          ? std::clamp((frame - a.frame) / (b.frame - a.frame), 0.f, 1.f)
          : 0;
  SceneryKey result;
  for (size_t k = 0; k < 3; ++k) {
    result.translation[k] =
        a.translation[k] + (b.translation[k] - a.translation[k]) * blend;
    result.scale[k] = a.scale[k] + (b.scale[k] - a.scale[k]) * blend;
  }
  XMFLOAT4 q;
  auto qa =
      XMVectorSet(a.rotation[0], a.rotation[1], a.rotation[2], a.rotation[3]);
  auto qb =
      XMVectorSet(b.rotation[0], b.rotation[1], b.rotation[2], b.rotation[3]);
  XMStoreFloat4(&q, XMQuaternionSlerp(qa, qb, blend));
  result.rotation = {q.x, q.y, q.z, q.w};
  return result;
}
} // namespace
std::vector<SceneryClip> decode_scenery(std::span<const uint8_t> data) {
  auto version = read<uint32_t>(data, 0), size = read<uint32_t>(data, 4),
       offset = read<uint32_t>(data, 8), ambient = read<uint32_t>(data, 12),
       groups = read<uint32_t>(data, 16);
  if (size != data.size() || offset != 20)
    throw std::runtime_error("Invalid map animation header");
  if (version != 4) {
    if ((version == 2 || version == 3) && !ambient && !groups)
      return {};
    // The supported executable's map-animation loader also accepts version 4
    // only. Older unused exports have a different key representation.
    throw std::runtime_error("Legacy map animation version " +
                             std::to_string(version) + " remains static");
  }
  if (ambient > 20000 || groups > 2000)
    throw std::runtime_error("Map animation exceeds track limit");
  size_t at = offset, total_keys = 0, total_tracks = 0;
  auto tracks = [&](uint32_t count, SceneryClip &clip) {
    for (uint32_t i = 0; i < count; ++i) {
      if (++total_tracks > 20000)
        throw std::runtime_error("Map animation exceeds track limit");
      SceneryTrack track;
      auto keys_offset = read<uint32_t>(data, at);
      track.object = read<uint32_t>(data, at + 4);
      track.parent = read<uint32_t>(data, at + 8);
      auto count = read<uint32_t>(data, at + 12);
      track.frames = read<uint32_t>(data, at + 16);
      track.playback = read<uint32_t>(data, at + 20);
      track.loop_start = read<uint32_t>(data, at + 24);
      total_keys += count;
      if (keys_offset < 28 || keys_offset > data.size() - at || !count ||
          total_keys > 4000000 || track.frames > 32767 || track.playback > 5 ||
          track.loop_start > track.frames || track.parent == track.object)
        throw std::runtime_error("Invalid map animation track");
      at += keys_offset;
      uint8_t channels = read<uint8_t>(data, at + 1);
      if (!channels || (channels & ~7))
        throw std::runtime_error("Unsupported map animation channels");
      for (uint32_t k = 0; k < count; ++k) {
        SceneryKey key;
        key.frame = read<uint16_t>(data, at + 2);
        if (read<uint8_t>(data, at + 1) != channels ||
            key.frame > track.frames ||
            (!track.keys.empty() && key.frame <= track.keys.back().frame))
          throw std::runtime_error("Invalid map animation key order");
        // Source keys store both the integer frame and 3ds Max ticks. Playback
        // uses the integer frame, like cMapAnimObject in the supplied game.
        read<uint32_t>(data, at + 4);
        at += 8;
        if (channels & 4)
          for (auto &value : key.translation) {
            value = number(data, at);
            at += 4;
          }
        if (channels & 1) {
          float squared = 0;
          for (auto &value : key.rotation) {
            value = number(data, at);
            squared += value * value;
            at += 4;
          }
          if (!std::isfinite(squared) || squared < .000001f)
            throw std::runtime_error("Invalid scenery quaternion");
          for (auto &value : key.rotation)
            value /= std::sqrt(squared);
        }
        if (channels & 2)
          for (auto &value : key.scale) {
            value = number(data, at);
            at += 4;
          }
        track.keys.push_back(key);
      }
      clip.duration = std::max(clip.duration, float(track.frames + 1) / 60);
      clip.tracks.push_back(std::move(track));
    }
  };
  std::vector<SceneryClip> clips;
  SceneryClip base;
  base.name = "Ambient";
  base.duration = 0;
  tracks(ambient, base);
  clips.push_back(std::move(base));
  for (uint32_t g = 0; g < groups; ++g) {
    auto group_offset = read<uint32_t>(data, at);
    SceneryClip clip;
    clip.group = read<uint32_t>(data, at + 4);
    uint32_t count = read<uint32_t>(data, at + 8);
    if (group_offset < 12 || group_offset > data.size() - at || count > 20000)
      throw std::runtime_error("Invalid map animation group");
    at += group_offset;
    std::ostringstream name;
    name << "Group " << std::hex << clip.group;
    clip.name = name.str();
    clip.duration = 0;
    tracks(count, clip);
    clips.push_back(std::move(clip));
  }
  if (at != data.size())
    throw std::runtime_error("Unexpected map animation trailing data");
  if (!ambient && !groups)
    return {};
  return clips;
}
size_t Model::scrolling_parts() const {
  return std::count_if(parts.begin(), parts.end(), [](auto &part) {
    return part.uv_speed[0] != 0 || part.uv_speed[1] != 0;
  });
}
size_t Model::moving_parts() const {
  return std::count_if(parts.begin(), parts.end(),
                       [](auto &part) { return !part.scene_local.empty(); });
}
void Model::bind_scenery() {
  using namespace DirectX;
  if (scenery.empty() &&
      (scrolling_parts() ||
       std::any_of(parts.begin(), parts.end(),
                   [](const auto &part) { return part.flicker; })))
    scenery.push_back({"Ambient"});
  std::set<uint32_t> targets;
  for (auto &clip : scenery) {
    std::map<uint32_t, uint32_t> parents;
    for (auto &track : scenery.front().tracks)
      parents[track.object] = track.parent;
    for (auto &track : clip.tracks)
      parents[track.object] = track.parent;
    for (auto [object, parent] : parents) {
      std::set<uint32_t> chain;
      while (parent && parents.contains(parent)) {
        if (!chain.insert(parent).second || chain.size() > 512 ||
            parent == object)
          throw std::runtime_error(
              "Cyclic or excessive scenery animation hierarchy");
        parent = parents.at(parent);
      }
    }
    if (clip.duration <= 0)
      clip.duration = 10;
    for (auto &track : clip.tracks)
      targets.insert(track.object);
    if (clip.group)
      for (auto &track : clip.tracks) {
        auto match = std::find_if(parts.begin(), parts.end(), [&](auto &part) {
          return part.object == track.object && !part.scene_group.empty();
        });
        if (match != parts.end()) {
          clip.name = match->scene_group;
          break;
        }
      }
  }
  for (auto &part : parts) {
    if (!targets.contains(part.object))
      continue;
    auto inverse = XMMatrixInverse(nullptr, static_matrix(part));
    part.scene_local = part.bind;
    for (auto &vertex : part.scene_local) {
      XMFLOAT3 p;
      XMStoreFloat3(
          &p, XMVector3TransformCoord(
                  XMVectorSet(vertex.x, -vertex.y, vertex.z, 1), inverse));
      vertex.x = p.x;
      vertex.y = p.y;
      vertex.z = p.z;
      XMStoreFloat3(
          &p, XMVector3TransformNormal(
                  XMVectorSet(vertex.nx, -vertex.ny, vertex.nz, 0), inverse));
      vertex.nx = p.x;
      vertex.ny = p.y;
      vertex.nz = p.z;
    }
  }
}
void Model::animate_scenery(int clip, double seconds, bool textures,
                            bool meshes) {
  using namespace DirectX;
  if (!std::isfinite(seconds))
    throw std::runtime_error("Invalid scenery preview time");
  seconds = std::max(0., seconds);
  bool active = clip >= 0 && size_t(clip) < scenery.size();
  for (auto &part : parts) {
    if (part.flicker) {
      const auto &f = part.flicker_settings;
      double phase = active && textures ? seconds * f[2] * .628318530718 : 0;
      // The original background flicker writes a per-mesh alpha byte. Its
      // fixed-step sine uses 0.64 amplitude and the same 128 alpha scale as
      // vertices.
      part.opacity = float(std::clamp(
          (f[0] + (f[1] - f[0]) * .64 * (1 + std::sin(phase))) / 128, 0., 1.));
    }
    for (size_t k = 0; k < 2; ++k)
      part.uv_offset[k] = active && textures
                              ? float(std::fmod(seconds * part.uv_speed[k], 1.))
                              : 0;
    if (!part.scene_local.empty())
      part.vertices = part.bind;
  }
  if (!active || !meshes)
    return;
  std::map<uint32_t, const SceneryTrack *> tracks;
  for (auto &track : scenery.front().tracks)
    tracks[track.object] = &track;
  if (clip > 0)
    for (auto &track : scenery[clip].tracks)
      tracks[track.object] = &track;
  std::map<uint32_t, size_t> objects;
  for (size_t i = 0; i < parts.size(); ++i)
    objects.try_emplace(parts[i].object, i);
  std::map<uint32_t, XMMATRIX> matrices;
  std::set<uint32_t> visiting;
  std::function<XMMATRIX(uint32_t)> matrix = [&](uint32_t object) -> XMMATRIX {
    if (auto it = matrices.find(object); it != matrices.end())
      return it->second;
    if (!visiting.insert(object).second)
      throw std::runtime_error("Cyclic scenery animation parent");
    auto found = objects.find(object);
    auto track = tracks.find(object);
    XMMATRIX result = found == objects.end()
                          ? XMMatrixIdentity()
                          : static_matrix(parts[found->second]);
    if (track != tracks.end()) {
      auto key = sample(*track->second, seconds);
      auto q = XMVectorSet(key.rotation[0], key.rotation[1], key.rotation[2],
                           key.rotation[3]);
      auto p = key.translation;
      if (!track->second->parent && found != objects.end())
        for (size_t k = 0; k < 3; ++k)
          p[k] += parts[found->second].scene_position[k];
      result = XMMatrixScaling(key.scale[0], key.scale[1], key.scale[2]) *
               XMMatrixRotationQuaternion(q) *
               XMMatrixTranslation(p[0], p[1], p[2]);
      if (track->second->parent)
        result *= matrix(track->second->parent);
    }
    visiting.erase(object);
    matrices.emplace(object, result);
    return result;
  };
  for (auto &part : parts) {
    if (part.scene_local.empty() || !tracks.contains(part.object))
      continue;
    auto transform = matrix(part.object);
    XMVECTOR determinant;
    auto inverse = XMMatrixInverse(&determinant, transform);
    auto normal = std::abs(XMVectorGetX(determinant)) > .000001f
                      ? XMMatrixTranspose(inverse)
                      : transform;
    for (size_t i = 0; i < part.vertices.size(); ++i) {
      const auto &local = part.scene_local[i];
      auto &v = part.vertices[i];
      XMFLOAT3 p;
      XMStoreFloat3(
          &p, XMVector3TransformCoord(XMVectorSet(local.x, local.y, local.z, 1),
                                      transform));
      v.x = p.x;
      v.y = -p.y;
      v.z = p.z;
      auto direction = XMVector3TransformNormal(
          XMVectorSet(local.nx, local.ny, local.nz, 0), normal);
      float length = XMVectorGetX(XMVector3Length(direction));
      if (std::isfinite(length) && length > .000001f) {
        XMStoreFloat3(&p, direction / length);
        v.nx = p.x;
        v.ny = -p.y;
        v.nz = p.z;
      }
    }
  }
}
} // namespace shaolin::viewer

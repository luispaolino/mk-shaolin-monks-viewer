#include "assets.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>

namespace shaolin::viewer {
const char *mesh_role_name(MeshRole role) {
  switch (role) {
  case MeshRole::Sky:
    return "Camera-relative sky";
  case MeshRole::Hidden:
    return "Hidden / helper mesh";
  case MeshRole::Camera:
    return "Camera volume";
  case MeshRole::ShadowReceiver:
    return "Shadow receiver / projection surface";
  case MeshRole::ShadowOverlay:
    return "Vertex-alpha shadow overlay (inferred)";
  case MeshRole::LightShaft:
    return "Additive light shaft (inferred)";
  case MeshRole::SolidEffect:
    return "Opaque effect-flagged prop (inferred)";
  case MeshRole::FlickerSurface:
    return "Source opacity-flicker surface";
  case MeshRole::ParticleEmitter:
    return "Particle emitter (preview)";
  case MeshRole::DebugFloor:
    return "Character debug floor";
  default:
    return "Surface / unclassified object";
  }
}
void Model::flip_object(size_t selected) {
  for (size_t i = 0; i < parts.size(); ++i) {
    if (!same_object(i, selected))
      continue;
    auto &p = parts[i];
    for (auto *vertices : {&p.vertices, &p.bind, &p.scene_local}) {
      for (size_t t = 0; t + 2 < vertices->size(); t += 3)
        std::swap((*vertices)[t + 1], (*vertices)[t + 2]);
      for (auto &v : *vertices) {
        v.nx = -v.nx;
        v.ny = -v.ny;
        v.nz = -v.nz;
      }
    }
    for (size_t t = 0; t + 2 < p.skin.size(); t += 3)
      std::swap(p.skin[t + 1], p.skin[t + 2]);
  }
}
void Model::set_winding_repair(bool enabled) {
  if (winding_repair == enabled)
    return;
  for (auto &p : parts)
    if (p.legacy_two_sided)
      p.two_sided = enabled;
  for (auto &p : parts)
    for (size_t at : p.repaired_faces)
      for (auto *vertices : {&p.vertices, &p.bind, &p.scene_local}) {
        if (at + 2 >= vertices->size())
          continue;
        std::swap((*vertices)[at + 1], (*vertices)[at + 2]);
        for (size_t k = at; k < at + 3; ++k) {
          auto &v = (*vertices)[k];
          v.nx = -v.nx;
          v.ny = -v.ny;
          v.nz = -v.nz;
        }
      }
  winding_repair = enabled;
}
size_t Model::repair_winding() {
  struct Face {
    size_t part, at;
    std::array<size_t, 3> ids;
  };
  size_t changed = 0;
  std::map<uint32_t, std::vector<size_t>> groups;
  for (size_t p = 0; p < parts.size(); ++p)
    if (!parts[p].sky && !parts[p].helper && parts[p].skin.empty())
      groups[parts[p].mesh_id].push_back(p);
  for (const auto &[id, group] : groups) {
    bool authored = std::any_of(group.begin(), group.end(), [&](size_t p) {
      return std::any_of(parts[p].vertices.begin(), parts[p].vertices.end(),
                         [](const auto &v) {
                           return v.nx * v.nx + v.ny * v.ny + v.nz * v.nz >
                                  .00001f;
                         });
    });
    // Authored normals supply orientation evidence that must not be replaced
    // by the terrain heuristic used for compact, normal-less legacy exports.
    if (authored)
      continue;
    std::map<std::array<int64_t, 3>, size_t> welded;
    std::vector<Face> faces;
    using Edge = std::pair<size_t, size_t>;
    std::map<Edge, std::vector<std::pair<size_t, bool>>> edges;
    for (size_t p : group) {
      auto &vertices = parts[p].vertices;
      for (size_t at = 0; at + 2 < vertices.size(); at += 3) {
        Face face{p, at, {}};
        for (size_t k = 0; k < 3; ++k) {
          const auto &v = vertices[at + k];
          std::array<int64_t, 3> key{std::llround(v.x * 1024.),
                                     std::llround(v.y * 1024.),
                                     std::llround(v.z * 1024.)};
          face.ids[k] = welded.emplace(key, welded.size()).first->second;
        }
        for (size_t k = 0; k < 3; ++k) {
          auto a = face.ids[k], b = face.ids[(k + 1) % 3];
          if (a != b)
            edges[std::minmax(a, b)].push_back({faces.size(), a < b});
        }
        faces.push_back(face);
      }
    }
    std::vector<std::vector<std::pair<size_t, bool>>> adjacent(faces.size());
    std::vector<bool> boundary(faces.size());
    for (const auto &[edge, uses] : edges) {
      if (uses.size() == 2 && uses[0].first != uses[1].first) {
        bool opposite = uses[0].second == uses[1].second;
        adjacent[uses[0].first].push_back({uses[1].first, opposite});
        adjacent[uses[1].first].push_back({uses[0].first, opposite});
      } else
        for (const auto &[f, direction] : uses)
          boundary[f] = true;
    }
    // These older exports have neither authored normals nor strip direction
    // bits. An open shell cannot establish an inside/outside convention:
    // room walls face inward, whereas solid props face outward. Retain the
    // orientation repair for normals, but show both sides in the preview.
    if (stage && legacy_winding &&
        std::any_of(boundary.begin(), boundary.end(), [](bool b) { return b; }))
      for (size_t p : group)
        parts[p].two_sided = parts[p].legacy_two_sided = true;
    std::vector<int> flip(faces.size(), -1);
    struct Component {
      std::vector<size_t> faces;
      bool invert, ambiguous, source_invert;
      double area;
    };
    std::vector<Component> components;
    double shell_area = 0;
    bool shell_bias = false;
    for (size_t seed = 0; seed < faces.size(); ++seed) {
      if (flip[seed] >= 0)
        continue;
      std::vector<size_t> component{seed};
      flip[seed] = 0;
      bool closed = true;
      double up = 0, area = 0, volume = 0, vote = 0;
      double positive[3]{}, negative[3]{}, normal_sum[3]{};
      float lo[3]{1e30f, 1e30f, 1e30f}, hi[3]{-1e30f, -1e30f, -1e30f};
      for (size_t n = 0; n < component.size(); ++n) {
        size_t f = component[n];
        closed &= !boundary[f];
        for (auto [other, opposite] : adjacent[f])
          if (flip[other] < 0) {
            flip[other] = flip[f] ^ int(opposite);
            component.push_back(other);
          }
        auto &face = faces[f];
        auto &vertices = parts[face.part].vertices;
        const auto &a = vertices[face.at], &b = vertices[face.at + 1],
                   &c = vertices[face.at + 2];
        double x = (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y);
        double y = (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z);
        double z = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        double length = std::sqrt(x * x + y * y + z * z),
               sign = flip[f] ? -1 : 1;
        area += length;
        up += y * sign;
        vote += length * sign;
        volume += (a.x * x + a.y * y + a.z * z) * sign;
        double normal[]{x * sign, y * sign, z * sign};
        for (size_t k = 0; k < 3; ++k) {
          positive[k] += std::max(0., normal[k]);
          negative[k] += std::max(0., -normal[k]);
          normal_sum[k] += normal[k];
        }
        for (size_t k = 0; k < 3; ++k) {
          auto &v = vertices[face.at + k];
          float xyz[]{v.x, v.y, v.z};
          for (size_t j = 0; j < 3; ++j) {
            lo[j] = std::min(lo[j], xyz[j]);
            hi[j] = std::max(hi[j], xyz[j]);
          }
        }
      }
      // An open terrain sheet has no signed volume. Use its broad horizontal
      // orientation; otherwise keep the majority of the authored faces.
      bool terrain =
          std::abs(up) > area * .6 &&
          hi[1] - lo[1] < std::max(hi[0] - lo[0], hi[2] - lo[2]) * .5;
      double centered_volume = volume;
      for (size_t k = 0; k < 3; ++k)
        centered_volume -= (lo[k] + hi[k]) * .5 * normal_sum[k];
      float span = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
      bool shell = !terrain &&
                   std::min({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]}) >
                       span * .1f &&
                   std::abs(centered_volume) > area * span * .04;
      double opposing =
          std::abs(centered_volume) > area * span * .25 ? .001 : .02;
      for (size_t k = 0; k < 3; ++k)
        shell &= positive[k] > area * opposing && negative[k] > area * opposing;
      // Open-bottom props have no closed signed volume, but opposing sides
      // still provide radial orientation evidence. Planar room surfaces do not.
      bool invert = closed    ? volume < 0
                    : terrain ? up < 0
                    : shell   ? centered_volume < 0
                              : vote < 0;
      if (shell && !closed && area > shell_area) {
        shell_area = area;
        shell_bias = invert != (vote < 0);
      }
      components.push_back(
          {std::move(component), invert, !closed && !shell, vote < 0, area});
    }
    for (auto &component : components) {
      // Small disconnected pieces, such as horns, inherit a larger open
      // shell's evidence rather than retaining the opposite overall winding.
      if (component.ambiguous && component.area < shell_area * .2 && shell_bias)
        component.invert = !component.source_invert;
      for (size_t f : component.faces)
        if (bool(flip[f]) != component.invert) {
          auto &face = faces[f];
          auto &p = parts[face.part];
          std::swap(p.vertices[face.at + 1], p.vertices[face.at + 2]);
          if (p.bind.size() == p.vertices.size())
            std::swap(p.bind[face.at + 1], p.bind[face.at + 2]);
          p.repaired_faces.push_back(face.at);
          ++changed;
        }
    }
  }
  return changed;
}
void Model::prepare_materials() {
  for (auto &part : parts) {
    if (part.opaque) {
      // Character alpha often stores lighting, but a substantial binary mask
      // is a cutout (for example the Priest's torn robe hem).
      if (!stage && part.texture >= 0 &&
          size_t(part.texture) < textures.size()) {
        const auto &pixels = textures[part.texture].rgba;
        size_t zero = 0, full = 0;
        for (size_t i = 3; i < pixels.size(); i += 4) {
          zero += pixels[i] == 0;
          full += pixels[i] == 255;
        }
        size_t count = pixels.size() / 4;
        if (count && (zero + full) * 20 >= count * 19 && zero * 100 >= count &&
            full * 2 >= count) {
          part.opaque = false;
          part.alpha_cutoff = .5f;
        }
      }
      if (part.opaque)
        continue;
    }
    if (part.flicker) {
      part.depth_write = false;
      part.alpha_cutoff = 0;
      if (part.role == MeshRole::Surface)
        part.role = MeshRole::FlickerSurface;
    }
    if (stage && !part.sky && !part.helper && part.texture >= 0 &&
        size_t(part.texture) < textures.size()) {
      const auto &image = textures[part.texture];
      unsigned low = 255, high = 0;
      for (const auto &v : part.bind) {
        low = std::min(low, v.color >> 24);
        high = std::max(high, v.color >> 24);
      }
      bool opaque_texture = !image.rgba.empty();
      for (size_t i = 3; i < image.rgba.size(); i += 4)
        opaque_texture &= image.rgba[i] == 255;
      // Some solid props carry the same flag as effects. A fully opaque,
      // depth-writing base surface must occlude the scene rather than add to
      // it.
      float lo[3]{1e30f, 1e30f, 1e30f}, hi[3]{-1e30f, -1e30f, -1e30f};
      for (const auto &v : part.bind) {
        float xyz[]{v.x, v.y, v.z};
        for (size_t k = 0; k < 3; ++k) {
          lo[k] = std::min(lo[k], xyz[k]);
          hi[k] = std::max(hi[k], xyz[k]);
        }
      }
      bool volume =
          std::min({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]}) >
          std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]}) * .1f;
      if (part.blend == BlendMode::Additive && part.depth_write && low >= 128 &&
          opaque_texture && volume && part.bind.size() >= 24) {
        part.role = MeshRole::SolidEffect;
        part.blend = BlendMode::Alpha;
        part.emissive = false;
        part.alpha_cutoff = .15f;
      }
      std::string name = part.name;
      std::transform(name.begin(), name.end(), name.begin(),
                     [](unsigned char c) { return char(std::tolower(c)); });
      bool beam = name.find("lightbeam") != std::string::npos;
      bool mask = opaque_texture && image.width >= 16 && image.height >= 4;
      int darkest = 255, brightest = 0;
      for (size_t p = 0; mask && p < image.rgba.size(); p += 4) {
        auto r = image.rgba[p], g = image.rgba[p + 1], b = image.rgba[p + 2];
        mask &= std::max({r, g, b}) - std::min({r, g, b}) <= 5;
        size_t column = p % (size_t(image.width) * 4);
        for (size_t k = 0; k < 3; ++k)
          mask &= image.rgba[p + k] == image.rgba[column + k];
        darkest = std::min(darkest, int(r));
        brightest = std::max(brightest, int(r));
      }
      beam |= mask && brightest - darkest >= 64 && low < high && high < 128;
      if (beam && (part.render_flags & 0x80000) &&
          part.role == MeshRole::Surface) {
        part.role = MeshRole::LightShaft;
        part.blend = BlendMode::Additive;
        part.emissive = true;
        part.depth_write = false;
        part.alpha_cutoff = 0;
      }
    }
    if (stage && !part.sky && !part.helper && part.role == MeshRole::Surface &&
        part.blend == BlendMode::Alpha && !part.bind.empty()) {
      bool black = true;
      unsigned low = 255, high = 0;
      float lo[3]{1e30f, 1e30f, 1e30f}, hi[3]{-1e30f, -1e30f, -1e30f};
      for (const auto &v : part.bind) {
        black &= (v.color & 0xffffff) == 0;
        low = std::min(low, v.color >> 24);
        high = std::max(high, v.color >> 24);
        float xyz[]{v.x, v.y, v.z};
        for (size_t k = 0; k < 3; ++k) {
          lo[k] = std::min(lo[k], xyz[k]);
          hi[k] = std::max(hi[k], xyz[k]);
        }
      }
      // Some baked shadows reuse the caster's diffuse material. Their planar
      // black vertices and fading opacity carry the visible shadow instead.
      if (black && high > low && high <= 128 &&
          hi[1] - lo[1] <
              std::max(.01f, std::max(hi[0] - lo[0], hi[2] - lo[2]) * .001f)) {
        part.role = MeshRole::ShadowOverlay;
        part.alpha_cutoff = 0;
        part.depth_write = false;
      }
    }
    bool gradient =
        std::any_of(part.bind.begin(), part.bind.end(), [](auto &v) {
          auto alpha = v.color >> 24;
          return alpha > 0 && alpha < 128;
        });
    if (part.texture >= 0 && size_t(part.texture) < textures.size()) {
      const auto &pixels = textures[part.texture].rgba;
      size_t middle = 0, transparent = 0;
      bool binary = true, dark = true;
      for (size_t i = 3; i < pixels.size(); i += 4) {
        middle += pixels[i] > 8 && pixels[i] < 247;
        binary &= pixels[i] == 0 || pixels[i] == 255;
        transparent += pixels[i] == 0;
        if (pixels[i] == 0)
          dark &= std::max({pixels[i - 3], pixels[i - 2], pixels[i - 1]}) <= 64;
      }
      // Some diffuse exports mark sparse dark detail as transparent black.
      // Do not punch those texels through otherwise solid, depth-writing
      // scenery. Substantial masks, smooth fades and effect layers keep alpha.
      bool solid =
          stage && !part.sky && !part.helper && !part.flicker &&
          part.role == MeshRole::Surface && part.depth_write &&
          part.blend == BlendMode::Alpha && !gradient && !part.bind.empty() &&
          std::all_of(part.bind.begin(), part.bind.end(),
                      [](const auto &v) { return (v.color >> 24) >= 128; }) &&
          binary && dark && transparent &&
          transparent * 20 <= pixels.size() / 4;
      if (solid) {
        part.inferred_solid_alpha = true;
        part.texture_alpha = false;
      }
      // A few filtered edge texels still belong to an alpha-tested cutout.
      gradient |= middle > pixels.size() / 4 / 20;
    }
    if (gradient || part.sky || part.blend == BlendMode::Multiply) {
      part.alpha_cutoff = 0;
      if (part.blend != BlendMode::Additive)
        part.depth_write = false;
    }
    if (part.legacy_two_sided &&
        (!part.depth_write || part.emissive || part.role != MeshRole::Surface))
      part.two_sided = part.legacy_two_sided = false;
  }
}
bool Model::same_object(size_t first, size_t second) const {
  return first < parts.size() && second < parts.size() &&
         (first == second || (parts[first].mesh_id != UINT32_MAX &&
                              parts[first].mesh_id == parts[second].mesh_id));
}
bool Model::object_bounds(size_t selected, std::array<float, 3> &lo,
                          std::array<float, 3> &hi) const {
  for (const auto &e : particle_emitters)
    if (e.part == selected)
      return object_bounds(e.anchor, lo, hi);
  lo.fill(std::numeric_limits<float>::max());
  hi.fill(-std::numeric_limits<float>::max());
  bool found = false;
  for (size_t p = 0; p < parts.size(); ++p)
    if (parts[p].visible && same_object(p, selected))
      for (const auto &vertex : parts[p].vertices) {
        float xyz[] = {vertex.x, vertex.y, vertex.z};
        for (int k = 0; k < 3; ++k) {
          lo[k] = std::min(lo[k], xyz[k]);
          hi[k] = std::max(hi[k], xyz[k]);
        }
        found = true;
      }
  return found;
}
void Model::prepare_normals() {
  authored_normals = generated_normals = 0;
  std::vector<std::vector<bool>> generated(parts.size());
  for (size_t p = 0; p < parts.size(); ++p) {
    auto &part = parts[p];
    generated[p].resize(part.vertices.size());
    for (size_t i = 0; i < part.vertices.size(); ++i) {
      auto &vertex = part.vertices[i];
      float length = std::sqrt(vertex.nx * vertex.nx + vertex.ny * vertex.ny +
                               vertex.nz * vertex.nz);
      if (std::isfinite(length) && length > .00001f) {
        ++authored_normals;
      } else {
        generated[p][i] = true;
        // Compact scenery can omit normals. Keep its hard triangle edges;
        // authored vertex normals on character meshes must remain distinct.
        size_t triangle = i / 3 * 3;
        if (triangle + 2 < part.vertices.size()) {
          auto &a = part.vertices[triangle];
          auto &b = part.vertices[triangle + 1];
          auto &c = part.vertices[triangle + 2];
          float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
          float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
          vertex.nx = uy * vz - uz * vy;
          vertex.ny = uz * vx - ux * vz;
          vertex.nz = ux * vy - uy * vx;
          length = std::sqrt(vertex.nx * vertex.nx + vertex.ny * vertex.ny +
                             vertex.nz * vertex.nz);
        }
        if (!std::isfinite(length) || length <= .00001f) {
          vertex.nx = vertex.nz = 0;
          vertex.ny = length = 1;
        }
        ++generated_normals;
      }
      vertex.nx /= length;
      vertex.ny /= length;
      vertex.nz /= length;
      if (i < part.bind.size()) {
        part.bind[i].nx = vertex.nx;
        part.bind[i].ny = vertex.ny;
        part.bind[i].nz = vertex.nz;
      }
    }
  }
  if (!stage)
    return;
  struct Normal {
    double x, y, z, weight;
  };
  using Key = std::array<int64_t, 4>;
  auto key = [&](size_t p, const Vertex &v) -> Key {
    return {parts[p].mesh_id == UINT32_MAX ? -int64_t(p) - 1
                                           : int64_t(parts[p].mesh_id),
            std::llround(v.x * 1024.), std::llround(v.y * 1024.),
            std::llround(v.z * 1024.)};
  };
  std::map<Key, std::vector<Normal>> adjacent;
  for (size_t p = 0; p < parts.size(); ++p) {
    const auto &vertices = parts[p].vertices;
    if (!parts[p].skin.empty())
      continue;
    for (size_t t = 0; t + 2 < vertices.size(); t += 3) {
      const auto &a = vertices[t], &b = vertices[t + 1], &c = vertices[t + 2];
      double x = (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y),
             y = (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
             z = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
      double length = std::sqrt(x * x + y * y + z * z);
      if (length > .00001)
        for (size_t i = t; i < t + 3; ++i)
          adjacent[key(p, vertices[i])].push_back(
              {x / length, y / length, z / length, length});
    }
  }
  // Compact stages omit normals. Smooth only their generated normals across
  // UV/material seams, retaining creases over 60 degrees and authored normals.
  for (size_t p = 0; p < parts.size(); ++p) {
    auto &part = parts[p];
    if (!part.skin.empty())
      continue;
    for (size_t i = 0; i < part.vertices.size(); ++i) {
      if (!generated[p][i])
        continue;
      auto &v = part.vertices[i];
      double x = 0, y = 0, z = 0;
      for (const auto &n : adjacent[key(p, v)])
        if (n.x * v.nx + n.y * v.ny + n.z * v.nz > .5) {
          x += n.x * n.weight;
          y += n.y * n.weight;
          z += n.z * n.weight;
        }
      double length = std::sqrt(x * x + y * y + z * z);
      if (length <= .00001)
        continue;
      v.nx = float(x / length);
      v.ny = float(y / length);
      v.nz = float(z / length);
      if (i < part.bind.size()) {
        part.bind[i].nx = v.nx;
        part.bind[i].ny = v.ny;
        part.bind[i].nz = v.nz;
      }
    }
  }
}
} // namespace shaolin::viewer

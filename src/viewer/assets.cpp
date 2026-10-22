#include "assets.h"
#include <DirectXMath.h>
#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <miniz.h>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace shaolin::viewer {
namespace {
constexpr size_t limit = 128 * 1024 * 1024;
void range(std::span<const uint8_t> b, size_t at, size_t n) {
  if (at > b.size() || n > b.size() - at)
    throw std::runtime_error("Truncated asset or invalid offset");
}
template <class T> T value(std::span<const uint8_t> b, size_t at) {
  range(b, at, sizeof(T));
  T out;
  std::memcpy(&out, b.data() + at, sizeof(T));
  return out;
}
uint32_t u32(std::span<const uint8_t> b, size_t at) {
  return value<uint32_t>(b, at);
}
float f32(std::span<const uint8_t> b, size_t at) {
  float f = value<float>(b, at);
  if (!std::isfinite(f) || std::abs(f) > 1e9f)
    throw std::runtime_error("Invalid mesh coordinate");
  return f;
}
bool tag(std::span<const uint8_t> b, const char *s) {
  return b.size() >= 4 && !std::memcmp(b.data(), s, 4);
}
std::string label(std::span<const uint8_t> b, size_t at, size_t n) {
  range(b, at, n);
  std::string s;
  for (size_t i = at; i < at + n && b[i]; ++i) {
    if (b[i] < 32 || b[i] > 126)
      return {};
    s += char(b[i]);
  }
  return s.size() >= 3 ? s : std::string{};
}
std::map<std::string, std::string> mesh_properties(std::span<const uint8_t> b,
                                                   size_t at,
                                                   uint64_t &render_flags,
                                                   uint64_t &object_flags) {
  std::map<std::string, std::string> properties;
  std::set<size_t> seen;
  while (at) {
    if (!seen.insert(at).second || seen.size() > 10000)
      throw std::runtime_error("Invalid mesh property chain");
    range(b, at, 8);
    if (u32(b, at + 4) == 15) {
      range(b, at, 32);
      auto count = u32(b, at + 8);
      if (count > 64)
        throw std::runtime_error("Invalid mesh attribute count");
      uint64_t mask = value<uint64_t>(b, at + 16);
      if (count < 64)
        mask &= (uint64_t(1) << count) - 1;
      size_t data = u32(b, at + 24);
      if (mask && !data)
        throw std::runtime_error("Missing mesh attribute data");
      range(b, data, size_t(std::popcount(mask)) * 8);
      // Attribute 20 contains the render flags. Only present attributes have
      // an entry in the packed uint64 table; absent earlier slots take no
      // space.
      if (mask & (uint64_t(1) << 20)) {
        size_t index = std::popcount(mask & ((uint64_t(1) << 20) - 1));
        render_flags = value<uint64_t>(b, data + index * 8);
      }
      if (mask & 1)
        object_flags = value<uint64_t>(b, data);
    } else if (u32(b, at + 4) == 19) {
      range(b, at, 40);
      auto text = [&](size_t start) {
        std::string result;
        for (size_t k = 0; k < 16 && b[start + k]; ++k) {
          if (b[start + k] < 32 || b[start + k] > 126)
            break;
          result += char(b[start + k]);
        }
        return result;
      };
      properties[text(at + 8)] = text(at + 24);
    }
    at = u32(b, at);
  }
  return properties;
}
float property_number(const std::map<std::string, std::string> &properties,
                      const char *name, float fallback = 0) {
  auto found = properties.find(name);
  if (found == properties.end())
    return fallback;
  float rate = 0;
  auto &text = found->second;
  auto result = std::from_chars(text.data(), text.data() + text.size(), rate);
  if (result.ec != std::errc{} || !std::isfinite(rate) ||
      std::abs(rate) > 10000)
    throw std::runtime_error("Invalid numeric mesh property");
  return rate;
}
float scroll_rate(const std::map<std::string, std::string> &properties,
                  const char *name) {
  float rate = property_number(properties, name);
  // 999 requests the original renderer's random-offset mode, not a scrolling
  // speed. Keep that unsupported mode static rather than moving it at 999 UV/s.
  return rate == 999 ? 0 : rate;
}
Bytes file_read(const std::filesystem::path &path, uint64_t at, uint32_t n) {
  if (n > limit)
    throw std::runtime_error("Asset exceeds the viewer size limit");
  std::ifstream f(path, std::ios::binary);
  if (!f)
    throw std::runtime_error("Cannot open game archive: " +
                             path.filename().string());
  f.seekg(0, std::ios::end);
  auto size = f.tellg();
  if (size < 0 || at > uint64_t(size) || n > uint64_t(size) - at)
    throw std::runtime_error("Game archive is incomplete");
  Bytes out(n);
  f.seekg(std::streamoff(at));
  if (n && !f.read(reinterpret_cast<char *>(out.data()), n))
    throw std::runtime_error("Cannot read game archive");
  return out;
}
std::array<uint8_t, 4> rgb565(uint16_t c) {
  return {uint8_t(((c >> 11) & 31) * 255 / 31),
          uint8_t(((c >> 5) & 63) * 255 / 63), uint8_t((c & 31) * 255 / 31),
          255};
}
void block(std::span<const uint8_t> b, size_t off, int format, uint8_t *out) {
  size_t color = off + (format == 12 ? 0 : 8);
  range(b, off, format == 12 ? 8 : 16);
  auto c0 = value<uint16_t>(b, color), c1 = value<uint16_t>(b, color + 2);
  std::array<std::array<uint8_t, 4>, 4> c{rgb565(c0), rgb565(c1)};
  for (int k = 0; k < 3; ++k) {
    if (c0 > c1 || format != 12) {
      c[2][k] = uint8_t((2 * c[0][k] + c[1][k]) / 3);
      c[3][k] = uint8_t((c[0][k] + 2 * c[1][k]) / 3);
    } else {
      c[2][k] = uint8_t((c[0][k] + c[1][k]) / 2);
      c[3][k] = 0;
    }
  }
  c[2][3] = 255;
  c[3][3] = (c0 > c1 || format != 12) ? 255 : 0;
  auto bits = u32(b, color + 4);
  std::array<uint8_t, 8> alpha{};
  uint64_t abits = 0;
  if (format == 15) {
    alpha[0] = b[off];
    alpha[1] = b[off + 1];
    if (alpha[0] > alpha[1])
      for (int k = 1; k <= 6; ++k)
        alpha[k + 1] = uint8_t(((7 - k) * alpha[0] + k * alpha[1]) / 7);
    else {
      for (int k = 1; k <= 4; ++k)
        alpha[k + 1] = uint8_t(((5 - k) * alpha[0] + k * alpha[1]) / 5);
      alpha[6] = 0;
      alpha[7] = 255;
    }
    for (int k = 0; k < 6; ++k)
      abits |= uint64_t(b[off + 2 + k]) << (k * 8);
  }
  for (int i = 0; i < 16; ++i) {
    auto pixel = c[(bits >> (i * 2)) & 3];
    if (format == 14)
      pixel[3] = uint8_t(((b[off + i / 2] >> ((i & 1) * 4)) & 15) * 17);
    if (format == 15)
      pixel[3] = alpha[(abits >> (i * 3)) & 7];
    std::memcpy(out + i * 4, pixel.data(), 4);
  }
}
size_t morton(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
  size_t out = 0, bit = 1;
  for (uint32_t i = 1; i < w || i < h; i <<= 1) {
    if (i < w) {
      if (x & i)
        out |= bit;
      bit <<= 1;
    }
    if (i < h) {
      if (y & i)
        out |= bit;
      bit <<= 1;
    }
  }
  return out;
}
} // namespace
Bytes unpack(std::span<const uint8_t> b, std::string *name) {
  if (!tag(b, "EWDF"))
    return Bytes(b.begin(), b.end());
  auto n = u32(b, 4), head = u32(b, 8);
  if (n > limit || head < 17 || head > 4096 || head > b.size())
    throw std::runtime_error("Invalid EWDF header");
  if (name)
    *name = label(b, 16, head - 16);
  Bytes out(n);
  size_t written = tinfl_decompress_mem_to_mem(
      out.data(), out.size(), b.data() + head, b.size() - head, 0);
  if (written != n)
    throw std::runtime_error("Corrupt EWDF compressed data");
  return out;
}
std::vector<Bytes> members(std::span<const uint8_t> b) {
  if (!tag(b, "PWF "))
    throw std::runtime_error("Invalid PWF archive");
  auto n = u32(b, 12);
  if (n > 100000)
    throw std::runtime_error("Invalid PWF entry count");
  range(b, 32, size_t(n) * 8);
  std::vector<Bytes> out;
  out.reserve(n);
  for (uint32_t i = 0; i < n; ++i) {
    uint64_t q = value<uint64_t>(b, 32 + size_t(i) * 8);
    uint64_t at = uint32_t(q);
    uint32_t size = uint32_t(q >> 32);
    if (q >> 63) {
      at = (q & 0x3fffff) * 2048;
      size = uint32_t((q >> 24) & 0xfffff) * 4;
    }
    range(b, size_t(at), size);
    out.push_back(unpack(b.subspan(size_t(at), size)));
  }
  return out;
}
Image decode_xpr(std::span<const uint8_t> b) {
  if (!tag(b, "XPR0"))
    throw std::runtime_error("Unsupported texture container");
  auto head = u32(b, 8), fmt = u32(b, 24), sz = u32(b, 28),
       format = (fmt >> 8) & 255;
  uint32_t w = 1u << ((fmt >> 20) & 15), h = 1u << ((fmt >> 24) & 15),
           pitch = 0;
  bool compressed = format == 12 || format == 14 || format == 15;
  bool linear = format == 0x10 || format == 0x11 || format == 0x12 ||
                format == 0x13 || format == 0x1c || format == 0x1d ||
                format == 0x1e || format == 0x1f || format == 0x20;
  if (linear) {
    w = (sz & 4095) + 1;
    h = ((sz >> 12) & 4095) + 1;
    pitch = (((sz >> 24) & 255) + 1) * 64;
  }
  if (!w || !h || w > 4096 || h > 4096)
    throw std::runtime_error("Unsupported texture dimensions");
  size_t start = size_t(head) + (u32(b, 16) & 0x0fffffff);
  range(b, start, 0);
  Image img{w, h, Bytes(size_t(w) * h * 4)};
  if (compressed) {
    size_t size = format == 12 ? 8 : 16, bw = (w + 3) / 4, bh = (h + 3) / 4;
    range(b, start, bw * bh * size);
    for (size_t y = 0; y < bh; ++y)
      for (size_t x = 0; x < bw; ++x) {
        uint8_t pixels[64];
        block(b, start + (y * bw + x) * size, int(format), pixels);
        for (uint32_t j = 0; j < 4; ++j)
          for (uint32_t k = 0; k < 4; ++k)
            if (x * 4 + k < w && y * 4 + j < h)
              std::memcpy(img.rgba.data() + ((y * 4 + j) * w + x * 4 + k) * 4,
                          pixels + (j * 4 + k) * 4, 4);
      }
    return img;
  }
  int base = int(format);
  if (linear) {
    switch (format) {
    case 0x10:
      base = 2;
      break;
    case 0x11:
      base = 5;
      break;
    case 0x12:
      base = 6;
      break;
    case 0x13:
      base = 0;
      break;
    case 0x1c:
      base = 3;
      break;
    case 0x1d:
      base = 4;
      break;
    case 0x1e:
      base = 7;
      break;
    case 0x1f:
      base = 0x19;
      break;
    case 0x20:
      base = 0x1a;
      break;
    }
  }
  int bytes = (base == 6 || base == 7)                   ? 4
              : (base == 0 || base == 1 || base == 0x19) ? 1
                                                         : 2;
  if (base != 0 && base != 1 && base != 2 && base != 3 && base != 4 &&
      base != 5 && base != 6 && base != 7 && base != 0x19 && base != 0x1a)
    throw std::runtime_error("Unsupported Xbox texture format: " +
                             std::to_string(format));
  for (uint32_t y = 0; y < h; ++y)
    for (uint32_t x = 0; x < w; ++x) {
      size_t at = start + (linear ? size_t(y) * pitch + x * bytes
                                  : morton(x, y, w, h) * bytes);
      range(b, at, bytes);
      auto *p = img.rgba.data() + (size_t(y) * w + x) * 4;
      p[3] = 255;
      if (base == 6 || base == 7) {
        p[0] = b[at + 2];
        p[1] = b[at + 1];
        p[2] = b[at];
        p[3] = base == 6 ? b[at + 3] : 255;
      } else if (base == 0 || base == 1) {
        p[0] = p[1] = p[2] = b[at];
        if (base == 1)
          p[3] = b[at];
      } else if (base == 0x19) {
        p[0] = p[1] = p[2] = 255;
        p[3] = b[at];
      } else if (base == 0x1a) {
        p[0] = p[1] = p[2] = b[at];
        p[3] = b[at + 1];
      } else {
        auto v = value<uint16_t>(b, at);
        if (base == 5) {
          auto c = rgb565(v);
          std::memcpy(p, c.data(), 4);
        } else if (base == 4) {
          p[0] = uint8_t(((v >> 8) & 15) * 17);
          p[1] = uint8_t(((v >> 4) & 15) * 17);
          p[2] = uint8_t((v & 15) * 17);
          p[3] = uint8_t((v >> 12) * 17);
        } else {
          p[0] = uint8_t(((v >> 10) & 31) * 255 / 31);
          p[1] = uint8_t(((v >> 5) & 31) * 255 / 31);
          p[2] = uint8_t((v & 31) * 255 / 31);
          p[3] = base == 3 ? 255 : ((v & 0x8000) ? 255 : 0);
        }
      }
    }
  return img;
}
void Model::bounds() {
  auto previous_min = minimum, previous_max = maximum;
  minimum.fill(std::numeric_limits<float>::max());
  maximum.fill(-std::numeric_limits<float>::max());
  triangles = 0;
  for (auto &part : parts) {
    if (part.role == MeshRole::ParticleEmitter ||
        part.role == MeshRole::DebugFloor)
      continue;
    triangles += part.vertices.size() / 3;
    if (!part.visible || part.sky)
      continue;
    for (auto &v : part.vertices) {
      float p[] = {v.x, v.y, v.z};
      for (int k = 0; k < 3; ++k) {
        minimum[k] = std::min(minimum[k], p[k]);
        maximum[k] = std::max(maximum[k], p[k]);
      }
    }
  }
  if (!triangles)
    throw std::runtime_error("No supported triangle geometry in this asset");
  if (minimum[0] > maximum[0]) {
    minimum = previous_min;
    maximum = previous_max;
  }
}
Model decode_model(std::span<const uint8_t> b, bool stage) {
  if (!tag(b, "2EMP"))
    throw std::runtime_error("Unsupported mesh container");
  range(b, 0, 60);
  struct Material {
    std::string name;
    int texture;
    uint32_t flags = 3;
    bool source = false;
    MaterialChannel diffuse, reflection, gloss;
  };
  std::vector<Material> materials;
  size_t at = 60;
  std::set<size_t> seen;
  for (uint32_t i = 0; at; ++i) {
    if (i > 4096 || !seen.insert(at).second)
      throw std::runtime_error("Invalid material chain");
    range(b, at, 128);
    if (u32(b, at + 4) != 22)
      throw std::runtime_error("Invalid material record");
    Material material{label(b, at + 64, 60), int32_t(u32(b, at + 16))};
    if (u32(b, at + 8) == 3) {
      material.source = true;
      material.flags = u32(b, at + 32);
      material.diffuse.texture = material.texture;
      for (size_t k = 0; k < 2; ++k) {
        material.diffuse.offset[k] = f32(b, at + 36 + k * 4);
        material.diffuse.scale[k] = f32(b, at + 44 + k * 4);
      }
      auto extra = u32(b, at + 60);
      if (extra) {
        range(b, extra, 56);
        // Older material extensions use a different layout. Only the v2
        // channel table has pointers at these offsets.
        if (u32(b, extra + 4) == 32 && u32(b, extra + 8) == 2) {
          auto channel = [&](size_t slot, MaterialChannel &out) {
            auto p = u32(b, extra + slot * 4);
            if (!p)
              return;
            range(b, p, 48);
            out.texture = int32_t(u32(b, p + 12));
            for (size_t k = 0; k < 2; ++k) {
              out.offset[k] = f32(b, p + 32 + k * 4);
              out.scale[k] = f32(b, p + 40 + k * 4);
            }
          };
          channel(5, material.diffuse);
          channel(8, material.gloss);
          channel(13, material.reflection);
        }
      }
      material.texture = material.diffuse.texture;
    }
    materials.push_back(std::move(material));
    at = u32(b, at);
    if (!at)
      break;
  }
  Model out;
  out.stage = stage;
  for (auto &m : materials)
    if (!m.name.empty() && m.name.find("Material #") != 0) {
      out.name = m.name;
      break;
    }
  at = u32(b, 24);
  seen.clear();
  std::map<uint32_t, size_t> skin_sizes;
  bool has_skin = false;
  bool winding_flags = false;
  bool sky_origin_set = false;
  for (size_t node = at; node;) {
    range(b, node, 8);
    if (!seen.insert(node).second)
      throw std::runtime_error("Invalid chunk chain");
    auto next = u32(b, node), type = u32(b, node + 4);
    if (type == 11) {
      range(b, node, 52);
      skin_sizes[u32(b, node + 40)] += u32(b, node + 16);
      has_skin = true;
    }
    node = next;
  }
  uint32_t main_skin = 0;
  size_t largest = 0;
  for (auto [id, size] : skin_sizes)
    if (size > largest) {
      largest = size;
      main_skin = id;
    }
  if (main_skin) {
    auto count = u32(b, main_skin + 8), bone = u32(b, main_skin + 16);
    if (count > 512)
      throw std::runtime_error("Invalid skeleton size");
    for (uint32_t i = 0; i < count; ++i) {
      range(b, bone, 76);
      if (u32(b, bone + 4) != 10)
        throw std::runtime_error("Invalid bone record");
      Bone joint;
      joint.parent = int32_t(u32(b, bone + 8));
      joint.name = label(b, bone + 12, 32);
      if (joint.parent >= int(i) || joint.parent < -1)
        throw std::runtime_error("Invalid bone parent");
      for (int k = 0; k < 3; ++k) {
        joint.bind[k] = f32(b, bone + 44 + k * 4);
        joint.rotation[k] = f32(b, bone + 56 + k * 4);
      }
      out.bones.push_back(std::move(joint));
      bone = u32(b, bone);
    }
  }
  std::array<std::vector<float>, 3> origins;
  std::array<float, 3> body_min, body_max;
  body_min.fill(std::numeric_limits<float>::max());
  body_max.fill(-std::numeric_limits<float>::max());
  seen.clear();
  size_t ordinal = 0;
  while (at) {
    if (!seen.insert(at).second || seen.size() > 20000)
      throw std::runtime_error("Invalid mesh chain");
    range(b, at, 8);
    auto next = u32(b, at), type = u32(b, at + 4);
    if (stage && type == 21) {
      range(b, at, 96);
      StageLight light;
      light.type = u32(b, at + 8);
      light.id = u32(b, at + 12);
      for (size_t k = 0; k < 3; ++k) {
        light.position[k] = f32(b, at + 16 + k * 4);
        light.direction[k] = f32(b, at + 40 + k * 4) - light.position[k];
        light.color[k] = std::min(1.f, b[at + 52 + k] / 170.f);
      }
      light.position[1] = -light.position[1];
      light.direction[1] = -light.direction[1];
      if (light.type == 4)
        for (auto &axis : light.direction)
          axis = -axis;
      light.near_range = f32(b, at + 80);
      light.far_range = f32(b, at + 84);
      if (light.near_range == -1) {
        light.near_range = 1000000;
        light.far_range = 1000001;
      } else if (light.near_range == light.far_range) {
        light.near_range *= .5f;
      } else if (light.far_range < light.near_range) {
        std::swap(light.near_range, light.far_range);
      }
      if (light.type == 2) {
        light.inner_angle = f32(b, at + 64) * .00872664626f;
        light.outer_angle = f32(b, at + 68) * .00872664626f;
      }
      uint64_t render = 0, flags = 0;
      mesh_properties(b, u32(b, at + 92), render, flags);
      light.enabled = !(flags & 0x1000);
      light.affects_meshes = !(flags & 0x800);
      out.lights.push_back(light);
    }
    if (type != 3 && type != 11) {
      at = next;
      continue;
    }
    range(b, at, type == 11 ? 52 : 124);
    auto format = u32(b, at + 8), count = u32(b, at + 16),
         vb = u32(b, at + (type == 11 ? 44 : 64)),
         ib = u32(b, at + (type == 11 ? 48 : 68));
    if ((type == 3 && format != 0 && format != 1 && format != 6) ||
        (type == 11 && format != 2)) {
      ++out.unsupported;
      at = next;
      continue;
    }
    uint32_t stride = type == 11    ? 64
                      : format == 0 ? 44
                      : format == 1 ? 28
                                    : 16;
    if (count > 1000000)
      throw std::runtime_error("Invalid vertex count");
    range(b, vb, size_t(count) * stride);
    range(b, ib, 20);
    if (u32(b, ib + 4) != 16)
      throw std::runtime_error("Invalid index record");
    auto indices = u32(b, ib + 12), data = u32(b, ib + 16);
    if (indices > 6000000)
      throw std::runtime_error("Invalid index count");
    range(b, data, size_t(indices) * 2);
    uint64_t render_flags = 0, object_flags = 0;
    auto properties = type == 3
                          ? mesh_properties(b, ib, render_flags, object_flags)
                          : std::map<std::string, std::string>{};
    struct V {
      Vertex v;
      uint32_t material;
      Influence skin;
    };
    std::vector<V> vertices;
    vertices.reserve(count);
    auto rotation = DirectX::XMMatrixIdentity();
    if (stage && type == 3)
      rotation = DirectX::XMMatrixRotationZ(f32(b, at + 44)) *
                 DirectX::XMMatrixRotationY(f32(b, at + 40)) *
                 DirectX::XMMatrixRotationX(f32(b, at + 36));
    for (uint32_t i = 0; i < count; ++i) {
      size_t p = vb + size_t(i) * stride;
      V v{};
      if (type == 11) {
        // The leading position preserves model-space bind geometry; the four
        // additional bone-local positions belong to the animated skin path.
        v.v.x = value<int16_t>(b, p);
        v.v.y = -float(value<int16_t>(b, p + 2));
        v.v.z = value<int16_t>(b, p + 4);
        v.v.nx = value<int16_t>(b, p + 6) / 4096.f;
        v.v.ny = -value<int16_t>(b, p + 8) / 4096.f;
        v.v.nz = value<int16_t>(b, p + 10) / 4096.f;
        v.material = value<uint16_t>(b, p + 12);
        v.v.u = value<int16_t>(b, p + 14) / 4096.f;
        v.v.v = value<int16_t>(b, p + 16) / 4096.f;
        v.v.color = u32(b, p + 54);
        for (int k = 0; k < 4; ++k) {
          v.skin.bone[k] = b[p + 26 + k];
          v.skin.weight[k] = value<int16_t>(b, p + 18 + k * 2) / 4096.f;
          for (int j = 0; j < 3; ++j)
            v.skin.local[k][j] =
                float(value<int16_t>(b, p + 30 + k * 6 + j * 2));
        }
        if (u32(b, at + 40) == main_skin) {
          float body[] = {v.v.x, v.v.y, v.v.z};
          for (int j = 0; j < 3; ++j) {
            body_min[j] = std::min(body_min[j], body[j]);
            body_max[j] = std::max(body_max[j], body[j]);
          }
          for (int k = 0; k < 4; ++k)
            if (v.skin.weight[k] > 0 && v.skin.bone[k] < out.bones.size())
              out.bones[v.skin.bone[k]].used = true;
          if (v.skin.weight[0] == 1.f && v.skin.bone[0] < out.bones.size())
            for (int j = 0; j < 3; ++j)
              origins[j].push_back(float(value<int16_t>(b, p + j * 2)) -
                                   v.skin.local[0][j] -
                                   out.bones[v.skin.bone[0]].bind[j]);
        }
      } else if (format == 0) {
        v.v.x = f32(b, p);
        v.v.y = f32(b, p + 4);
        v.v.z = f32(b, p + 8);
        v.v.nx = f32(b, p + 12);
        v.v.ny = f32(b, p + 16);
        v.v.nz = f32(b, p + 20);
        v.material = u32(b, p + 24);
        v.v.u = f32(b, p + 28);
        v.v.v = f32(b, p + 32);
        v.v.color = u32(b, p + 36);
      } else {
        v.v.x = value<int16_t>(b, p);
        v.v.y = value<int16_t>(b, p + 2);
        v.v.z = value<int16_t>(b, p + 4);
        if (format == 1) {
          v.v.nx = value<int16_t>(b, p + 6) / 4096.f;
          v.v.ny = value<int16_t>(b, p + 8) / 4096.f;
          v.v.nz = value<int16_t>(b, p + 10) / 4096.f;
        }
        size_t m = format == 1 ? 12 : 6;
        v.material = value<uint16_t>(b, p + m);
        v.v.u = value<int16_t>(b, p + m + 2) / 4096.f;
        v.v.v = value<int16_t>(b, p + m + 4) / 4096.f;
        v.v.color = u32(b, p + m + 6);
      }
      if (type == 3) {
        if (stage) {
          using namespace DirectX;
          XMFLOAT3 transformed;
          XMStoreFloat3(&transformed,
                        XMVector3TransformCoord(
                            XMVectorSet(v.v.x, v.v.y, v.v.z, 1), rotation));
          v.v.x = transformed.x;
          v.v.y = transformed.y;
          v.v.z = transformed.z;
          XMStoreFloat3(&transformed,
                        XMVector3TransformNormal(
                            XMVectorSet(v.v.nx, v.v.ny, v.v.nz, 0), rotation));
          v.v.nx = transformed.x;
          v.v.ny = transformed.y;
          v.v.nz = transformed.z;
        }
        v.v.x += f32(b, at + 24);
        v.v.y += f32(b, at + 28);
        v.v.z += f32(b, at + 32);
        int parent = int32_t(u32(b, at + 20));
        if (has_skin && parent >= 0 && size_t(parent) < out.bones.size()) {
          v.skin.bone[0] = uint16_t(parent);
          v.skin.weight[0] = 1;
          float p[] = {v.v.x, v.v.y, v.v.z};
          for (int j = 0; j < 3; ++j)
            v.skin.local[0][j] = p[j] - out.bones[parent].bind[j];
          out.bones[parent].used = true;
        }
        v.v.y = -v.v.y;
        v.v.ny = -v.v.ny;
      }
      vertices.push_back(v);
    }
    std::vector<Part> batches(materials.size() + 1);
    std::array<uint32_t, 2> prior{};
    size_t run = 0;
    bool reverse = false;
    for (uint32_t i = 0; i < indices; ++i) {
      uint16_t raw = value<uint16_t>(b, data + size_t(i) * 2);
      winding_flags |= (raw & 0x4000) != 0;
      uint32_t index = raw & 0x3fff;
      // A strip's first index carries its restart and winding flags. The
      // winding bit reverses the whole strip, including alternating triangles.
      if (raw & 0x8000) {
        run = 0;
        reverse = (raw & 0x4000) != 0;
      }
      if (index >= count)
        throw std::runtime_error("Index outside vertex buffer");
      if (run >= 2 && prior[0] != prior[1] && prior[0] != index &&
          prior[1] != index) {
        auto a = prior[0], c = prior[1];
        if (bool(run & 1) != reverse)
          std::swap(a, c);
        auto mat = vertices[index].material;
        size_t slot = std::min<size_t>(mat, materials.size());
        auto &part = batches[slot];
        if (part.name.empty()) {
          part.mesh_id = uint32_t(ordinal);
          part.render_flags = render_flags;
          part.object_flags = object_flags;
          if (type == 3) {
            part.object = u32(b, at + 80);
            for (size_t k = 0; k < 3; ++k) {
              part.scene_position[k] = f32(b, at + 24 + k * 4);
              part.scene_rotation[k] = stage ? f32(b, at + 36 + k * 4) : 0;
            }
            part.uv_speed = {scroll_rate(properties, "USCROLLS"),
                             scroll_rate(properties, "VSCROLLS")};
            if (auto group = properties.find("ANIMGROUP");
                group != properties.end())
              part.scene_group =
                  group->second.substr(0, group->second.find(']'));
          }
          part.name = "Part " + std::to_string(ordinal + 1) + " / " +
                      (slot < materials.size() && !materials[slot].name.empty()
                           ? materials[slot].name
                           : "material " + std::to_string(mat));
          part.texture = slot < materials.size() ? materials[slot].texture : -1;
          if (slot < materials.size()) {
            const auto &material = materials[slot];
            part.diffuse = material.diffuse;
            part.reflection = material.reflection;
            part.gloss = material.gloss;
            part.material_flags = material.flags;
            part.source_material = material.source;
          }
          // Compact scenery carries baked illumination in its vertex colors;
          // generated inspection normals are not a request to relight it.
          part.prelit = stage && type == 3 && format == 6;
          if (stage && (properties.contains("FLICKERMIN") ||
                        properties.contains("FLICKERMAX"))) {
            part.flicker = true;
            part.flicker_settings = {
                property_number(properties, "FLICKERMIN"),
                property_number(properties, "FLICKERMAX", 100),
                property_number(properties, "FLICKERSPEED", 1)};
            const auto &f = part.flicker_settings;
            part.opacity =
                std::clamp((f[0] + (f[1] - f[0]) * .64f) / 128, 0.f, 1.f);
          }
          part.visible = !has_skin ||
                         (type == 11 && u32(b, at + 40) == main_skin) ||
                         (type == 3 && int32_t(u32(b, at + 20)) >= 0);
          part.opaque = has_skin;
          if (!has_skin) {
            // The original renderer collects flag 0x10 into its sky pass.
            part.sky = stage && (render_flags & 0x10) != 0;
            if (part.sky && !sky_origin_set) {
              out.sky_origin = {part.scene_position[0], -part.scene_position[1],
                                part.scene_position[2]};
              sky_origin_set = true;
            }
            if (render_flags & 0x40000)
              part.blend = BlendMode::Additive;
            part.emissive = part.blend == BlendMode::Additive;
            if (stage && slot < materials.size()) {
              std::string name = materials[slot].name;
              std::transform(
                  name.begin(), name.end(), name.begin(),
                  [](unsigned char c) { return char(std::tolower(c)); });
              // The eye masks use ordinary alpha blending with depth writes
              // disabled; their separate glow layer must stay unlit.
              part.emissive |= (render_flags & 0x80000) &&
                               (name.find("statueeyes") != std::string::npos ||
                                name.find("glow") != std::string::npos);
            }
            if (stage && (render_flags & 0x1000)) {
              part.role = MeshRole::ShadowReceiver;
              part.blend = BlendMode::Multiply;
            }
            part.depth_write = (render_flags & 0x80000) == 0;
            if (part.blend == BlendMode::Multiply)
              part.depth_write = false;
            if (part.blend != BlendMode::Alpha || !part.depth_write)
              part.alpha_cutoff = 0;
          }
          if (stage && slot < materials.size()) {
            std::string name = materials[slot].name;
            std::transform(
                name.begin(), name.end(), name.begin(),
                [](unsigned char c) { return char(std::tolower(c)); });
            // Collision-only/hidden objects can reuse a visible wall material.
            // Preserve them for helper inspection instead of obscuring the map.
            part.helper =
                (object_flags & 0x800) != 0 || name.starts_with("temptex") ||
                (!part.sky && (name.find("sky") != std::string::npos ||
                               name.find("cloud") != std::string::npos)) ||
                (name.empty() && materials[slot].texture == 0 && count <= 24);
            part.visible = !part.helper;
            if (part.sky)
              part.role = MeshRole::Sky;
            else if (properties.contains("CAMERAID")) {
              part.role = MeshRole::Camera;
              part.helper = true;
              part.visible = false;
            } else if (part.helper)
              part.role = MeshRole::Hidden;
          }
        }
        for (auto k : {a, c, index}) {
          part.vertices.push_back(vertices[k].v);
          if (type == 11 ||
              (type == 3 && has_skin && vertices[k].skin.weight[0] > 0))
            part.skin.push_back(vertices[k].skin);
        }
      }
      prior = {prior[1], index};
      ++run;
    }
    for (auto &part : batches)
      if (!part.vertices.empty()) {
        part.bind = part.vertices;
        out.parts.push_back(std::move(part));
      }
    ++ordinal;
    at = next;
  }
  // Older extra maps omit the strip direction bits used by the campaign
  // export. Repair connected surface orientation only for that encoding.
  out.legacy_winding =
      stage && !winding_flags && u32(b, 28) >= 32 && u32(b, 28) <= 33;
  if (out.legacy_winding)
    out.repaired_triangles = out.repair_winding();
  for (int k = 0; k < 3; ++k)
    if (!origins[k].empty()) {
      auto &v = origins[k];
      auto middle = v.begin() + v.size() / 2;
      std::nth_element(v.begin(), middle, v.end());
      out.origin[k] = *middle;
    }
  // Some detailed rigs split the torso, head, and each limb into separate
  // skin containers. Their largest container alone does not bound the body.
  const std::set<std::string> anatomy = {
      "pelvis",    "abdomen",    "chest",      "neck",       "head",
      "head_end",  "l_bicep",    "l_calf",     "l_clavicle", "l_foot",
      "l_forearm", "l_hand",     "l_shoulder", "l_thigh",    "l_toe",
      "r_bicep",   "r_calf",     "r_clavicle", "r_foot",     "r_forearm",
      "r_hand",    "r_shoulder", "r_thigh",    "r_toe"};
  size_t anatomical = 0;
  std::array<float, 3> rig_min, rig_max;
  rig_min.fill(std::numeric_limits<float>::max());
  rig_max.fill(-std::numeric_limits<float>::max());
  for (auto &bone : out.bones)
    if (anatomy.contains(bone.name)) {
      ++anatomical;
      for (int k = 0; k < 3; ++k) {
        float p = (bone.bind[k] + out.origin[k]) * (k == 1 ? -1.f : 1.f);
        rig_min[k] = std::min(rig_min[k], p);
        rig_max[k] = std::max(rig_max[k], p);
      }
    }
  if (anatomical >= 4) {
    body_min = rig_min;
    body_max = rig_max;
  }
  for (auto &part : out.parts)
    if (part.skin.size() == part.vertices.size()) {
      bool nearby = true;
      for (size_t i = 0; i < part.skin.size(); ++i) {
        auto &skin = part.skin[i];
        float xyz[3]{}, sum = 0;
        for (int k = 0; k < 4; ++k)
          if (skin.weight[k] > 0 && skin.bone[k] < out.bones.size()) {
            sum += skin.weight[k];
            for (int j = 0; j < 3; ++j)
              xyz[j] += (skin.local[k][j] + out.bones[skin.bone[k]].bind[j]) *
                        skin.weight[k];
          }
        if (sum > 0) {
          for (int j = 0; j < 3; ++j) {
            xyz[j] = (xyz[j] / sum + out.origin[j]) * (j == 1 ? -1.f : 1.f);
            float margin = std::max((body_max[j] - body_min[j]) * .3f,
                                    (body_max[1] - body_min[1]) * .2f);
            nearby &= xyz[j] >= body_min[j] - margin &&
                      xyz[j] <= body_max[j] + margin;
          }
          part.bind[i].x = xyz[0];
          part.bind[i].y = xyz[1];
          part.bind[i].z = xyz[2];
        }
      }
      part.visible = part.visible || nearby;
    }
  // Wound caps share the intact body's skeleton and bounds, so proximity
  // cannot distinguish them. They belong to dismemberment states, not the
  // intact animation preview. Keep their geometry available in the hierarchy.
  if (has_skin && !stage)
    for (auto &part : out.parts) {
      auto name = part.name;
      std::transform(name.begin(), name.end(), name.begin(),
                     [](unsigned char c) { return char(std::tolower(c)); });
      if (name.ends_with("_cap"))
        part.visible = false;
    }
  for (auto &bone : out.bones)
    bone.used = false;
  for (auto &part : out.parts)
    if (part.visible)
      for (auto &skin : part.skin)
        for (int k = 0; k < 4; ++k)
          if (skin.weight[k] > 0 && skin.bone[k] < out.bones.size())
            out.bones[skin.bone[k]].used = true;
  for (size_t i = out.bones.size(); i-- > 0;)
    if (out.bones[i].used && out.bones[i].parent >= 0)
      out.bones[out.bones[i].parent].used = true;
  out.pose(-1, 0);
  out.prepare_normals();
  if (stage && std::none_of(out.parts.begin(), out.parts.end(),
                            [](auto &part) { return part.visible; }))
    for (auto &part : out.parts)
      part.visible = true;
  out.bounds();
  out.grid_origin = {(out.minimum[0] + out.maximum[0]) * .5f, out.minimum[1],
                     (out.minimum[2] + out.maximum[2]) * .5f};
  out.grid_radius = std::max({out.maximum[0] - out.minimum[0],
                              out.maximum[1] - out.minimum[1],
                              out.maximum[2] - out.minimum[2], .01f}) *
                    .5f;
  for (const auto &bone : out.bones)
    if (bone.parent < 0 && bone.used) {
      out.grid_origin[0] = bone.bind[0] + out.origin[0];
      out.grid_origin[2] = bone.bind[2] + out.origin[2];
      break;
    }
  return out;
}
Source::Source(const std::filesystem::path &input) {
  auto path = std::filesystem::absolute(input);
  std::error_code ec;
  if (std::filesystem::is_directory(path, ec))
    folder_ = path;
  else {
    auto ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    if (ext == L".iso" || ext == L".xiso") {
      disc_ = std::make_unique<kernel::Disc>();
      std::string error;
      if (!disc_->Open(path, error))
        throw std::runtime_error("Cannot open Xbox disc: " + error);
    } else if (ext == L".wad" || ext == L".xbe")
      folder_ = path.parent_path();
    else
      throw std::runtime_error(
          "Select an Xbox ISO/XISO, GAMEDATA.WAD, or default.xbe");
  }
  auto head = read(0, 32);
  if (!tag(head, "PWF ") || u32(head, 8) != 2)
    throw std::runtime_error("Unsupported game archive profile");
  span_ = u32(head, 16);
  auto count = u32(head, 12);
  if (span_ < 2048 || span_ > 0x40000000 || count > 100000 || !count)
    throw std::runtime_error("Invalid game archive header");
  auto table = read(32, count * 8);
  for (uint32_t i = 0; i < count; ++i) {
    auto q = value<uint64_t>(table, size_t(i) * 8);
    Record r{uint32_t(q), uint32_t(q >> 32)};
    if (q >> 63) {
      r.offset = (q & 0x3fffff) * 2048;
      r.size = uint32_t((q >> 24) & 0xfffff) * 4;
    }
    if (r.size > limit || r.offset / span_ > 7)
      throw std::runtime_error("Invalid archive entry");
    records_.push_back(r);
  }
  if (disc_) {
    auto *xbe = disc_->Find("default.xbe");
    if (xbe && !xbe->dir && xbe->size < 32 * 1024 * 1024) {
      executable_.resize(xbe->size);
      if (disc_->Read(*xbe, 0, executable_.data(), executable_.size()) !=
          executable_.size())
        executable_.clear();
    }
  } else if (std::filesystem::exists(folder_ / "default.xbe")) {
    auto size = std::filesystem::file_size(folder_ / "default.xbe");
    if (size < 32 * 1024 * 1024)
      executable_ = file_read(folder_ / "default.xbe", 0, uint32_t(size));
  }
}
Bytes Source::read(uint64_t at, uint32_t n) const {
  uint64_t bank = span_ ? at / span_ : 0, local = span_ ? at % span_ : at;
  if (bank > 7)
    throw std::runtime_error("Invalid archive bank");
  std::string name = "GAMEDATA.WA";
  name += char('D' + bank);
  if (disc_) {
    auto *file = disc_->Find(name);
    if (!file || file->dir || local > file->size || n > file->size - local)
      throw std::runtime_error("Missing or incomplete " + name);
    Bytes out(n);
    if (n && disc_->Read(*file, local, out.data(), n) != n)
      throw std::runtime_error("Cannot read " + name);
    return out;
  }
  return file_read(folder_ / name, local, n);
}
Bytes Source::load(uint32_t id, std::string *name) const {
  if (id >= records_.size())
    throw std::runtime_error("Invalid resource ID");
  auto r = records_[id];
  return unpack(read(r.offset, r.size), name);
}
MusicTrack Source::stage_music(const Asset &asset) const {
  if (asset.kind != Kind::Stage || asset.id < 913 || asset.id > 6013 ||
      (asset.id - 913) % 20 || asset.id >= records_.size())
    return {};
  if (!music_scanned_) {
    std::map<std::string, MusicCue> groups;
    std::map<uint32_t, std::string> maps;
    std::map<uint32_t, MusicCue> cues;
    std::map<std::string, Record> files;
    // Maps in one area share a music bank. Pair definitions with their own
    // MAP_*_AHX sound-bank names rather than borrowing the previous map's BGM.
    for (uint32_t id = 913; id <= 6013 && id < records_.size(); id += 20) {
      auto group = music_group(load(id - 9));
      auto cue = decode_music_cue(load(id - 7));
      if (!cue.name.empty()) {
        cues[id] = cue;
        if (!group.empty())
          groups[group] = cue;
      }
      if (!group.empty())
        maps[id] = std::move(group);
    }
    for (const auto &[id, group] : maps)
      if (!cues.contains(id) && groups.contains(group))
        cues[id] = groups.at(group);
    // Only index AFS directories here. Read the selected ADX directly from the
    // archive later, without decoding dialogue/effects or extracting game
    // files.
    for (auto record : records_) {
      if (record.size < 8)
        continue;
      auto header = read(record.offset, 8);
      if (!tag(header, "AFS\0"))
        continue;
      auto count = u32(header, 4);
      if (!count || count > 4096 || uint64_t(count) * 8 + 16 > record.size)
        throw std::runtime_error("Invalid music archive directory");
      auto toc = read(record.offset + 8, count * 8 + 8);
      auto names = u32(toc, count * 8), size = u32(toc, count * 8 + 4);
      if (names > record.size || size > record.size - names ||
          size < count * 48)
        throw std::runtime_error("Invalid music archive names");
      auto directory = read(record.offset + names, count * 48);
      for (uint32_t i = 0; i < count; ++i) {
        auto name = label(directory, i * 48, 32);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if ((!name.starts_with("mus_") && !name.starts_with("music_")) ||
            !name.ends_with(".adx"))
          continue;
        auto offset = u32(toc, i * 8), length = u32(toc, i * 8 + 4);
        if (offset > record.size || length > record.size - offset)
          throw std::runtime_error("Invalid music archive entry");
        files.try_emplace(name, Record{record.offset + offset, length});
      }
    }
    stage_music_ = std::move(cues);
    music_files_ = std::move(files);
    music_scanned_ = true;
  }
  auto cue = stage_music_.find(asset.id);
  if (cue == stage_music_.end())
    return {};
  auto file = music_files_.find(cue->second.name);
  if (file == music_files_.end())
    throw std::runtime_error(
        "Stage music track is missing from the game source");
  auto track = decode_adx(read(file->second.offset, file->second.size));
  track.name = cue->second.name;
  track.volume = cue->second.volume;
  return track;
}
std::vector<Asset> Source::catalog(const std::atomic_bool *cancel) const {
  std::vector<Asset> out;
  std::map<uint32_t, std::string> stage_names;
  // The retail executable pairs its map IDs with stage selection labels.
  // Read those labels from the user's executable, including region variants.
  constexpr std::string_view marker = "This is map ";
  for (size_t at = 0; at + marker.size() < executable_.size(); ++at) {
    if (std::memcmp(executable_.data() + at, marker.data(), marker.size()))
      continue;
    size_t p = at + marker.size();
    uint32_t id = 0;
    while (p < executable_.size() && executable_[p] >= '0' &&
           executable_[p] <= '9' && id <= 256)
      id = id * 10 + executable_[p++] - '0';
    if (!id || id > 256 || p >= executable_.size() || executable_[p])
      continue;
    size_t end = std::min(p + 8, executable_.size());
    while (p < end && !executable_[p])
      ++p;
    if (p + 64 <= executable_.size()) {
      auto name = label(executable_, p, 64);
      if (!name.empty())
        stage_names[id] = std::move(name);
    }
  }
  std::map<uint32_t, std::string> skeleton_keys;
  struct Profile {
    uint32_t bank;
    std::string name;
  };
  std::map<std::string, Profile> profiles;
  for (uint32_t i = 0; i < records_.size(); ++i) {
    if (cancel && cancel->load())
      return {};
    if (!records_[i].size)
      continue;
    std::string name;
    auto b = load(i, &name);
    if (tag(b, "2EMP")) {
      auto p = u32(b, 24);
      range(b, p, 12);
      auto type = u32(b, p + 4), fmt = u32(b, p + 8);
      if (type == 0)
        continue;
      auto kind = type == 5 || fmt == 1 ? Kind::Character
                  : fmt == 6            ? Kind::Environment
                                        : Kind::Prop;
      // The supported Xbox archive has 256 map slots of 20 resources each;
      // the geometry occupies slot 9 after the shared resource block.
      uint32_t stage_id =
          i >= 913 && i <= 6013 && (i - 913) % 20 == 0 ? (i - 893) / 20 : 0;
      if (stage_id && type == 3)
        kind = Kind::Stage;
      auto first = label(b, 0x7c, 60);
      if (!first.empty() && first.find("Material #") != 0)
        name = first;
      std::string prefix = kind == Kind::Character     ? "Character"
                           : kind == Kind::Environment ? "Environment"
                                                       : "Prop";
      if (name.empty() || name == "PS2.bin" || name == "P_PS2.bin")
        name = prefix;
      if (kind == Kind::Stage) {
        auto it = stage_names.find(stage_id);
        name = it == stage_names.end() ? "Stage " + std::to_string(stage_id)
                                       : it->second;
        auto map = name.find(" - Map ");
        if (map != std::string::npos) {
          auto sound = load(i - 9);
          std::string text(sound.begin(), sound.end());
          const std::pair<const char *, const char *> areas[] = {
              {"GOROS_LAIR", "Goro's Lair"}, {"SOULTOMBS", "Soul Tombs"},
              {"WU_SHI", "Wu Shi Academy"},  {"WASTELAND", "Wasteland"},
              {"FOUNDRY", "Foundry"},        {"FOREST", "Living Forest"},
              {"NETHER", "Netherrealm"},     {"EVILMON", "Evil Monastery"}};
          for (auto [key, area] : areas)
            if (text.find(std::string("MAP_") + key + "_AHX_") !=
                std::string::npos) {
              name = area + name.substr(map);
              break;
            }
        }
      }
      if (kind == Kind::Character && i + 4 < records_.size()) {
        auto sound = load(i + 4);
        std::string text(sound.begin(), sound.end());
        auto pos = text.find("CHAR_");
        if (pos != std::string::npos) {
          size_t end = text.find('_', pos + 5);
          if (end != std::string::npos && end - pos < 40) {
            auto character = text.substr(pos + 5, end - pos - 5);
            std::transform(
                character.begin(), character.end(), character.begin(),
                [](unsigned char c) { return char(std::tolower(c)); });
            const std::map<std::string, std::string> names = {
                {"liukang", "Liu Kang"},      {"kunglao", "Kung Lao"},
                {"subzero", "Sub-Zero"},      {"noobsaibot", "Noob Saibot"},
                {"shaokahn", "Shao Kahn"},    {"shangtsung", "Shang Tsung"},
                {"johnnycage", "Johnny Cage"}};
            if (auto it = names.find(character); it != names.end())
              name = it->second;
            else if (!character.empty()) {
              character[0] =
                  char(std::toupper(static_cast<unsigned char>(character[0])));
              name = character;
            }
          }
        }
      }
      Asset asset{i, kind, name + "  [" + std::to_string(i) + "]",
                  uint32_t(b.size())};
      asset.stage_id = kind == Kind::Stage ? stage_id : 0;
      size_t node = p;
      std::set<size_t> nodes;
      while (node && nodes.insert(node).second) {
        range(b, node, 8);
        if (u32(b, node + 4) == 5) {
          uint32_t count = u32(b, node + 8), bone = u32(b, node + 16);
          std::string key;
          if (count > 512)
            break;
          for (uint32_t j = 0; j < count; ++j) {
            key += label(b, bone + 12, 32);
            key += '|';
            bone = u32(b, bone);
          }
          skeleton_keys[i] = key;
          if (i + 3 < records_.size()) {
            auto bank = load(i + 3);
            if (bank.size() > 16 && u32(bank, 0) == 16 &&
                u32(bank, 4) < bank.size()) {
              asset.animation_id = i + 3;
              profiles[key] = {i + 3, name};
            }
          }
          break;
        }
        node = u32(b, node);
      }
      out.push_back(std::move(asset));
    } else if (tag(b, "XPR0")) {
      Asset asset{i, Kind::Texture,
                  (name.empty() ? "Texture" : name) + "  [" +
                      std::to_string(i) + "]",
                  uint32_t(b.size())};
      asset.art = concept_art_page(i);
      if (asset.art.group != ArtGroup::None) {
        asset.kind = Kind::ConceptArt;
        asset.name = std::string(art_group_name(asset.art.group)) + " " +
                     (asset.art.number < 10 ? "0" : "") +
                     std::to_string(asset.art.number) + "  [" +
                     std::to_string(i) + "]";
      }
      out.push_back(std::move(asset));
    }
  }
  for (auto &asset : out)
    if (asset.animation_id == UINT32_MAX) {
      auto key = skeleton_keys.find(asset.id);
      if (key != skeleton_keys.end()) {
        auto profile = profiles.find(key->second);
        if (profile != profiles.end()) {
          asset.animation_id = profile->second.bank;
          asset.shared_animation = true;
        } else {
          std::string name = asset.name;
          std::transform(name.begin(), name.end(), name.begin(),
                         [](unsigned char c) { return char(std::tolower(c)); });
          for (auto &[bones, candidate] : profiles) {
            auto title = candidate.name;
            std::transform(
                title.begin(), title.end(), title.begin(),
                [](unsigned char c) { return char(std::tolower(c)); });
            if (title.size() > 3 && title != "character" &&
                name.find(title) != std::string::npos) {
              asset.animation_id = candidate.bank;
              asset.shared_animation = true;
              break;
            }
          }
        }
      }
    }
  return out;
}
bool map_animation_bones(Model &out, std::span<const uint8_t> executable_,
                         uint32_t mesh) {
  std::vector<std::vector<std::string>> bone_tables;
  std::vector<std::string> character_table;
  // The supplied executable contains the character-specific animation bone
  // name tables. Read metadata from it instead of shipping game tables.
  if (tag(executable_, "XBEH")) {
    auto &x = executable_;
    auto base = u32(x, 0x104), sections = u32(x, 0x11c), table = u32(x, 0x120);
    if (sections > 256 || table < base)
      throw std::runtime_error("Invalid executable section table");
    auto address = [&](uint32_t va) -> size_t {
      for (uint32_t s = 0; s < sections; ++s) {
        size_t at = size_t(table - base) + s * 56;
        auto start = u32(x, at + 4), raw = u32(x, at + 12),
             size = u32(x, at + 16);
        if (va >= start && va - start < size &&
            size_t(raw) + va - start < x.size())
          return size_t(raw) + va - start;
      }
      return x.size();
    };
    // Retail actor IDs start at two; their resource rows carry the mesh
    // at +12. The setup routine indexes the animation-name registry by
    // actor ID minus two. This distinguishes identical facial/finger name
    // sets whose track orders differ (for example Shang Tsung).
    size_t registry_code = address(0xbf893);
    if (registry_code + 7 <= x.size() && x[registry_code] == 0x8d &&
        x[registry_code + 1] == 0x3c && x[registry_code + 2] == 0xbd) {
      uint32_t registry = u32(x, registry_code + 3);
      for (size_t at = 12; at + 4 <= x.size(); at += 4)
        if (u32(x, at) == mesh && (u32(x, at - 12) & 0xffff0000) == 0x20000 &&
            u32(x, at - 8) == 1) {
          uint32_t actor = u32(x, at - 12) & 0xffff;
          if (actor < 2 || actor > 128)
            continue;
          size_t entry = address(registry + (actor - 2) * 12);
          if (entry + 8 > x.size())
            continue;
          uint32_t count = u32(x, entry + 4);
          size_t names = address(u32(x, entry));
          if (!count || count > 512 || names + count * 4 > x.size())
            continue;
          for (uint32_t i = 0; i < count; ++i) {
            size_t p = address(u32(x, names + i * 4));
            if (p + 32 > x.size()) {
              character_table.clear();
              break;
            }
            character_table.push_back(label(x, p, 32));
          }
          if (!character_table.empty() && character_table[0] == "pelvis")
            break;
          character_table.clear();
        }
    }
    for (size_t at = 0; at + 4 <= x.size(); at += 4) {
      size_t first = address(u32(x, at));
      if (first + 7 > x.size() || std::memcmp(x.data() + first, "pelvis\0", 7))
        continue;
      std::vector<std::string> names;
      for (size_t j = 0; j < 512 && at + j * 4 + 4 <= x.size(); ++j) {
        size_t p = address(u32(x, at + j * 4));
        if (p >= x.size())
          break;
        std::string name;
        for (size_t k = 0; k < 32 && p + k < x.size() && x[p + k]; ++k) {
          char c = char(x[p + k]);
          if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            name.clear();
            break;
          }
          name += c;
        }
        if (name.empty() || (j && name == "pelvis"))
          break;
        names.push_back(name);
      }
      if (!names.empty())
        bone_tables.push_back(std::move(names));
    }
  }
  if (bone_tables.empty() && !tag(executable_, "XBEH"))
    throw std::runtime_error("Original default.xbe bone table is unavailable");
  bool index_mapping = false;
  for (auto &clip : out.clips) {
    std::fill(clip.bones.begin(), clip.bones.end(), -1);
    // Banks mix the common body tracks with longer character-specific
    // lists. A table selected for the longest clip shifts shorter clips.
    std::vector<std::string> best;
    int best_score = -100000;
    for (const auto &names : bone_tables) {
      if (names.size() < clip.bones.size())
        continue;
      int score = -int(names.size() - clip.bones.size()) * 8;
      for (size_t i = 0; i < clip.bones.size(); ++i) {
        bool match = std::any_of(out.bones.begin(), out.bones.end(),
                                 [&](auto &b) { return b.name == names[i]; });
        score += match ? 4 : -12;
      }
      if (score > best_score) {
        best = names;
        best_score = score;
      }
    }
    if (character_table.size() >= clip.bones.size())
      best = character_table;
    bool indexed = best.empty() || (clip.bones.size() > best.size() &&
                                    clip.bones.size() + 1 >= out.bones.size());
    index_mapping |= indexed;
    for (size_t i = 0; i < clip.bones.size(); ++i) {
      if (indexed) {
        if (i < out.bones.size())
          clip.bones[i] = int(i);
        continue;
      }
      if (i >= best.size())
        continue;
      auto it = std::find_if(out.bones.begin(), out.bones.end(),
                             [&](auto &b) { return b.name == best[i]; });
      if (it != out.bones.end())
        clip.bones[i] = int(it - out.bones.begin());
    }
  }
  return index_mapping;
}
Model Source::model(const Asset &asset) const {
  auto bytes = load(asset.id);
  auto out = decode_model(bytes, asset.kind == Kind::Stage);
  out.name = asset.name;
  if (asset.kind == Kind::Character) {
    out.controls = decode_character_controls(executable_);
    out.joint_dynamics = decode_joint_dynamics(executable_);
  }
  // Character archives precede their mesh; level and prop archives follow it.
  for (int delta : {1, -1}) {
    int64_t id = int64_t(asset.id) + delta;
    if (id < 0 || id >= int64_t(count()))
      continue;
    auto textures = load(uint32_t(id));
    if (tag(textures, "PWF ") && u32(textures, 12) == u32(bytes, 20)) {
      for (auto &data : members(textures)) {
        try {
          out.textures.push_back(decode_xpr(data));
        } catch (const std::exception &) {
          out.textures.push_back({});
        }
      }
      break;
    }
  }
  out.prepare_materials();
  if (!out.bones.empty()) {
    try {
      if (asset.animation_id == UINT32_MAX)
        throw std::runtime_error(
            "No matching original animation bank for this variant");
      auto bank = load(asset.animation_id);
      out.clips = decode_animations(bank);
      if (asset.animation_id >= 2) {
        try {
          out.action_graph = decode_action_graph(load(asset.animation_id - 2),
                                                 load(asset.animation_id - 1),
                                                 out.clips);
        } catch (const std::exception &) {
          out.action_graph = {};
        }
      }
      bool index_mapping = map_animation_bones(
          out, executable_,
          asset.shared_animation ? asset.animation_id - 3 : asset.id);
      out.animation_status =
          std::to_string(out.clips.size()) + " original motion clips";
      if (index_mapping)
        out.animation_status += " / includes bone index mapping (preview)";
      if (asset.shared_animation)
        out.animation_status +=
            " / shared bank " + std::to_string(asset.animation_id);
      if (asset.shared_animation &&
          std::all_of(out.clips.begin(), out.clips.end(),
                      [](const auto &c) { return c.frames <= 1; })) {
        auto common = common_motion(out);
        auto idle = std::find_if(common.begin(), common.end(),
                                 [](auto &c) { return c.name == "a00_stnd"; });
        if (idle != common.end()) {
          out.clips.insert(out.clips.begin(), std::move(*idle));
          out.animation_status += " / retargeted idle (static reference bank)";
        }
      }
    } catch (const std::exception &e) {
      out.clips.clear();
      out.animation_status = e.what();
    }
  }
  if (out.bones.empty()) {
    if (asset.kind == Kind::Stage && asset.id + 2 < count()) {
      try {
        out.atmosphere = decode_atmosphere(load(asset.id + 2));
      } catch (const std::exception &error) {
        out.atmosphere_status = error.what();
      }
    }
    if (asset.kind == Kind::Stage && asset.id + 7 < count()) {
      auto bank = load(asset.id + 7);
      if (!bank.empty()) {
        try {
          out.scenery = decode_scenery(bank);
        } catch (const std::exception &error) {
          out.scenery_status = error.what();
        }
      }
    }
    out.bind_scenery();
    out.animate_scenery(0, 0);
    if (asset.kind == Kind::Stage) {
      try {
        out.bind_particles(particle_library());
      } catch (const std::exception &error) {
        out.particle_status = error.what();
      }
    }
  }
  return out;
}
void save_png(const Image &image, const std::filesystem::path &path) {
  size_t size = 0;
  void *data = tdefl_write_image_to_png_file_in_memory(
      image.rgba.data(), int(image.width), int(image.height), 4, &size);
  if (!data)
    throw std::runtime_error("PNG encoding failed");
  std::unique_ptr<void, decltype(&mz_free)> owned(data, &mz_free);
  std::ofstream f(path, std::ios::binary);
  if (!f.write(static_cast<const char *>(data), size))
    throw std::runtime_error("Cannot write PNG");
}

} // namespace shaolin::viewer

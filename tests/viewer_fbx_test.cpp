#include "assets.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <variant>
using namespace shaolin::viewer;
namespace fs = std::filesystem;
namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
using Value = std::variant<int64_t, double, std::string, std::vector<double>,
                           std::vector<int32_t>>;
struct Node {
  std::string name;
  std::vector<Value> values;
  std::vector<Node> children;
  const Node &child(const char *name) const {
    for (const auto &n : children)
      if (n.name == name)
        return n;
    throw std::runtime_error("Missing FBX node");
  }
};
// Read the on-disk FBX tree without relying on the exporter's node classes.
struct Reader {
  Bytes data;
  size_t at = 27;
  template <class T> T read() {
    require(at <= data.size() && sizeof(T) <= data.size() - at,
            "Truncated FBX scalar");
    T value;
    std::memcpy(&value, data.data() + at, sizeof(value));
    at += sizeof(value);
    return value;
  }
  std::string string(size_t length) {
    require(at <= data.size() && length <= data.size() - at,
            "Truncated FBX string");
    std::string s(reinterpret_cast<const char *>(data.data() + at), length);
    at += length;
    return s;
  }
  template <class T> std::vector<T> array() {
    auto count = read<uint32_t>(), encoding = read<uint32_t>(),
         length = read<uint32_t>();
    require(encoding == 0 && uint64_t(count) * sizeof(T) == length,
            "Invalid FBX array framing");
    std::vector<T> out;
    for (uint32_t i = 0; i < count; ++i)
      out.push_back(read<T>());
    return out;
  }
  Node node() {
    auto end = read<uint32_t>(), count = read<uint32_t>(),
         length = read<uint32_t>();
    auto name_length = read<uint8_t>();
    Node n{string(name_length)};
    size_t begin = at;
    for (uint32_t i = 0; i < count; ++i) {
      switch (read<uint8_t>()) {
      case 'I':
        n.values.emplace_back(int64_t(read<int32_t>()));
        break;
      case 'L':
        n.values.emplace_back(read<int64_t>());
        break;
      case 'D':
        n.values.emplace_back(read<double>());
        break;
      case 'S':
        n.values.emplace_back(string(read<uint32_t>()));
        break;
      case 'd':
        n.values.emplace_back(array<double>());
        break;
      case 'i':
        n.values.emplace_back(array<int32_t>());
        break;
      default:
        throw std::runtime_error("Unsupported FBX property type");
      }
    }
    require(at - begin == length && at <= end && end <= data.size(),
            "Invalid FBX node lengths");
    while (at < end) {
      require(end - at >= 13, "Truncated FBX child");
      bool null = true;
      for (size_t i = 0; i < 13; ++i)
        null &= data[at + i] == 0;
      if (null) {
        at += 13;
        break;
      }
      n.children.push_back(node());
    }
    require(at == end, "FBX child offset does not match its parent");
    return n;
  }
  Node document(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    data.assign(std::istreambuf_iterator<char>(file), {});
    require(data.size() > 27 &&
                std::memcmp(data.data(), "Kaydara FBX Binary  \0\x1a\0", 23) ==
                    0,
            "Export is not a binary FBX");
    at = 23;
    require(read<uint32_t>() == 7400, "Unexpected FBX version");
    Node root{"Root"};
    while (at + 13 <= data.size()) {
      uint32_t end = 0;
      std::memcpy(&end, data.data() + at, sizeof(end));
      if (!end)
        break;
      root.children.push_back(node());
    }
    return root;
  }
};
struct Fixture {
  fs::path directory =
      fs::temp_directory_path() /
      ("shaolin-fbx-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::path path = directory / L"model \u00e9.fbx";
  Fixture() { fs::create_directory(directory); }
  ~Fixture() {
    std::error_code error;
    for (const auto &p : {path, directory / L"model \u00e9_texture_0.png",
                          directory / L"model \u00e9_texture_0_opaque.png"})
      fs::remove(p, error);
    fs::remove(directory, error);
  }
};
void checks() {
  Model m;
  Part part;
  part.name = "Original part / \"quoted\"";
  part.vertices = {{0, 0, 1, 0, 0, 0x80808080, 0, 0, 1},
                   {1, 0, 1, 1, 0, 0x40404040, 0, 0, 1},
                   {0, 1, 1, 0, 1, 0x00000000, 0, 0, 1}};
  part.bind = part.vertices;
  part.uv_offset = {.25f, .5f};
  part.diffuse.offset = {.1f, .2f};
  part.diffuse.scale = {2, 3};
  part.texture = 0;
  m.parts = {part, part, part, part, part};
  m.parts[1].visible = false;
  m.parts[2].opaque = true;
  m.parts[2].vertices[0].x = 7; // Export the current pose, not bind geometry.
  m.parts[3].role = MeshRole::ParticleEmitter;
  m.parts[4].role = MeshRole::DebugFloor;
  m.textures = {{1, 1, {100, 120, 140, 32}}, {1, 1, {0, 0, 0, 255}}};
  Fixture fixture;
  export_fbx(m, fixture.path);
  Reader reader;
  auto root = reader.document(fixture.path);
  const auto &objects = root.child("Objects");
  size_t meshes = 0, materials = 0, videos = 0, textures = 0;
  for (const auto &n : objects.children) {
    if (n.name == "Geometry") {
      const auto &p =
          std::get<std::vector<double>>(n.child("Vertices").values[0]);
      require(p.size() == 9 && p[2] == -1 && p[4] == 1 &&
                  p[0] == (meshes ? 7 : 0),
              "FBX pose, handedness or face orientation changed");
      const auto &normal = std::get<std::vector<double>>(
          n.child("LayerElementNormal").child("Normals").values[0]);
      require(normal == std::vector<double>{0, 0, -1, 0, 0, -1, 0, 0, -1},
              "FBX omitted or reversed vertex normals");
      const auto &uv = std::get<std::vector<double>>(
          n.child("LayerElementUV").child("UV").values[0]);
      require(uv.size() == 6 && std::abs(uv[0] - .35) < 1e-6 &&
                  std::abs(uv[1] - .3) < 1e-6 && std::abs(uv[3] + 2.7) < 1e-6,
              "FBX omitted source UV transforms, current scrolling or V "
              "conversion");
      require(std::get<std::vector<int32_t>>(
                  n.child("PolygonVertexIndex").values[0]) ==
                  std::vector<int32_t>{0, 1, -3},
              "FBX polygon termination or control-point indices are invalid");
      const auto &color = std::get<std::vector<double>>(
          n.child("LayerElementColor").child("Colors").values[0]);
      require(color.size() == 12 && color[0] == 1 && color[7] == 0 &&
                  color[11] == .5,
              "FBX lost source vertex tint or alpha");
      ++meshes;
    } else if (n.name == "Material")
      ++materials;
    else if (n.name == "Texture")
      ++textures;
    else if (n.name == "Video") {
      auto relative =
          std::get<std::string>(n.child("RelativeFilename").values[0]);
      auto png = fixture.directory / fs::u8path(relative);
      require(fs::exists(png),
              "FBX relative texture reference does not resolve");
      std::ifstream file(png, std::ios::binary);
      char signature[8]{};
      file.read(signature, 8);
      require(std::memcmp(signature, "\x89PNG\r\n\x1a\n", 8) == 0,
              "FBX texture is not PNG");
      ++videos;
    }
  }
  require(
      meshes == 2 && materials == 2 && videos == 2 && textures == 2,
      "FBX included hidden/generated geometry or lost material assignments");
  require(root.child("Connections").children.size() == 10,
          "FBX object/texture connections are incomplete");
  require(m.textures[0].rgba[3] == 32,
          "FBX opaque texture export modified source pixels");
  Model empty;
  bool rejected = false;
  try {
    export_fbx(empty, fixture.directory / "empty.fbx");
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected && !fs::exists(fixture.directory / "empty.fbx"),
          "Empty export created a misleading FBX file");
}
} // namespace
int main() {
  try {
    checks();
    std::cout << "Binary FBX structure, visible pose, normals, winding, UVs, "
                 "vertex colors, materials and relative PNG paths passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

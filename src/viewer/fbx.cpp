#include "assets.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <type_traits>

namespace shaolin::viewer {
namespace {
template <class T> void append(Bytes &out, T value) {
  const auto *p = reinterpret_cast<const uint8_t *>(&value);
  out.insert(out.end(), p, p + sizeof(value));
}
void raw(Bytes &out, const std::string &value) {
  out.insert(out.end(), value.begin(), value.end());
}
struct Property {
  Bytes bytes;
  Property(int32_t value) {
    bytes.push_back('I');
    append(bytes, value);
  }
  Property(int64_t value) {
    bytes.push_back('L');
    append(bytes, value);
  }
  Property(double value) {
    if (!std::isfinite(value))
      throw std::runtime_error("Non-finite FBX value");
    bytes.push_back('D');
    append(bytes, value);
  }
  Property(const char *value) : Property(std::string(value)) {}
  Property(const std::string &value) {
    if (value.size() > UINT32_MAX)
      throw std::runtime_error("FBX string is too large");
    bytes.push_back('S');
    append(bytes, uint32_t(value.size()));
    raw(bytes, value);
  }
  template <class T> Property(const std::vector<T> &values) {
    static_assert(std::is_same_v<T, double> || std::is_same_v<T, int32_t>);
    if (values.size() > UINT32_MAX / sizeof(T))
      throw std::runtime_error("FBX array is too large");
    bytes.push_back(std::is_same_v<T, double> ? 'd' : 'i');
    append(bytes, uint32_t(values.size()));
    append(bytes, uint32_t(0)); // Uncompressed arrays need no external codec.
    append(bytes, uint32_t(values.size() * sizeof(T)));
    for (auto value : values) {
      if constexpr (std::is_same_v<T, double>)
        if (!std::isfinite(value))
          throw std::runtime_error("Non-finite FBX geometry");
      append(bytes, value);
    }
  }
};
struct Node {
  std::string name;
  std::vector<Property> properties;
  std::vector<Node> children;
  Node(std::string label, std::vector<Property> values = {},
       std::vector<Node> nodes = {})
      : name(std::move(label)), properties(std::move(values)),
        children(std::move(nodes)) {}
  void write(Bytes &out) const {
    if (name.size() > 255 || properties.size() > UINT32_MAX)
      throw std::runtime_error("FBX node exceeds version 7.4 limits");
    size_t start = out.size();
    out.resize(start + 13);
    out[start + 12] = uint8_t(name.size());
    raw(out, name);
    size_t property_start = out.size();
    for (const auto &p : properties)
      out.insert(out.end(), p.bytes.begin(), p.bytes.end());
    size_t property_size = out.size() - property_start;
    for (const auto &n : children)
      n.write(out);
    if (!children.empty())
      out.resize(out.size() + 13);
    if (out.size() > UINT32_MAX || property_size > UINT32_MAX)
      throw std::runtime_error("FBX export exceeds the 4 GiB format limit");
    uint32_t header[]{uint32_t(out.size()), uint32_t(properties.size()),
                      uint32_t(property_size)};
    std::memcpy(out.data() + start, header, sizeof(header));
  }
};
Node property(const char *name, const char *type, std::vector<Property> values,
              const char *flags = "") {
  Node p{"P", {name, type, "", flags}};
  for (auto &value : values)
    p.properties.push_back(std::move(value));
  return p;
}
std::string typed_name(const std::string &name, const char *type) {
  std::string result = name;
  result.append("\0\1", 2);
  return result + type;
}
std::string utf8(const std::filesystem::path &path) {
  auto value = path.generic_u8string();
  return {value.begin(), value.end()};
}
Node attribute(const char *type, Node data,
               const char *mapping = "ByPolygonVertex",
               const char *reference = "Direct") {
  return {type,
          {int32_t(0)},
          {{"Version", {int32_t(101)}},
           {"Name", {"UVSet"}},
           {"MappingInformationType", {mapping}},
           {"ReferenceInformationType", {reference}},
           std::move(data)}};
}
} // namespace

void export_fbx(const Model &model, const std::filesystem::path &path) {
  Node objects{"Objects"}, connections{"Connections"};
  std::map<std::string, int32_t> counts;
  int64_t id = 1000;
  auto object = [&](Node node) {
    ++counts[node.name];
    objects.children.push_back(std::move(node));
  };
  auto connect = [&](int64_t from, int64_t to, const char *channel = nullptr) {
    Node n{"C", {channel ? "OP" : "OO", from, to}};
    if (channel)
      n.properties.emplace_back(channel);
    connections.children.push_back(std::move(n));
  };
  std::map<std::pair<int, bool>, std::pair<int64_t, std::filesystem::path>>
      images;
  auto image = [&](int index,
                   bool opaque) -> std::pair<int64_t, std::filesystem::path> {
    auto key = std::pair{index, opaque};
    if (auto found = images.find(key); found != images.end())
      return found->second;
    auto png = path.parent_path() / path.stem();
    png +=
        "_texture_" + std::to_string(index) + (opaque ? "_opaque.png" : ".png");
    auto pixels = model.textures.at(size_t(index));
    if (opaque)
      for (size_t i = 3; i < pixels.rgba.size(); i += 4)
        pixels.rgba[i] = 255;
    save_png(pixels, png);
    int64_t video = id++;
    auto relative = utf8(png.filename());
    object({"Video",
            {video, typed_name(relative, "Video"), "Clip"},
            {{"Type", {"Clip"}},
             {"UseMipMap", {int32_t(0)}},
             {"Filename", {relative}},
             {"RelativeFilename", {relative}}}});
    auto result = std::pair{video, png.filename()};
    images.emplace(key, result);
    return result;
  };
  size_t exported = 0;
  for (const auto &part : model.parts) {
    if (!part.visible || part.vertices.empty() ||
        part.role == MeshRole::ParticleEmitter ||
        part.role == MeshRole::DebugFloor)
      continue;
    if (part.vertices.size() % 3 || part.vertices.size() > INT32_MAX)
      throw std::runtime_error("FBX requires complete triangle geometry");
    std::vector<double> positions, normals, uvs, colors;
    std::vector<int32_t> polygons, uv_indices;
    size_t count = part.vertices.size();
    positions.reserve(count * 3);
    normals.reserve(count * 3);
    uvs.reserve(count * 2);
    colors.reserve(count * 4);
    polygons.reserve(count);
    uv_indices.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      // Convert the viewer's left-handed coordinates to FBX Y-up/right-handed
      // coordinates, reversing each face with the reflected Z axis.
      size_t local = i % 3;
      const auto &v = part.vertices[i / 3 * 3 + (local == 0 ? 0 : 3 - local)];
      positions.insert(positions.end(),
                       {double(v.x), double(v.y), -double(v.z)});
      normals.insert(normals.end(),
                     {double(v.nx), double(v.ny), -double(v.nz)});
      uvs.push_back(double(v.u) * part.diffuse.scale[0] +
                    part.diffuse.offset[0] + part.uv_offset[0]);
      uvs.push_back(1 - (double(v.v) * part.diffuse.scale[1] +
                         part.diffuse.offset[1] + part.uv_offset[1]));
      for (unsigned shift : {16u, 8u, 0u, 24u})
        colors.push_back(std::min(1., double((v.color >> shift) & 255) / 128));
      polygons.push_back(local == 2 ? ~int32_t(i) : int32_t(i));
      uv_indices.push_back(int32_t(i));
    }
    int64_t geometry = id++, mesh = id++, material = id++;
    std::string name =
        part.name.empty() ? "Part " + std::to_string(exported + 1) : part.name;
    Node g{"Geometry",
           {geometry, typed_name(name, "Geometry"), "Mesh"},
           {{"GeometryVersion", {int32_t(124)}},
            {"Vertices", {positions}},
            {"PolygonVertexIndex", {polygons}},
            attribute("LayerElementNormal", {"Normals", {normals}}),
            attribute("LayerElementColor", {"Colors", {colors}})}};
    auto uv = attribute("LayerElementUV", {"UV", {uvs}}, "ByPolygonVertex",
                        "IndexToDirect");
    uv.children.push_back({"UVIndex", {uv_indices}});
    g.children.push_back(std::move(uv));
    g.children.push_back(attribute("LayerElementMaterial",
                                   {"Materials", {std::vector<int32_t>{0}}},
                                   "AllSame", "IndexToDirect"));
    Node layer{"Layer", {int32_t(0)}, {{"Version", {int32_t(100)}}}};
    for (const char *type : {"LayerElementNormal", "LayerElementColor",
                             "LayerElementUV", "LayerElementMaterial"})
      layer.children.push_back(
          {"LayerElement",
           {},
           {{"Type", {type}}, {"TypedIndex", {int32_t(0)}}}});
    g.children.push_back(std::move(layer));
    object(std::move(g));
    object(
        {"Model",
         {mesh, typed_name(name, "Model"), "Mesh"},
         {{"Version", {int32_t(232)}},
          {"Properties70",
           {},
           {property("Lcl Translation", "Lcl Translation", {0., 0., 0.}, "A"),
            property("Lcl Rotation", "Lcl Rotation", {0., 0., 0.}, "A"),
            property("Lcl Scaling", "Lcl Scaling", {1., 1., 1.}, "A"),
            property("Visibility", "Visibility", {1.}, "A")}},
          {"Shading", {int32_t(1)}},
          {"Culling", {part.two_sided ? "CullingOff" : "CullingOnCCW"}}}});
    object({"Material",
            {material, typed_name(name, "Material"), ""},
            {{"Version", {int32_t(102)}},
             {"ShadingModel", {"phong"}},
             {"MultiLayer", {int32_t(0)}},
             {"Properties70",
              {},
              {property("DiffuseColor", "Color", {1., 1., 1.}, "A"),
               property("DiffuseFactor", "Number", {1.}, "A"),
               property("EmissiveColor", "Color", {1., 1., 1.}, "A"),
               property("EmissiveFactor", "Number", {part.emissive ? 1. : 0.},
                        "A"),
               property("SpecularFactor", "Number", {0.}, "A"),
               property("Opacity", "double", {double(part.opacity)}),
               property("TransparencyFactor", "Number", {1. - part.opacity},
                        "A")}}}});
    connect(mesh, 0);
    connect(geometry, mesh);
    connect(material, mesh);
    if (part.texture >= 0 && size_t(part.texture) < model.textures.size() &&
        !model.textures[part.texture].rgba.empty()) {
      auto [video, filename] =
          image(part.texture, part.opaque || !part.texture_alpha);
      int64_t texture = id++;
      auto relative = utf8(filename);
      object({"Texture",
              {texture, typed_name(name, "Texture"), ""},
              {{"Type", {"TextureVideoClip"}},
               {"Version", {int32_t(202)}},
               {"TextureName", {typed_name(name, "Texture")}},
               {"Media", {typed_name(relative, "Video")}},
               {"FileName", {relative}},
               {"RelativeFilename", {relative}},
               {"ModelUVTranslation", {0., 0.}},
               {"ModelUVScaling", {1., 1.}},
               {"Texture_Alpha_Source", {"Alpha"}},
               {"Cropping", {0, 0, 0, 0}},
               {"Properties70",
                {},
                {property("UVSet", "KString", {"UVSet"}),
                 property("WrapModeU", "enum",
                          {int32_t((part.material_flags & 1) ? 0 : 1)}),
                 property("WrapModeV", "enum",
                          {int32_t((part.material_flags & 2) ? 0 : 1)})}}}});
      connect(video, texture);
      connect(texture, material, "DiffuseColor");
      if (part.emissive)
        connect(texture, material, "EmissiveColor");
    }
    ++exported;
  }
  if (!exported)
    throw std::runtime_error("No visible mesh to export");
  Node settings{"Properties70"};
  for (auto [name, value] : {std::pair{"UpAxis", 1},
                             {"UpAxisSign", 1},
                             {"FrontAxis", 2},
                             {"FrontAxisSign", -1},
                             {"CoordAxis", 0},
                             {"CoordAxisSign", 1},
                             {"OriginalUpAxis", 1},
                             {"OriginalUpAxisSign", 1}})
    settings.children.push_back(property(name, "int", {int32_t(value)}));
  settings.children.push_back(property("UnitScaleFactor", "double", {1.}));
  settings.children.push_back(
      property("OriginalUnitScaleFactor", "double", {1.}));
  Node definitions{"Definitions",
                   {},
                   {{"Version", {int32_t(100)}},
                    {"Count", {int32_t(objects.children.size())}}}};
  for (const auto &[type, count] : counts)
    definitions.children.push_back(
        {"ObjectType", {type}, {{"Count", {count}}}});
  Bytes bytes;
  raw(bytes, std::string("Kaydara FBX Binary  \0\x1a\0", 23));
  append(bytes, uint32_t(7400));
  Node{"FBXHeaderExtension",
       {},
       {{"FBXHeaderVersion", {int32_t(1003)}},
        {"FBXVersion", {int32_t(7400)}},
        {"EncryptionType", {int32_t(0)}},
        {"Creator", {"Shaolin Monks Viewer"}}}}
      .write(bytes);
  Node{
      "GlobalSettings", {}, {{"Version", {int32_t(1000)}}, std::move(settings)}}
      .write(bytes);
  Node{"Documents",
       {},
       {{"Count", {int32_t(1)}},
        {"Document",
         {int64_t(1), "Scene", "Scene"},
         {{"RootNode", {int64_t(0)}}}}}}
      .write(bytes);
  definitions.write(bytes);
  objects.write(bytes);
  connections.write(bytes);
  bytes.resize(bytes.size() + 13);
  const uint8_t footer[]{0xfa, 0xbc, 0xab, 0x09, 0xd0, 0xc8, 0xd4, 0x66,
                         0xb1, 0x76, 0xfb, 0x83, 0x1c, 0xf7, 0x26, 0x7e};
  bytes.insert(bytes.end(), std::begin(footer), std::end(footer));
  bytes.resize(bytes.size() + (16 - bytes.size() % 16) % 16 + 4);
  append(bytes, uint32_t(7400));
  bytes.resize(bytes.size() + 120);
  const uint8_t end[]{0xf8, 0x5a, 0x8c, 0x6a, 0xde, 0xf5, 0xd9, 0x7e,
                      0xec, 0xe9, 0x0c, 0xe3, 0x75, 0x8f, 0x29, 0x0b};
  bytes.insert(bytes.end(), std::begin(end), std::end(end));
  std::ofstream file(path, std::ios::binary);
  if (!file ||
      !file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()))
    throw std::runtime_error("Cannot write FBX export");
  file.flush();
  if (!file)
    throw std::runtime_error("FBX export did not complete");
}
} // namespace shaolin::viewer

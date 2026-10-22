#include "assets.h"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

using namespace shaolin::viewer;
namespace fs = std::filesystem;
namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class T> void put(Bytes &bytes, size_t at, T value) {
  std::memcpy(bytes.data() + at, &value, sizeof(value));
}
void text(Bytes &bytes, size_t at, const char *value) {
  std::memcpy(bytes.data() + at, value, std::strlen(value) + 1);
}
Bytes texture(uint16_t color) {
  Bytes bytes(44);
  text(bytes, 0, "XPR0");
  put(bytes, 8, 36u);
  put(bytes, 24, uint32_t((2 << 20) | (2 << 24) | (12 << 8)));
  put(bytes, 36, color);
  return bytes;
}
Bytes archive(uint32_t count, const std::map<uint32_t, Bytes> &entries) {
  Bytes bytes(32 + count * 8);
  text(bytes, 0, "PWF ");
  put(bytes, 8, 2u);
  put(bytes, 12, count);
  put(bytes, 16, 0x100000u);
  for (auto &[id, data] : entries) {
    put(bytes, 32 + id * 8, uint32_t(bytes.size()));
    put(bytes, 36 + id * 8, uint32_t(data.size()));
    bytes.insert(bytes.end(), data.begin(), data.end());
  }
  return bytes;
}
void rejects(auto work) {
  bool rejected = false;
  try {
    work();
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "Malformed artwork input accepted");
}
struct Fixture {
  fs::path folder =
      fs::temp_directory_path() /
      ("shaolin-viewer-art-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  Fixture() { fs::create_directory(folder); }
  ~Fixture() {
    std::error_code error;
    fs::remove(folder / "GAMEDATA.WAD", error);
    fs::remove(folder, error);
  }
};
void checks() {
  std::map<ArtGroup, std::set<uint32_t>> numbers;
  for (uint32_t id = 0; id < 8000; ++id) {
    auto page = concept_art_page(id);
    if (page.group == ArtGroup::None)
      continue;
    require(page.number && page.thumbnail_id != UINT32_MAX &&
                page.thumbnail_index < 16,
            "Gallery page metadata is incomplete");
    require(numbers[page.group].insert(page.number).second,
            "A gallery category has duplicate page numbers");
  }
  const std::map<ArtGroup, size_t> expected = {{ArtGroup::Characters, 88},
                                               {ArtGroup::Environments, 64},
                                               {ArtGroup::Storyboards, 31},
                                               {ArtGroup::DesignStudies, 17}};
  for (auto [group, count] : expected) {
    auto &pages = numbers[group];
    require(pages.size() == count && *pages.begin() == 1 &&
                *pages.rbegin() == count,
            "Artwork collection is incomplete");
  }
  for (uint32_t id : {438u, 455u, 489u, 490u, 506u, 600u, 602u, 617u, 686u,
                      725u, 742u, 743u, 748u, 757u, 759u, 913u})
    require(concept_art_page(id).group == ArtGroup::None,
            "An unrelated texture was added to the artwork gallery");
  require(concept_art_page(749).thumbnail_index == 6 &&
              concept_art_page(758).thumbnail_index == 15,
          "Mixed gallery bank offsets are incorrect");
  auto red = texture(0xf800), green = texture(0x07e0), blue = texture(0x001f);
  auto bytes = archive(800, {{439, red},
                             {440, blue},
                             {455, archive(2, {{0, green}, {1, blue}})},
                             {490, red},
                             {513, blue},
                             {512, archive(1, {{0, green}})},
                             {600, red},
                             {602, red},
                             {757, red}});
  Fixture fixture;
  auto path = fixture.folder / "GAMEDATA.WAD";
  {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    require(bool(file), "Cannot write synthetic artwork fixture");
  }
  Source source(fixture.folder);
  auto catalog = source.catalog();
  require(catalog.size() == 7, "Artwork catalog size is incorrect");
  require(catalog[0].kind == Kind::ConceptArt &&
              catalog[0].name == "Characters 01  [439]",
          "Artwork is not selectable by its gallery label");
  require(catalog[2].kind == Kind::Texture &&
              catalog[3].kind == Kind::ConceptArt,
          "Photo or environment artwork classification is incorrect");
  auto full = source.image(catalog[0]),
       thumbnail = source.image(catalog[0], true);
  require(full.width == 4 && full.height == 4 && full.rgba[0] == 255 &&
              full.rgba[1] == 0,
          "Full-size artwork was replaced by a thumbnail");
  require(thumbnail.width == 4 && thumbnail.rgba[0] == 0 &&
              thumbnail.rgba[1] == 255,
          "Original gallery thumbnail was not decoded");
  require(source.image(catalog[1], true).rgba[2] == 255,
          "The gallery selected the wrong thumbnail index");
  auto invalid = catalog[0];
  invalid.art.thumbnail_index = 20;
  rejects([&] { source.image(invalid, true); });
  require(source.image(invalid).rgba[0] == 255,
          "Missing thumbnail prevents full-size image loading");
  invalid.kind = Kind::Stage;
  rejects([&] { source.image(invalid); });
  std::ifstream input(path, std::ios::binary);
  Bytes original((std::istreambuf_iterator<char>(input)), {});
  require(original == bytes, "Artwork browsing modified the source archive");
}
} // namespace
int main() {
  try {
    checks();
    std::cout << "Concept art catalog, full-size images, thumbnails, and "
                 "source integrity passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

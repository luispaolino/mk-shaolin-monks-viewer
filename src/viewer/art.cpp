#include "assets.h"
#include <stdexcept>

namespace shaolin::viewer {
const char *art_group_name(ArtGroup group) {
  switch (group) {
  case ArtGroup::Characters:
    return "Characters";
  case ArtGroup::Environments:
    return "Environments";
  case ArtGroup::Storyboards:
    return "Storyboards";
  case ArtGroup::DesignStudies:
    return "Design studies";
  default:
    return "Concept art";
  }
}
ArtPage concept_art_page(uint32_t resource_id) {
  struct Pages {
    uint32_t first, last, thumbnail, thumbnail_offset, number;
    ArtGroup group;
  };
  // Original Xbox gallery pages and their thumbnail banks. These ranges exclude
  // team photos, move instructions, loading screens, and menu texture atlases.
  constexpr Pages pages[] = {
      {439, 454, 455, 0, 1, ArtGroup::Characters},
      {456, 471, 472, 0, 17, ArtGroup::Characters},
      {473, 488, 489, 0, 33, ArtGroup::Characters},
      {513, 528, 512, 0, 1, ArtGroup::Environments},
      {530, 545, 529, 0, 17, ArtGroup::Environments},
      {547, 562, 546, 0, 33, ArtGroup::Environments},
      {564, 579, 563, 0, 49, ArtGroup::Environments},
      {585, 599, 584, 0, 1, ArtGroup::Storyboards},
      {619, 634, 618, 0, 49, ArtGroup::Characters},
      {636, 651, 635, 0, 65, ArtGroup::Characters},
      {653, 668, 652, 0, 1, ArtGroup::DesignStudies},
      {670, 685, 669, 0, 16, ArtGroup::Storyboards},
      {749, 756, 742, 6, 81, ArtGroup::Characters},
      {758, 758, 742, 15, 17, ArtGroup::DesignStudies},
  };
  for (auto &range : pages)
    if (resource_id >= range.first && resource_id <= range.last) {
      auto offset = resource_id - range.first;
      return {range.group, range.number + offset, range.thumbnail,
              range.thumbnail_offset + offset};
    }
  return {};
}
Image Source::image(const Asset &asset, bool thumbnail) const {
  if (asset.kind != Kind::Texture && asset.kind != Kind::ConceptArt)
    throw std::runtime_error("This resource is not an image");
  if (thumbnail && asset.art.thumbnail_id != UINT32_MAX) {
    auto bank = members(load(asset.art.thumbnail_id));
    if (asset.art.thumbnail_index >= bank.size())
      throw std::runtime_error("Artwork thumbnail is missing");
    return decode_xpr(bank[asset.art.thumbnail_index]);
  }
  return decode_xpr(load(asset.id));
}
} // namespace shaolin::viewer

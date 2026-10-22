#include "assets.h"
#include <cstring>
#include <stdexcept>

namespace shaolin::viewer {
Atmosphere decode_atmosphere(std::span<const uint8_t> bytes) {
  Atmosphere out;
  if (bytes.empty())
    return out;
  if (bytes.size() != 5 * 7 * sizeof(int32_t))
    throw std::runtime_error("Unsupported stage atmosphere block");
  auto integer = [&](size_t at) {
    int32_t value;
    std::memcpy(&value, bytes.data() + at, sizeof(value));
    return value;
  };
  // The original loader copies five seven-integer presets. The render setup
  // normalizes RGB and min/max opacity by 255, retaining signed distance
  // ranges.
  for (size_t i = 0; i < out.fog.size(); ++i) {
    auto &fog = out.fog[i];
    size_t at = i * 28;
    for (size_t k = 0; k < 7; ++k) {
      auto value = integer(at + k * 4);
      bool distance = k == 3 || k == 4;
      if (value < 0 || value > (distance ? 100000000 : 255))
        throw std::runtime_error("Invalid stage atmosphere value");
      if (k < 3)
        fog.color[k] = value / 255.f;
      else if (k == 3)
        fog.start = float(value);
      else if (k == 4)
        fog.end = float(value);
      else if (k == 5)
        fog.minimum = value / 255.f;
      else
        fog.maximum = value / 255.f;
    }
  }
  out.available = true;
  return out;
}
} // namespace shaolin::viewer

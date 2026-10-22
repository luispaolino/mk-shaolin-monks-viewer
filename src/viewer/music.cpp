#include "music.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace shaolin::viewer {
namespace {
uint32_t word(std::span<const uint8_t> b, size_t p, bool big = false,
              size_t n = 4) {
  if (p > b.size() || n > b.size() - p)
    throw std::runtime_error("Truncated music data");
  uint32_t v = 0;
  for (size_t k = 0; k < n; ++k)
    v |= uint32_t(b[p + k]) << ((big ? n - k - 1 : k) * 8);
  return v;
}
} // namespace
MusicCue decode_music_cue(std::span<const uint8_t> b) {
  if (b.empty())
    return {};
  auto count = word(b, 20);
  if (count > 256 || word(b, 24) != count || 32 + size_t(count) * 28 > b.size())
    throw std::runtime_error("Invalid stage music definitions");
  size_t text = 32 + size_t(count) * 28;
  for (uint32_t i = 0; i < count; ++i) {
    auto end = text;
    while (end < b.size() && b[end])
      ++end;
    if (end == b.size() || end - text > 128)
      throw std::runtime_error("Invalid stage music cue name");
    std::string name(reinterpret_cast<const char *>(b.data() + text),
                     end - text);
    text = end + 1;
    // Cue zero is the bank's base intensity; other cues are
    // combat/intros/endings.
    if (word(b, 28 + i * 28 + 4) != 0)
      continue;
    auto marker = name.find("_ADX_");
    if (marker == std::string::npos || !name.ends_with("_ADX"))
      return {};
    name = name.substr(marker + 5, name.size() - marker - 9);
    for (char &c : name) {
      if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
        throw std::runtime_error("Invalid stage music filename");
      if (c >= 'A' && c <= 'Z')
        c += 'a' - 'A';
    }
    float volume;
    auto bits = word(b, 28 + i * 28 + 8);
    std::memcpy(&volume, &bits, 4);
    if (!std::isfinite(volume) || volume < 0 || volume > 1)
      throw std::runtime_error("Invalid stage music volume");
    return {name + ".adx", volume};
  }
  return {};
}
std::string music_group(std::span<const uint8_t> b) {
  constexpr std::string_view start = "MAP_", end = "_AHX_";
  for (size_t i = 0; i + start.size() < b.size(); ++i) {
    if (std::memcmp(b.data() + i, start.data(), start.size()))
      continue;
    for (size_t j = i + start.size(); j + end.size() <= b.size() && j - i < 64;
         ++j) {
      if (!std::memcmp(b.data() + j, end.data(), end.size()))
        return std::string(reinterpret_cast<const char *>(b.data() + i), j - i);
      if (!((b[j] >= 'A' && b[j] <= 'Z') || b[j] == '_'))
        break;
    }
  }
  return {};
}
MusicTrack decode_adx(std::span<const uint8_t> b) {
  if (word(b, 0, true, 2) != 0x8000 || b.size() < 24 || b[4] != 3 ||
      b[5] != 18 || b[6] != 4 || (b[7] != 1 && b[7] != 2))
    throw std::runtime_error("Unsupported ADX music format");
  size_t data = word(b, 2, true, 2) + 4;
  auto version = word(b, 18, true, 2);
  if ((version != 0x300 && version != 0x400) || data < 24 || data > b.size() ||
      std::memcmp(b.data() + data - 6, "(c)CRI", 6))
    throw std::runtime_error("Invalid or encrypted ADX music header");
  MusicTrack out;
  out.channels = b[7];
  out.rate = word(b, 8, true);
  uint32_t frames = word(b, 12, true), cutoff = word(b, 16, true, 2);
  if (out.rate < 8000 || out.rate > 96000 || !frames ||
      frames > out.rate * 600ull || cutoff >= out.rate / 2)
    throw std::runtime_error("Invalid ADX music duration or sample rate");
  size_t blocks = (size_t(frames) + 31) / 32;
  if (blocks * 18 * out.channels > b.size() - data ||
      size_t(frames) * out.channels > 64 * 1024 * 1024)
    throw std::runtime_error("Truncated or oversized ADX music");
  std::array<int32_t, 2> previous{}, older{};
  if (version == 0x400) {
    if (data < 38)
      throw std::runtime_error("Truncated ADX sample history");
    for (size_t ch = 0; ch < out.channels; ++ch) {
      previous[ch] = int16_t(word(b, 24 + ch * 4, true, 2));
      older[ch] = int16_t(word(b, 26 + ch * 4, true, 2));
    }
  }
  size_t loop = version == 0x300 ? 20 : 32;
  if (data >= loop + 24 + 6 && word(b, loop + 4, true)) {
    out.loop_begin = word(b, loop + 8, true);
    out.loop_end = word(b, loop + 16, true);
    if (out.loop_begin >= out.loop_end || out.loop_end > frames)
      throw std::runtime_error("Invalid ADX loop points");
  }
  const double root = std::sqrt(2.0);
  double a = root - std::cos(6.283185307179586 * cutoff / out.rate);
  double c = (a - std::sqrt(a * a - (root - 1) * (root - 1))) / (root - 1);
  int32_t first = int32_t(c * 8192), second = int32_t(-c * c * 4096);
  out.samples.resize(size_t(frames) * out.channels);
  for (size_t block = 0; block < blocks; ++block)
    for (size_t ch = 0; ch < out.channels; ++ch) {
      size_t at = data + (block * out.channels + ch) * 18;
      int32_t scale = word(b, at, true, 2);
      if (scale & 0x8000)
        throw std::runtime_error("Unexpected ADX music end marker");
      for (size_t j = 0; j < 32 && block * 32 + j < frames; ++j) {
        int32_t residual = (b[at + 2 + j / 2] >> ((j & 1) ? 0 : 4)) & 15;
        if (residual >= 8)
          residual -= 16;
        int32_t sample = std::clamp(residual * scale +
                                        int32_t((int64_t(first) * previous[ch] +
                                                 int64_t(second) * older[ch]) >>
                                                12),
                                    -32768, 32767);
        older[ch] = previous[ch];
        previous[ch] = sample;
        out.samples[(block * 32 + j) * out.channels + ch] = int16_t(sample);
      }
    }
  return out;
}
} // namespace shaolin::viewer

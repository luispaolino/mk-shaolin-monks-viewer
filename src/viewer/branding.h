#pragma once
#include "../../resources/viewer_ids.h"
#include "assets.h"
#include <cstring>
#include <stdexcept>
#include <windows.h>

namespace shaolin::viewer {
inline constexpr char about_title[] = "About Shaolin Monks Viewer";
inline constexpr char build_label[] = "Development build - " __DATE__;

inline Image embedded_icon() {
  auto module = GetModuleHandleW(nullptr);
  auto resource =
      FindResourceW(module, MAKEINTRESOURCEW(IDR_VIEWER_ABOUT_ICON), RT_RCDATA);
  auto loaded = resource ? LoadResource(module, resource) : nullptr;
  auto data =
      loaded ? static_cast<const uint8_t *>(LockResource(loaded)) : nullptr;
  size_t size = resource ? SizeofResource(module, resource) : 0;
  if (!data || size < 8)
    throw std::runtime_error("Missing embedded viewer icon");
  uint32_t width, height;
  std::memcpy(&width, data, 4);
  std::memcpy(&height, data + 4, 4);
  if (!width || !height || width > 1024 || height > 1024 ||
      size != 8 + size_t(width) * height * 4)
    throw std::runtime_error("Invalid embedded viewer icon");
  return {width, height, Bytes(data + 8, data + size)};
}
} // namespace shaolin::viewer

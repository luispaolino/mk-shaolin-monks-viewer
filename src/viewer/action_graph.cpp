#include "assets.h"
#include <cstring>
#include <stdexcept>

namespace shaolin::viewer {
namespace {
template <class T> T get(std::span<const uint8_t> b, size_t at) {
  if (at > b.size() || sizeof(T) > b.size() - at)
    throw std::runtime_error("Truncated character action graph");
  T value;
  std::memcpy(&value, b.data() + at, sizeof(value));
  return value;
}
size_t section(std::span<const uint8_t> b, unsigned i) {
  if (get<uint32_t>(b, 0) != 3 || i > 2)
    throw std::runtime_error("Unsupported character action container");
  size_t at = 20ull + get<uint32_t>(b, 4 + i * 4);
  if (at > b.size())
    throw std::runtime_error("Invalid character action section");
  return at;
}
} // namespace
ActionGraph decode_action_graph(std::span<const uint8_t> graph,
                                std::span<const uint8_t> actions,
                                std::vector<Clip> &clips) {
  ActionGraph out;
  // Retail loaders 1D84A0/1D9DB0: 112-byte action records and 56-byte
  // input nodes. Node links are indices; negative markers select state roots.
  size_t records = section(actions, 0), end = section(actions, 1);
  if (end < records || (end - records) % 112 ||
      (end - records) / 112 != clips.size())
    return out;
  size_t first = section(graph, 2), limit = 20ull + get<uint32_t>(graph, 16);
  if (limit < first || limit > graph.size() || (limit - first) % 56 ||
      (limit - first) / 56 > 65536)
    return out;
  bool sentinel = false;
  for (size_t p = first; p + 56 <= limit; p += 56) {
    int action = get<int16_t>(graph, p);
    if (action == -999) {
      sentinel = true;
      break;
    }
    ActionNode n;
    n.action = action;
    n.start = get<int8_t>(graph, p + 2);
    n.blend = get<int8_t>(graph, p + 3);
    n.earliest = get<uint8_t>(graph, p + 5);
    n.conditions = get<uint32_t>(graph, p + 8);
    n.conditions2 = get<uint32_t>(graph, p + 12);
    n.timeout = get<int16_t>(graph, p + 16);
    n.pressed = get<uint16_t>(graph, p + 20);
    n.held = get<uint16_t>(graph, p + 22);
    n.released = get<uint16_t>(graph, p + 24);
    n.ignored = get<uint16_t>(graph, p + 26);
    n.child = get<uint32_t>(graph, p + 32);
    n.sibling = get<uint32_t>(graph, p + 36);
    n.predicate = get<int16_t>(graph, p + 44);
    out.nodes.push_back(n);
    if (action <= -2 && action >= -28)
      out.roots[size_t(-2 - action)] = uint32_t(out.nodes.size());
  }
  if (!sentinel || out.nodes.empty())
    return {};
  for (const auto &n : out.nodes)
    if (n.child >= out.nodes.size() || n.sibling >= out.nodes.size() ||
        n.blend < 0 || n.timeout < 0)
      return {};
  for (auto root : out.roots)
    if (root >= out.nodes.size())
      return {};
  for (size_t i = 0; i < clips.size(); ++i) {
    uint32_t id = get<uint32_t>(actions, records + i * 112);
    if (id > 65535)
      return {};
    clips[i].source_action = int(id);
  }
  return out;
}
} // namespace shaolin::viewer

#include "character_debug.h"
#include "renderer.h"
#include <iostream>
#include <limits>
using namespace shaolin::viewer;
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
Clip clip(const char *name, float z = 0) {
  Clip c;
  c.name = name;
  c.frames = 31;
  c.fps = 30;
  c.bones = {0};
  c.keys = {{0, {{{0, 0, 0}, {0, 0, 0}, true}}},
            {30, {{{0, 0, 0}, {0, 0, z}, true}}}};
  return c;
}
Image checker() {
  return {2,
          2,
          {200, 200, 200, 255, 60, 60, 60, 255, 60, 60, 60, 255, 200, 200, 200,
           255}};
}
Model actor() {
  Model m;
  m.bones = {{"pelvis", -1}};
  Part p;
  p.vertices = {{-.5f, 0, 0, 0, 0, 0x80808080, 0, 0, 1},
                {.5f, 0, 0, 1, 0, 0x80808080, 0, 0, 1},
                {0, 1, 0, 0, 1, 0x80808080, 0, 0, 1}};
  p.bind = p.vertices;
  for (auto &v : p.vertices) {
    Influence skin;
    skin.weight[0] = 1;
    skin.local[0] = {v.x, -v.y, v.z};
    p.skin.push_back(skin);
  }
  m.parts = {p};
  m.clips = {clip("a00_stnd"), clip("a10_walk", -1), clip("a20_nrun", -3),
             clip("a30_jump"), clip("a33_jump")};
  m.debug_meter = 1;
  m.controls.available = true;
  m.controls.release_ticks = 5;
  m.controls.landing_cancel_frame = 8;
  m.controls.position_step = m.controls.release_scale = .5f;
  m.controls.air_limit_drag = .25f;
  for (auto &p : m.controls.jump) {
    p.launch = 185.f / 512;
    p.gravity = 8.f / 512;
    p.maximum_speed = 30.f / 512;
    p.turn = .1f;
  }
  m.bounds();
  return m;
}
void graph_checks() {
  auto model = actor();
  model.controls.ground_turn = .46f;
  model.clips.push_back(clip("g00_st_q"));
  model.clips.push_back(clip("g01_st_q"));
  model.clips.push_back(clip("i00_st_p"));
  model.clips[5].keys.back().pose[0].translation[2] = -3;
  Bytes actions(20 + model.clips.size() * 112), graph(40 + 6 * 56);
  auto write = [](Bytes &b, size_t at, auto value) {
    std::memcpy(b.data() + at, &value, sizeof(value));
  };
  for (auto *b : {&actions, &graph})
    write(*b, 0, 3u);
  write(actions, 8, uint32_t(actions.size() - 20));
  for (size_t i = 0; i < model.clips.size(); ++i)
    write(actions, 20 + i * 112, uint32_t(100 + i));
  write(graph, 8, 16u);
  write(graph, 12, 20u);
  write(graph, 16, uint32_t(graph.size() - 20));
  auto node = [&](size_t i, int16_t id, uint16_t button, uint32_t child,
                  uint32_t sibling, uint8_t earliest = 0) {
    size_t p = 40 + i * 56;
    write(graph, p, id);
    write(graph, p + 5, earliest);
    write(graph, p + 16, int16_t(i == 1 || i == 3 ? 5000 : 40));
    write(graph, p + 20, button);
    write(graph, p + 32, child);
    write(graph, p + 36, sibling);
  };
  node(0, -2, 0, 0, 0);
  node(1, 105, 0x80, 2, 3);
  node(2, 106, 0x80, 0, 4, 10);
  write(graph, 40 + 2 * 56 + 2, int8_t(7));
  write(graph, 40 + 2 * 56 + 3, int8_t(6));
  node(3, 107, 0x20, 0, 0);
  node(4, 107, 0x20, 0, 0, 12);
  write(graph, 40 + 4 * 56 + 44, int16_t(9000));
  node(5, -999, 0, 0, 0);
  model.action_graph = decode_action_graph(graph, actions, model.clips);
  require(model.action_graph.available() && model.clips[6].source_action == 106,
          "Original graph sections/action identifiers were not decoded");
  auto broken = graph;
  write(broken, 40 + 56 + 32, 10000u);
  require(!decode_action_graph(broken, actions, model.clips).available(),
          "Out-of-range original graph link was accepted");
  bool truncated = false;
  try {
    decode_action_graph(std::span(graph).first(8), actions, model.clips);
  } catch (const std::exception &) {
    truncated = true;
  }
  require(truncated, "Truncated graph was accepted");
  CharacterDebug d;
  d.begin(model, checker(), {});
  DebugInput quick;
  quick.buttons = 0x80;
  for (int i = 0; i < 20; ++i)
    d.update(model, quick, 1. / 60, 0);
  require(d.attack_clip == 5 && d.graph_queue.empty(),
          "Holding an attack invented repeated button presses");
  d.reset();
  for (int i = 0; i < 50; ++i)
    d.update(model, quick, 1. / 60, 0);
  require(d.attack_clip == -1 && std::abs(d.position[2] + 3) < .0001f,
          "Authored attack travel was lost or snapped back after the attack");
  d.reset();
  d.update(model, quick, 1. / 60, 0);
  d.update(model, {}, 1. / 60, 0);
  d.update(model, quick, 1. / 60, 0);
  require(
      d.attack_clip == 5 && d.graph_queue.size() == 1,
      "Early source input did not buffer or canceled before its start frame");
  for (int i = 0; i < 30 && d.attack_clip == 5; ++i)
    d.update(model, {}, 1. / 60, 0);
  require(d.attack_clip == 6 && d.frame == 7,
          "Graph child did not use its authored link/start frame/blend");
  d.reset();
  d.update(model, quick, 1. / 60, 0);
  d.update(model, {}, 1. / 60, 0);
  DebugInput power;
  power.buttons = 0x20;
  d.update(model, power, 1. / 60, 0);
  require(d.graph_queue.empty(),
          "Unavailable world predicate was treated as a successful hit/unlock");
  d.end(model);
  model.clips[5].frames = 121;
  d.begin(model, checker(), {});
  d.update(model, quick, 1. / 60, 0);
  for (int i = 0; i < 45; ++i)
    d.update(model, {}, 1. / 60, 0);
  double recovery_time = d.attack_time;
  d.update(model, quick, 1. / 60, 0);
  require(d.graph_queue.size() == 1 && d.attack_time > recovery_time,
          "A root input canceled attack recovery instead of waiting for idle");
  d.end(model);
  model.clips[5].frames = 31;
  std::array<float, 3> reference{};
  float reference_frame = 0;
  for (int hz : {30, 60, 120, 144}) {
    d.begin(model, checker(), {});
    for (int t = 0; t < hz; ++t)
      d.update(model, {1, 0, true}, 1. / hz, 0);
    if (hz == 30) {
      reference = d.position;
      reference_frame = d.frame;
    } else
      require(std::abs(d.position[0] - reference[0]) < .0001f &&
                  std::abs(d.position[2] - reference[2]) < .0001f &&
                  std::abs(d.frame - reference_frame) < .0001f,
              "Original controller tick rate depended on host FPS");
    d.end(model);
  }
}
void checks() {
  Bytes exe(0x1f800);
  std::memcpy(exe.data(), "XBEH", 4);
  auto put = [&](size_t at, auto value) {
    std::memcpy(exe.data() + at, &value, sizeof(value));
  };
  put(0x104, 0x10000u);
  put(0x11c, 7u);
  put(0x120, 0x10180u);
  const uint32_t sections[][3]{
      {0x3c0000, 0x400, 0x2000},   {0x700000, 0x2400, 0x400},
      {0x1c0000, 0x2800, 0x9000},  {0x70000, 0xb800, 0x5000},
      {0x490000, 0x10800, 0x8000}, {0x510000, 0x18800, 0x6000},
      {0xd5000, 0x1e800, 0x1000}};
  for (size_t i = 0; i < 7; ++i) {
    put(0x180 + i * 56 + 4, sections[i][0]);
    put(0x180 + i * 56 + 12, sections[i][1]);
    put(0x180 + i * 56 + 16, sections[i][2]);
  }
  auto address = [&](uint32_t va) -> size_t {
    for (auto &section : sections)
      if (va >= section[0] && va - section[0] < section[2])
        return size_t(va - section[0]) + section[1];
    throw std::runtime_error("Invalid synthetic executable fixture");
  };
  size_t init = address(0x3c11d0);
  exe[init] = 0x56;
  exe[init + 1] = 0x6a;
  exe[init + 2] = 5;
  exe[init + 13] = 0xbe;
  put(init + 14, 0x700000u);
  for (uint32_t va : {0x1c30e0u, 0x74d23u}) {
    exe[address(va)] = 0xd8;
    exe[address(va) + 1] = 0x0d;
    put(address(va) + 2, 0x496b98u);
  }
  exe[address(0x1c891f)] = 0x68;
  put(address(0x1c8920), -.25f);
  put(address(0x496b98), .5f);
  put(address(0x514ac4), 8u);
  exe[address(0xd5995)] = 0xc7;
  exe[address(0xd5995) + 1] = 0x05;
  put(address(0xd599b), 300.f);
  for (uint32_t id : {2u, 11u}) {
    size_t row = address(0x700000 + id * 48);
    put(row, id);
    put(row + 4, 64.f);
    put(row + 8, 10.f);
    put(row + 12, 30.f);
    put(row + 36, -185.f);
    put(row + 40, int16_t(17));
  }
  auto decoded = decode_character_controls(exe);
  require(decoded.available && decoded.jump[0].launch == 185 &&
              decoded.jump[0].gravity == 8 && decoded.release_ticks == 5,
          "Source jump parameters were not read from the supplied executable");
  put(address(0x700000 + 2 * 48 + 36), -120.f);
  require(decode_character_controls(exe).jump[0].launch == 120,
          "Jump parameters were hardcoded instead of reading source data");
  exe[init + 13] = 0;
  require(!decode_character_controls(exe).available &&
              !decode_character_controls(std::span(exe).first(12)).available,
          "Unsupported/truncated executable supplied invented jump settings");
  auto m = actor();
  auto common = clip("a10_walk", -10);
  common.common = true;
  m.clips.push_back(common);
  auto mappings = CharacterDebug::resolve(m);
  require(mappings[CharacterDebug::Walk] == 1,
          "Common clip replaced an authored character clip");
  m.clips.erase(m.clips.begin() + 1);
  mappings = CharacterDebug::resolve(m);
  require(mappings[CharacterDebug::Walk] == 4,
          "Missing character action did not use a compatible common clip");
  m = actor();
  m.clips[0].frames = 1;
  auto shared_idle = clip("y00_stnd");
  shared_idle.common = true;
  m.clips.push_back(shared_idle);
  require(CharacterDebug::resolve(m)[CharacterDebug::Idle] == 5,
          "A static reference frame replaced a compatible common idle");
  m = actor();
  m.clips[1].frames = 5;
  auto gait = clip("a30_walk");
  m.clips.push_back(gait);
  require(CharacterDebug::resolve(m)[CharacterDebug::Walk] == 5,
          "A short walk transition was repeated as the gait loop");
  m.clips[5].encoding = 6;
  auto fallback = clip("a10_walk");
  fallback.common = true;
  m.clips.push_back(fallback);
  require(CharacterDebug::resolve(m)[CharacterDebug::Walk] == 6,
          "An older incompatible gait replaced the retargeted fallback");
  auto combat_model = actor();
  combat_model.clips.push_back(clip("g00_st_q"));
  combat_model.clips.push_back(clip("g01_st_q"));
  combat_model.clips.push_back(clip("i00_st_p"));
  combat_model.clips.push_back(clip("k10_st_m"));
  combat_model.clips.push_back(clip("j00_st_d"));
  combat_model.clips.push_back(clip("l10_sp_q"));
  combat_model.clips.push_back(clip("g03_reac"));
  combat_model.clips.push_back(clip("g40_jp_q"));
  CharacterDebug combat;
  combat.begin(combat_model, checker(), {});
  require(
      combat.moves.size() == 7 &&
          combat.attacks[CharacterDebug::Quick].size() == 3,
      "Source attacks were omitted or a reaction was classified as an attack");
  DebugInput attack;
  attack.attack = CharacterDebug::Quick;
  combat.update(combat_model, attack, .1, 0);
  require(combat.attack_clip == 5 && combat.frame > 0,
          "Quick attack input did not execute its source clip");
  combat.update(combat_model, attack, .1, 0);
  require(combat.attack_queue == std::vector<int>{6},
          "Repeated quick input did not buffer the next kombo move");
  for (int i = 0; i < 60; ++i)
    combat.update(combat_model, {}, 1. / 60, 0);
  require(combat.attack_clip == 6,
          "Buffered kombo failed to advance to its second source clip");
  for (int i = 0; i < 90; ++i)
    combat.update(combat_model, {}, 1. / 60, 0);
  require(combat.attack_clip < 0 && combat.action == CharacterDebug::Idle,
          "Attack end looped forever instead of returning to locomotion");
  combat.request_attack(CharacterDebug::Quick, true);
  combat.update(combat_model, {}, 0, 0);
  require(combat.attack_clip == 5 && combat.attack_queue == std::vector<int>{6},
          "Kombo button did not enqueue the complete source sequence");
  combat.reset();
  combat.request_attack(CharacterDebug::Quick);
  combat.update(combat_model, {}, 0, 0);
  combat.request_attack(CharacterDebug::Power);
  combat.update(combat_model, {}, 0, 0);
  require(combat.attack_clip == 5 && combat.attack_queue == std::vector<int>{7},
          "Mixed attack keys did not buffer a different attack group");
  combat.reset();
  combat.request_move(10);
  combat.update(combat_model, {}, .1, 0);
  require(combat.attack_clip == 10,
          "Move browser could not execute a special attack");
  combat.reset();
  combat.update(combat_model, {0, 0, false, true, true}, 1. / 60, 0);
  float height = combat.position[1];
  combat.request_attack(CharacterDebug::Quick);
  combat.update(combat_model, {0, 0, false, false, true}, 1. / 60, 0);
  require(combat.action == CharacterDebug::Jump && combat.attack_clip == 12 &&
              combat.position[1] > height,
          "Air attack lost its source variant or stopped jump physics");
  combat.end(combat_model);
  require(combat_model.clips.size() == 13 && combat.attack_clip < 0 &&
              combat.attack_queue.empty(),
          "Ending debug retained combat state or modified original clips");
  m = actor();
  CharacterDebug d;
  auto original = m.parts[0].bind;
  auto count = m.clips.size();
  d.begin(m, checker(), {});
  require(d.active && m.character_debug &&
              m.parts.back().role == MeshRole::DebugFloor,
          "Debug floor was not created");
  require(d.walk_speed > 0 && d.run_speed > d.walk_speed,
          "Walk/run speeds were not established");
  m.bounds();
  require(m.triangles == 1 && m.maximum[0] < 1,
          "Debug floor changed character framing or triangle counts");
  auto &floor = m.parts.back();
  require(std::abs((floor.vertices[1].x - floor.vertices[0].x) /
                       (floor.vertices[1].u - floor.vertices[0].u) -
                   2) < .0001f,
          "A 2-square source checker did not tile at one meter per square");
  require(floor.vertices.size() == 2400 && floor.vertices[0].x == -80,
          "Debug floor did not use bounded eight-meter tiles");
  auto normal_rate = actor(), fast_rate = actor();
  CharacterDebug normal_debug, fast_debug;
  normal_debug.begin(normal_rate, checker(), {});
  fast_debug.begin(fast_rate, checker(), {});
  normal_debug.locomotion_rate = 1;
  for (int i = 0; i < 12; ++i) {
    normal_debug.update(normal_rate, {0, 1, false, false}, 1. / 60, 0);
    fast_debug.update(fast_rate, {0, 1, false, false}, 1. / 60, 0);
  }
  require(std::abs(fast_debug.position[2] - normal_debug.position[2] * 2) <
                  .0001f &&
              std::abs(fast_debug.frame - normal_debug.frame * 2) < .0001f &&
              fast_rate.clips[1].fps == normal_rate.clips[1].fps,
          "Movement cadence did not scale travel and playback together");
  for (int i = 0; i < 60; ++i)
    d.update(m, {0, 1, false, false}, 1. / 60, 0);
  require(d.action == CharacterDebug::Walk && d.position[2] < -.5f,
          "Forward did not move away from the camera");
  float walk = d.velocity;
  for (int i = 0; i < 60; ++i)
    d.update(m, {1, 1, true, false}, 1. / 60, 0);
  require(d.action == CharacterDebug::Run && d.velocity > walk,
          "Shift did not select running");
  auto before = d.position;
  d.update(m, {}, 1. / 60, 0);
  require(d.action == CharacterDebug::Idle && d.position == before,
          "Releasing movement did not stop at idle");
  d.update(m, {0, 0, false, true}, 1. / 60, 0);
  require(d.action == CharacterDebug::Jump && d.position[1] > 0,
          "Jump did not take off");
  float top = 0;
  bool landed = false;
  for (int i = 0; i < 130; ++i) {
    d.update(m, {0, 0, false, false, true}, 1. / 60, 0);
    top = std::max(top, d.position[1]);
    landed |= d.action == CharacterDebug::Land;
  }
  require(top > .9f && landed && d.action == CharacterDebug::Idle &&
              d.position[1] == 0,
          "Jump did not complete its arc and landing transition");
  auto held_model = actor(), tap_model = actor();
  CharacterDebug held_debug, tap_debug;
  held_debug.begin(held_model, checker(), {});
  tap_debug.begin(tap_model, checker(), {});
  float held_top = 0, tap_top = 0;
  unsigned held_ticks = 0, tap_ticks = 0;
  for (unsigned i = 0; i < 60; ++i) {
    held_debug.update(held_model, {0, 0, false, i == 0, true}, 1. / 60, 0);
    tap_debug.update(tap_model, {0, 0, false, i == 0, i == 0}, 1. / 60, 0);
    held_top = std::max(held_top, held_debug.position[1]);
    tap_top = std::max(tap_top, tap_debug.position[1]);
    if (!held_ticks && held_debug.action != CharacterDebug::Jump)
      held_ticks = i + 1;
    if (!tap_ticks && tap_debug.action != CharacterDebug::Jump)
      tap_ticks = i + 1;
  }
  require(held_ticks == 48 && tap_ticks == 26 &&
              std::abs(held_top - 1116.f / 512) < .0001f &&
              std::abs(tap_top - 359.5f / 512) < .0001f,
          "Jump integration diverged from the original routine trace");
  auto air_model = actor();
  auto &air_profile = air_model.controls.jump[0];
  air_profile.launch = 185;
  air_profile.gravity = 8;
  air_profile.speed = 10;
  air_profile.maximum_speed = 30;
  air_profile.acceleration.fill(4);
  CharacterDebug air;
  air.begin(air_model, checker(), {});
  for (int i = 0; i < 6; ++i)
    air.update(air_model, {0, 1, false, i == 0, true}, 1. / 60, 0);
  require(std::abs(air.air_velocity[1] + 30) < .001f,
          "Air acceleration differed from the native routine trace");
  air.update(air_model, {0, 1, false, false, true}, 1. / 60, 0);
  require(std::abs(air.air_velocity[1] + 25.5f) < .001f,
          "Air speed limiting differed from the native routine trace");
  auto run_jump_model = actor();
  CharacterDebug run_jump;
  run_jump.begin(run_jump_model, checker(), {});
  for (int i = 0; i < 8; ++i)
    run_jump.update(run_jump_model, {0, 1, true, i == 0, i == 0}, 1. / 60, 0);
  require(!run_jump.shortened_jump &&
              std::abs(run_jump.vertical_velocity - 121.f / 512) < .0001f,
          "A running jump incorrectly used the standing short-hop rule");
  auto slow_jump_model = actor(), fast_jump_model = actor();
  CharacterDebug slow_jump, fast_jump;
  slow_jump.begin(slow_jump_model, checker(), {});
  fast_jump.begin(fast_jump_model, checker(), {});
  for (int i = 0; i < 15; ++i)
    slow_jump.update(slow_jump_model, {0, 0, false, i == 0, true}, 1. / 30, 0);
  for (int i = 0; i < 60; ++i)
    fast_jump.update(fast_jump_model, {0, 0, false, i == 0, true}, 1. / 120, 0);
  require(slow_jump.jump_ticks == fast_jump.jump_ticks &&
              std::abs(slow_jump.position[1] - fast_jump.position[1]) < .0001f,
          "Jump motion depended on rendering FPS");
  while (slow_jump.action == CharacterDebug::Jump)
    slow_jump.update(slow_jump_model, {0, 0, false, false, true}, 1. / 60, 0);
  slow_jump.update(slow_jump_model, {0, 0, false, true, true}, 1. / 60, 0);
  require(slow_jump.action == CharacterDebug::Jump,
          "A new jump press could not interrupt the landing state");
  auto unknown = actor();
  unknown.controls = {};
  CharacterDebug unavailable;
  unavailable.begin(unknown, checker(), {});
  unavailable.update(unknown, {0, 0, false, true, true}, 1. / 60, 0);
  require(unavailable.action == CharacterDebug::Idle,
          "Missing source jump parameters silently used an invented arc");
  auto pose = m.parts[0].vertices;
  d.update(m, {}, 0, 0);
  require(d.position == before, "A paused update changed position");
  d.reset();
  d.update(m, {1, 0, false, false}, 1., 0);
  require(d.position[0] < 0 && std::abs(d.position[2]) < .0001f,
          "Right movement did not follow the left-handed camera basis");
  d.reset();
  d.update(m, {0, 1, false, false}, .25, 1.570796327f);
  require(d.position[0] < 0 && std::abs(d.position[2]) < .0001f,
          "Movement ignored camera yaw");
  d.reset();
  d.update(m, {}, 0, 0);
  auto origin = d.position;
  d.update(m, {}, std::numeric_limits<double>::infinity(), 0);
  require(d.position == origin, "Invalid elapsed time moved the character");
  // Integrating constant input must preserve travel across rendering rates.
  auto low = actor(), high = actor();
  CharacterDebug a, b;
  a.begin(low, checker(), {});
  b.begin(high, checker(), {});
  for (int i = 0; i < 30; ++i)
    a.update(low, {0, 1, true, false}, 1. / 30, 0);
  for (int i = 0; i < 120; ++i)
    b.update(high, {0, 1, true, false}, 1. / 120, 0);
  require(std::abs(a.position[2] - b.position[2]) < .001f,
          "Movement depended on rendering rate");
  auto diagonal = actor();
  CharacterDebug diagonal_debug;
  diagonal_debug.begin(diagonal, checker(), {});
  for (int i = 0; i < 30; ++i)
    diagonal_debug.update(diagonal, {1, 1, true, false}, 1. / 30, 0);
  require(std::abs(std::hypot(diagonal_debug.position[0],
                              diagonal_debug.position[2]) -
                   std::abs(a.position[2])) < .001f,
          "Diagonal movement was faster");
  // Quaternion transitions preserve segment length rather than blending skinned
  // vertices.
  auto blend = actor();
  blend.clips[1].keys[0].pose[0].rotation[2] = 1024;
  blend.clips[1].keys[1].pose[0].rotation[2] = 1024;
  blend.pose(0, 0);
  auto initial = blend.parts[0].vertices[0];
  blend.pose(1, 0, false, false, 0, 0, 0, 0);
  auto zero = blend.parts[0].vertices[0];
  require(std::abs(zero.x - initial.x) < .0001f &&
              std::abs(zero.y - initial.y) < .0001f,
          "Transition zero did not retain the previous clip");
  blend.pose(1, 0, false, false, 0, 0, 0, .5f);
  auto middle = blend.parts[0].vertices[0];
  require(std::abs(std::hypot(middle.x, middle.y) - .5f) < .0001f,
          "Pose transition shortened a rigid bone segment");
  d.end(m);
  require(!d.active && !m.character_debug && m.parts.size() == 1 &&
              m.textures.empty() && m.clips.size() == count,
          "Disabling debug did not restore the source model");
  require(m.parts[0].vertices[0].x == original[0].x,
          "Disabling debug retained a world transform");
  // Actual GPU rendering must keep black fog enabled even if stage fog is off.
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                          D3D11_SDK_VERSION, &device, nullptr, &context));
  Renderer r(device.Get(), context.Get());
  r.grid = r.joints = r.bloom = r.fog = false;
  r.resize(128, 96);
  d.begin(m, checker(), {});
  m.parts[0].visible = false;
  Camera camera;
  camera.target = {0, .5f, 0};
  camera.yaw = 0;
  camera.pitch = .2f;
  camera.distance = 4;
  camera.target = {0, .5f, 0};
  camera.pitch = .2f;
  camera.radius = 1;
  r.upload(m);
  r.draw(m, camera);
  auto image = r.capture();
  uint64_t bright = 0;
  for (size_t i = 0; i < image.rgba.size(); i += 4)
    bright += image.rgba[i] + image.rgba[i + 1] + image.rgba[i + 2];
  require(bright > 10000, "Checker floor was not rendered");
  require(image.rgba[0] == 0 && image.rgba[1] == 0 && image.rgba[2] == 0,
          "Debug background did not stay black");
  r.lighting = false;
  r.draw(m, camera);
  auto unlit = r.capture();
  uint64_t unlit_brightness = 0;
  for (size_t i = 0; i < unlit.rgba.size(); i += 4)
    unlit_brightness += unlit.rgba[i] + unlit.rgba[i + 1] + unlit.rgba[i + 2];
  require(bright < unlit_brightness * .95 && bright > unlit_brightness * .7,
          "Debug floor did not follow the shared preview lighting");
  m.parts.back().role = MeshRole::Surface;
  r.draw(m, camera);
  auto untinted = r.capture();
  uint64_t untinted_brightness = 0;
  for (size_t i = 0; i < untinted.rgba.size(); i += 4)
    untinted_brightness +=
        untinted.rgba[i] + untinted.rgba[i + 1] + untinted.rgba[i + 2];
  require(unlit_brightness > untinted_brightness * .33 &&
              unlit_brightness < untinted_brightness * .37 &&
              m.textures.back().rgba == checker().rgba,
          "Floor diffuse tint changed source pixels or remained full bright");
  m.parts.back().role = MeshRole::DebugFloor;
  r.lighting = true;
  camera.target = {0, 100, 0};
  camera.pitch = 1.4f;
  r.draw(m, camera);
  auto fogged = r.capture();
  uint64_t distant_brightness = 0;
  for (size_t i = 0; i < fogged.rgba.size(); i += 4)
    distant_brightness +=
        fogged.rgba[i] + fogged.rgba[i + 1] + fogged.rgba[i + 2];
  require(distant_brightness == 0,
          "Forced black fog did not hide distant floor geometry");
  camera.distance = 4;
  camera.target = {0, .5f, 0};
  camera.pitch = .2f;
  require(r.pick(m, camera, 64, 80) == -1,
          "Generated floor intercepted character selection");
  // A surface one millimeter above the floor must win the depth test in
  // either draw order at native character scale, without biasing the actor.
  m.debug_meter = 512;
  for (auto &v : m.parts.back().vertices) {
    v.x *= 512;
    v.y *= 512;
    v.z *= 512;
  }
  Part pad;
  pad.opaque = pad.two_sided = true;
  pad.texture = int(m.textures.size());
  m.textures.push_back({1, 1, {240, 20, 20, 255}});
  const int corners[6][2]{{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
  for (auto &corner : corners)
    pad.vertices.push_back({corner[0] * 400.f, .512f, corner[1] * 400.f, 0, 0,
                            0x80808080, 0, 1, 0});
  m.parts.push_back(pad);
  camera.target = {0, 256, 0};
  camera.distance = 2304;
  camera.radius = camera.scene_radius = 512;
  camera.pitch = .24f;
  r.resize(320, 240);
  r.upload(m);
  r.draw(m, camera);
  auto floor_first = r.capture();
  std::swap(m.parts[1], m.parts[2]);
  r.upload(m);
  r.draw(m, camera);
  auto floor_last = r.capture();
  size_t surface_pixels = 0;
  for (size_t i = 0; i < floor_first.rgba.size(); i += 4) {
    bool first = floor_first.rgba[i] > floor_first.rgba[i + 1] * 2;
    bool last = floor_last.rgba[i] > floor_last.rgba[i + 1] * 2;
    require(first == last,
            "Separated floor and surface occlusion depended on draw order");
    surface_pixels += first;
  }
  require(surface_pixels > 1000, "Depth regression surface was not visible");
}
} // namespace
int main() {
  try {
    graph_checks();
    checks();
    std::cout << "Character debug mapping, input, movement, transition, floor, "
                 "fog and restore checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

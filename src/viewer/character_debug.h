#pragma once
#include "assets.h"

namespace shaolin::viewer {
struct DebugInput {
  float right = 0, forward = 0;
  bool run = false, jump = false;
  bool jump_held = false;
  int attack = -1;
  bool kombo = false;
  uint16_t buttons = 0;
};
struct CharacterDebug {
  enum Attack { Quick, Power, Launch, Throw, Special, AttackCount };
  enum Action { Idle, Walk, Run, Jump, Land, Count };
  std::array<int, Count> clips{-1, -1, -1, -1, -1};
  Action action = Idle;
  std::array<float, 3> position{};
  float heading = 0, velocity = 0, frame = 0;
  float walk_speed = 0, run_speed = 0;
  float locomotion_rate = 2;
  float vertical_velocity = 0;
  std::array<float, 2> air_velocity{};
  unsigned jump_ticks = 0;
  bool running_jump = false, shortened_jump = false;
  double jump_accumulator = 0;
  double action_time = 0, blend_time = 0;
  int previous_clip = -1;
  float previous_frame = 0;
  bool active = false;
  std::array<std::vector<int>, AttackCount> attacks;
  std::vector<int> moves, attack_queue;
  int attack_clip = -1, requested_move = -1, requested_attack = -1;
  bool requested_kombo = false;
  double attack_time = 0;
  double control_accumulator = 0;
  uint16_t previous_buttons = 0, latched_buttons = 0;
  bool latched_jump = false;
  uint32_t graph_cursor = 0;
  unsigned graph_age = 0;
  int graph_state = -1;
  std::vector<uint32_t> graph_queue;
  float attack_blend = .16f;
  void update_step(Model &model, const DebugInput &input, double seconds,
                   float camera_yaw, bool secondary);
  static const char *attack_name(int kind);
  void request_attack(int kind, bool kombo = false);
  void request_move(int clip);
  size_t original_parts = 0, original_textures = 0, original_clips = 0;
  void begin(Model &model, const Image &checker, std::vector<Clip> common);
  void end(Model &model);
  void reset();
  void update(Model &model, const DebugInput &input, double seconds,
              float camera_yaw, bool secondary = false);
  static const char *action_name(Action value);
  static std::array<int, Count> resolve(const Model &model);
};
} // namespace shaolin::viewer

#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace shaolin::viewer {
struct MusicCue {
  std::string name;
  float volume = 1;
};
struct MusicTrack {
  std::string name;
  uint32_t rate = 0, channels = 0, loop_begin = 0, loop_end = 0;
  float volume = 1;
  std::vector<int16_t> samples;
};
MusicCue decode_music_cue(std::span<const uint8_t> bytes);
std::string music_group(std::span<const uint8_t> sound_bank);
MusicTrack decode_adx(std::span<const uint8_t> bytes);

class MusicPlayer {
  struct Output;
  std::unique_ptr<Output> output_;
  MusicTrack track_;
  bool silent_, playing_ = false;
  float volume_ = 1;

public:
  explicit MusicPlayer(bool silent = false);
  ~MusicPlayer();
  void play(MusicTrack track);
  void stop();
  void set_volume(float volume);
  float volume() const { return volume_; }
  bool playing() const { return playing_; }
};
} // namespace shaolin::viewer

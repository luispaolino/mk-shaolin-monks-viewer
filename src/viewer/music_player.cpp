#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "music.h"
#include <cmath>
#include <stdexcept>
#include <windows.h>
#include <xaudio2.h>

namespace shaolin::viewer {
struct MusicPlayer::Output {
  IXAudio2 *engine = nullptr;
  IXAudio2MasteringVoice *master = nullptr;
  IXAudio2SourceVoice *voice = nullptr;
  bool com_initialized = false;
  Output() {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    com_initialized = SUCCEEDED(result);
    if (FAILED(result) && result != RPC_E_CHANGED_MODE)
      throw std::runtime_error("Cannot initialize stage music audio output");
  }
  ~Output() {
    if (voice)
      voice->DestroyVoice();
    if (master)
      master->DestroyVoice();
    if (engine)
      engine->Release();
    if (com_initialized)
      CoUninitialize();
  }
};
MusicPlayer::MusicPlayer(bool silent) : silent_(silent) {}
MusicPlayer::~MusicPlayer() { stop(); }
void MusicPlayer::set_volume(float volume) {
  if (!std::isfinite(volume) || volume < 0 || volume > 1)
    throw std::runtime_error("Invalid stage music volume");
  if (output_ && output_->voice &&
      FAILED(output_->voice->SetVolume(track_.volume * volume)))
    throw std::runtime_error("Cannot change stage music volume");
  volume_ = volume;
}
void MusicPlayer::stop() {
  if (output_ && output_->voice) {
    output_->voice->DestroyVoice();
    output_->voice = nullptr;
  }
  playing_ = false;
  track_ = {};
}
void MusicPlayer::play(MusicTrack track) {
  stop();
  if (track.samples.empty())
    return;
  if (!track.rate || (track.channels != 1 && track.channels != 2) ||
      track.samples.size() % track.channels ||
      track.samples.size() > UINT32_MAX / sizeof(int16_t) ||
      !std::isfinite(track.volume) || track.volume < 0 || track.volume > 1 ||
      track.loop_begin > track.loop_end ||
      track.loop_end > track.samples.size() / track.channels)
    throw std::runtime_error("Invalid music playback buffer");
  track_ = std::move(track);
  // Hidden validation must never initialize an audio device or produce sound.
  if (silent_) {
    playing_ = true;
    return;
  }
  auto check = [](HRESULT result) {
    if (FAILED(result))
      throw std::runtime_error("Stage music audio output is unavailable");
  };
  try {
    if (!output_)
      output_ = std::make_unique<Output>();
    if (!output_->engine)
      check(XAudio2Create(&output_->engine));
    if (!output_->master)
      check(output_->engine->CreateMasteringVoice(&output_->master));
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = WORD(track_.channels);
    format.nSamplesPerSec = track_.rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = WORD(track_.channels * 2);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    check(output_->engine->CreateSourceVoice(&output_->voice, &format));
    XAUDIO2_BUFFER buffer{};
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.AudioBytes = UINT32(track_.samples.size() * sizeof(int16_t));
    buffer.pAudioData = reinterpret_cast<const BYTE *>(track_.samples.data());
    buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
    if (track_.loop_end > track_.loop_begin) {
      buffer.LoopBegin = track_.loop_begin;
      buffer.LoopLength = track_.loop_end - track_.loop_begin;
    }
    check(output_->voice->SetVolume(track_.volume * volume_));
    check(output_->voice->SubmitSourceBuffer(&buffer));
    check(output_->voice->Start());
    playing_ = true;
  } catch (...) {
    stop();
    throw;
  }
}
} // namespace shaolin::viewer

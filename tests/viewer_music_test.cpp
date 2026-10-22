#define wmain viewer_entrypoint
#include "../src/viewer/main.cpp"
#undef wmain
#include <limits>
#include <thread>

namespace {
void require(bool good, const char *message) {
  if (!good)
    throw std::runtime_error(message);
}
template <class T> void put(sv::Bytes &b, size_t p, T value) {
  std::memcpy(b.data() + p, &value, sizeof(value));
}
void be(sv::Bytes &b, size_t p, uint32_t v, size_t n = 4) {
  for (size_t i = 0; i < n; ++i)
    b[p + i] = uint8_t(v >> (8 * (n - i - 1)));
}
void text(sv::Bytes &b, size_t p, const char *value) {
  std::memcpy(b.data() + p, value, std::strlen(value) + 1);
}
void rejects(auto action) {
  bool failed = false;
  try {
    action();
  } catch (const std::exception &) {
    failed = true;
  }
  require(failed, "Malformed music data was accepted");
}
sv::Bytes adx() {
  sv::Bytes b(64 + 4 * 18);
  be(b, 0, 0x8000, 2);
  be(b, 2, 60, 2);
  b[4] = 3;
  b[5] = 18;
  b[6] = 4;
  b[7] = 2;
  be(b, 8, 8000);
  be(b, 12, 33);
  be(b, 16, 500, 2);
  be(b, 18, 0x300, 2);
  be(b, 24, 1);
  be(b, 28, 1);
  be(b, 36, 32);
  std::memcpy(b.data() + 58, "(c)CRI", 6);
  for (size_t i = 0; i < 4; ++i) {
    be(b, 64 + i * 18, 100, 2);
    b[66 + i * 18] = (i & 1) ? 0xf1 : 0x1f;
  }
  return b;
}
sv::Bytes definitions() {
  sv::Bytes b(88);
  put(b, 20, 2u);
  put(b, 24, 2u);
  put(b, 32, 1u);
  put(b, 36, .5f);
  put(b, 60, 0u);
  put(b, 64, .6f);
  for (const char *s :
       {"MUS_SYNTH_ADX_MUS_INTRO_ADX", "MUS_SYNTH_ADX_MUS_TUNE_ADX"})
    b.insert(b.end(), s, s + std::strlen(s) + 1);
  return b;
}
void decoder_checks() {
  auto b = adx();
  auto pcm = sv::decode_adx(b);
  require(
      pcm.channels == 2 && pcm.rate == 8000 && pcm.samples.size() == 66 &&
          pcm.loop_begin == 1 && pcm.loop_end == 32,
      "ADX timing, stereo interleave, partial block or loop points changed");
  require(pcm.samples[0] == 100 && pcm.samples[1] == -100 &&
              pcm.samples[2] == 10 && pcm.samples[3] == -11,
          "ADX signed residuals or per-channel predictor history changed");
  auto version4 = b;
  std::fill(version4.begin() + 20, version4.begin() + 58, 0);
  be(version4, 18, 0x400, 2);
  be(version4, 24, 1000, 2);
  be(version4, 26, 1000, 2);
  be(version4, 28, uint16_t(-1000), 2);
  be(version4, 30, uint16_t(-1000), 2);
  require(sv::decode_adx(version4).samples[0] == 897 &&
              sv::decode_adx(version4).samples[1] == -898,
          "ADX version-four initial histories were ignored");
  auto short_data = b;
  short_data.pop_back();
  rejects([&] { sv::decode_adx(short_data); });
  auto bad = b;
  be(bad, 12, UINT32_MAX);
  rejects([&] { sv::decode_adx(bad); });
  bad = b;
  be(bad, 36, 34);
  rejects([&] { sv::decode_adx(bad); });
  bad = b;
  bad[19] = 8;
  rejects([&] { sv::decode_adx(bad); });
  rejects([] { sv::decode_adx({}); });
  auto cue = sv::decode_music_cue(definitions());
  require(cue.name == "mus_tune.adx" && cue.volume == .6f,
          "Base-intensity music cue was confused with its intro");
  auto bank = definitions();
  bank.pop_back();
  rejects([&] { sv::decode_music_cue(bank); });
  require(sv::decode_music_cue({}).name.empty(),
          "Empty music bank invented BGM");
  sv::MusicPlayer player(true);
  require(player.volume() == 1, "BGM volume should default to 100 percent");
  player.set_volume(.4f);
  player.play(std::move(pcm));
  require(player.playing() && player.volume() == .4f,
          "Starting BGM reset its volume");
  player.set_volume(0);
  require(player.playing() && player.volume() == 0,
          "Zero volume restarted or stopped the music timeline");
  player.set_volume(.7f);
  for (float bad : {-1.f, 1.1f, std::numeric_limits<float>::quiet_NaN(),
                    std::numeric_limits<float>::infinity()})
    rejects([&] { player.set_volume(bad); });
  require(player.volume() == .7f, "Invalid volume changed the previous level");
  player.stop();
  require(!player.playing() && player.volume() == .7f,
          "Stopping BGM lost the user's volume");
}
struct Fixture {
  fs::path path =
      fs::temp_directory_path() /
      ("shaolin-viewer-music-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  Fixture() {
    fs::create_directory(path);
    sv::Bytes wad(32 + 955 * 8);
    text(wad, 0, "PWF ");
    put(wad, 8, 2u);
    put(wad, 12, 955u);
    put(wad, 16, 0x100000u);
    auto add = [&](uint32_t id, const sv::Bytes &b) {
      put(wad, 32 + id * 8, uint32_t(wad.size()));
      put(wad, 36 + id * 8, uint32_t(b.size()));
      wad.insert(wad.end(), b.begin(), b.end());
    };
    for (uint32_t id : {904u, 924u, 944u}) {
      sv::Bytes group(64);
      text(group, 0,
           id == 944 ? "MAP_OTHER_AHX_FX_WIND_ADX"
                     : "MAP_SYNTH_AHX_FX_WIND_ADX");
      add(id, group);
    }
    add(906, definitions());
    auto audio = adx();
    sv::Bytes afs(304);
    text(afs, 0, "AFS");
    put(afs, 4, 1u);
    put(afs, 8, 64u);
    put(afs, 12, uint32_t(audio.size()));
    put(afs, 16, 256u);
    put(afs, 20, 48u);
    text(afs, 256, "mus_tune.adx");
    std::copy(audio.begin(), audio.end(), afs.begin() + 64);
    add(954, afs);
    std::ofstream file(path / "GAMEDATA.WAD", std::ios::binary);
    file.write(reinterpret_cast<const char *>(wad.data()), wad.size());
    require(bool(file), "Cannot write synthetic music archive");
  }
  ~Fixture() {
    std::error_code e;
    fs::remove(path / "GAMEDATA.WAD", e);
    fs::remove(path, e);
  }
};
void lifecycle_checks() {
  Fixture fixture;
  App app(true);
  require(app.stage_bgm, "Stage BGM should default to enabled");
  app.music.set_volume(.35f);
  app.source = std::make_unique<sv::Source>(fixture.path);
  app.assets = {{913, sv::Kind::Stage, "First", 0},
                {933, sv::Kind::Stage, "Shared area", 0},
                {953, sv::Kind::Stage, "No music", 0},
                {6029, sv::Kind::Character, "Character", 0}};
  app.model.stage = true;
  auto wait = [&] {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (app.pending_music.valid() &&
           std::chrono::steady_clock::now() < end) {
      app.poll_music();
      std::this_thread::yield();
    }
    require(!app.pending_music.valid(), "Stage BGM loading timed out");
  };
  for (int stage : {0, 1}) {
    app.selected = stage;
    app.start_music();
    wait();
    require(app.music.playing() && app.music_status == "mus_tune.adx" &&
                app.music.volume() == .35f,
            "Stage BGM did not resolve the area's original music bank");
  }
  app.stage_bgm = false;
  app.start_music();
  require(!app.music.playing() && !app.pending_music.valid(),
          "BGM checkbox did not stop playback");
  app.selected = 0;
  app.start_music();
  require(!app.pending_music.valid(), "Disabled BGM restarted on stage change");
  app.stage_bgm = true;
  app.start_music();
  wait();
  require(app.music.playing() && app.music.volume() == .35f,
          "BGM checkbox did not restart current stage music");
  app.selected = 2;
  app.start_music();
  wait();
  require(!app.music.playing() && app.music_status.find("No BGM") == 0,
          "Unmapped stage borrowed unrelated music");
  app.selected = 0;
  app.start_music();
  app.model.stage = false;
  app.selected = 3;
  app.start_music();
  require(!app.music.playing() && !app.pending_music.valid(),
          "Leaving stages did not cancel queued music");
}
} // namespace
int wmain() {
  try {
    decoder_checks();
    lifecycle_checks();
    std::cout << "ADX decoding, source music mapping, looping metadata, mute, "
                 "stage changes and silent output checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what();
    return 1;
  }
}

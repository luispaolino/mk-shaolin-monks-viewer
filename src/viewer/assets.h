#pragma once
#include "kernel/disc.h"
#include "music.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace shaolin::viewer {
using Bytes = std::vector<uint8_t>;
struct Image {
  uint32_t width = 0, height = 0;
  Bytes rgba;
};
struct Vertex {
  float x, y, z, u, v;
  uint32_t color = 0xffffffff;
  float nx = 0, ny = 0, nz = 0;
};
struct Influence {
  std::array<uint16_t, 4> bone{};
  std::array<float, 4> weight{};
  std::array<std::array<float, 3>, 4> local{};
};
struct Bone {
  std::string name;
  int parent = -1;
  std::array<float, 3> bind{}, rotation{}, position{};
  bool used = false;
};
struct Pose {
  std::array<float, 3> rotation{}, translation{};
  bool translated = false;
};
struct Key {
  float frame = 0;
  std::vector<Pose> pose;
};
struct Clip {
  std::string name;
  uint32_t frames = 0;
  float fps = 30;
  std::vector<Key> keys;
  std::vector<int> bones;
  bool common = false;
  uint16_t encoding = 11;
  float debug_ground_offset = 0;
  int source_action = -1;
};
struct ActionNode {
  int action = -1;
  int start = 0, blend = 0, earliest = 0, timeout = 0;
  uint32_t conditions = 0, conditions2 = 0;
  uint16_t pressed = 0, held = 0, released = 0, ignored = 0;
  uint32_t child = 0, sibling = 0;
  int predicate = 0;
};
inline bool action_buttons_match(const ActionNode &n, uint16_t buttons,
                                 uint16_t changed) {
  uint16_t edge = changed & buttons & ~n.ignored;
  return (!n.pressed || edge == n.pressed) && (buttons & n.held) == n.held &&
         !(buttons & n.released);
}
struct ActionGraph {
  std::vector<ActionNode> nodes;
  std::array<uint32_t, 27> roots{};
  bool available() const { return !nodes.empty() && roots[0] != 0; }
};
ActionGraph decode_action_graph(std::span<const uint8_t> graph,
                                std::span<const uint8_t> actions,
                                std::vector<Clip> &clips);
struct JumpProfile {
  float launch = 0, gravity = 0, speed = 0, maximum_speed = 0, drag = 0,
        turn = 0;
  std::array<float, 4> acceleration{};
};
struct CharacterControls {
  bool available = false;
  unsigned release_ticks = 0, landing_cancel_frame = 0;
  float position_step = 0, release_scale = 0;
  float air_limit_drag = 0;
  float ground_turn = 0;
  std::array<JumpProfile, 2> jump;
};
CharacterControls
decode_character_controls(std::span<const uint8_t> executable);
float advance_animation_frame(const Clip &clip, float frame, double seconds,
                              float rate = 1);
struct SceneryKey {
  float frame = 0;
  std::array<float, 3> translation{}, scale{1, 1, 1};
  std::array<float, 4> rotation{0, 0, 0, 1};
};
struct SceneryTrack {
  uint32_t object = 0, parent = 0, frames = 0, playback = 0, loop_start = 0;
  std::vector<SceneryKey> keys;
};
struct SceneryClip {
  std::string name;
  uint32_t group = 0;
  std::vector<SceneryTrack> tracks;
  float duration = 10;
};
enum class BlendMode { Alpha, Additive, Multiply, Subtractive };
enum class MeshRole {
  Surface,
  Sky,
  Hidden,
  Camera,
  ShadowReceiver,
  ShadowOverlay,
  LightShaft,
  SolidEffect,
  FlickerSurface,
  ParticleEmitter,
  DebugFloor
};
struct ParticleCommand {
  uint32_t texture = 0, texture_end = 0, texture_animation = 0;
  uint32_t maximum = 0, multiple = 0, flags = 0, emitter_flags = 0;
  float life = 0, delay = 0, start_size = 0, end_size = 0, speed = 0,
        rotation_speed = 0, dev_origin = 0, dev_angle = 0, dev_size = 0,
        dev_life = 0, dev_speed = 0, dev_rotation = 0, dev_scale = 0,
        dev_fade = 0, weight = 0, lerp_delay = 0, ease_in = 0, bounce = 0,
        friction = 0;
  std::array<float, 4> color{}, end_color{};
};
struct ParticleLibrary {
  uint32_t resource = 0;
  std::vector<ParticleCommand> commands;
  std::vector<Image> textures;
};
struct ParticleEmitter {
  size_t part = 0, anchor = 0, command = 0;
  uint32_t seed = 0;
  bool inferred = true;
  size_t alive = 0;
};
ParticleCommand decode_particle(std::span<const uint8_t> bytes);
const char *mesh_role_name(MeshRole role);
struct FogPreset {
  std::array<float, 3> color{};
  float start = 0, end = 0, minimum = 0, maximum = 0;
  bool active() const { return start != end && (minimum > 0 || maximum > 0); }
};
struct Atmosphere {
  bool available = false;
  std::array<FogPreset, 5> fog;
};
struct MaterialChannel {
  int texture = -1;
  std::array<float, 2> offset{}, scale{1, 1};
};
struct StageLight {
  uint32_t type = 0, id = 0;
  bool enabled = true, affects_meshes = true;
  std::array<float, 3> position{}, direction{}, color{};
  float near_range = 0, far_range = 0;
  float inner_angle = 0, outer_angle = 0;
};
struct Part {
  std::string name;
  std::vector<Vertex> vertices, bind;
  std::vector<Influence> skin;
  int texture = -1;
  MaterialChannel diffuse, reflection, gloss;
  uint32_t material_flags = 3;
  bool source_material = false, prelit = false;
  bool visible = true;
  bool opaque = false;
  bool helper = false;
  bool sky = false;
  bool emissive = false;
  bool texture_alpha = true;
  bool inferred_solid_alpha = false;
  bool two_sided = false, legacy_two_sided = false;
  MeshRole role = MeshRole::Surface;
  uint64_t render_flags = 0, object_flags = 0;
  BlendMode blend = BlendMode::Alpha;
  bool depth_write = true;
  float alpha_cutoff = .15f;
  bool flicker = false;
  std::array<float, 3> flicker_settings{0, 100, 1};
  float opacity = 1;
  uint32_t object = 0;
  uint32_t mesh_id = UINT32_MAX;
  std::array<float, 3> scene_position{}, scene_rotation{};
  std::array<float, 2> uv_speed{}, uv_offset{};
  std::string scene_group;
  std::vector<Vertex> scene_local;
  std::vector<size_t> repaired_faces;
};
struct JointDynamics {
  bool available = false;
  float stiffness = 0, damping = 0, mass = 0, gravity = 0, stretch = 0;
  float tension_scale = 0, damping_scale = 0, gravity_scale = 0,
        stretch_scale = 0, hz = 0;
};
JointDynamics decode_joint_dynamics(std::span<const uint8_t> executable);
struct Model {
  std::string name;
  std::vector<Part> parts;
  std::vector<Image> textures;
  std::vector<Bone> bones;
  std::vector<Clip> clips;
  std::vector<SceneryClip> scenery;
  std::vector<StageLight> lights;
  std::string scenery_status;
  std::shared_ptr<const ParticleLibrary> particle_library;
  std::vector<ParticleEmitter> particle_emitters;
  std::string particle_status;
  size_t particle_texture_base = 0;
  std::vector<int> particle_sprites;
  bool particles_enabled = true;
  std::array<float, 3> origin{};
  std::string animation_status;
  std::array<float, 3> minimum{}, maximum{};
  std::array<float, 3> grid_origin{};
  float grid_radius = 1;
  size_t triangles = 0;
  size_t unsupported = 0;
  size_t authored_normals = 0, generated_normals = 0;
  bool stage = false;
  bool character_debug = false;
  float debug_meter = 512;
  CharacterControls controls;
  ActionGraph action_graph;
  JointDynamics joint_dynamics;
  std::array<float, 3> joint_translation{}, secondary_translation{};
  float joint_heading = 0, secondary_heading = 0;
  double secondary_accumulator = 0;
  std::vector<std::array<float, 3>> secondary_velocity;
  Atmosphere atmosphere;
  std::string atmosphere_status;
  std::array<float, 3> sky_origin{};
  size_t repaired_triangles = 0;
  bool legacy_winding = false;
  bool winding_repair = true;
  std::vector<std::array<float, 3>> secondary_positions, secondary_previous,
      secondary_rest;
  int secondary_clip = -2;
  float secondary_frame = 0;
  size_t secondary_joints() const;
  size_t repair_winding();
  void set_winding_repair(bool enabled);
  void flip_object(size_t selected);
  void prepare_materials();
  void bounds();
  void prepare_normals();
  bool same_object(size_t first, size_t second) const;
  bool object_bounds(size_t selected, std::array<float, 3> &lo,
                     std::array<float, 3> &hi) const;
  void pose(int clip, float frame, bool root_motion = false,
            bool secondary = false, float seconds = 0, int from_clip = -1,
            float from_frame = 0, float transition = 1,
            bool rebase_height = false, bool loop = false);
  void bind_scenery();
  void animate_scenery(int clip, double seconds, bool textures = true,
                       bool meshes = true);
  size_t scrolling_parts() const;
  size_t moving_parts() const;
  void bind_particles(std::shared_ptr<const ParticleLibrary> library);
  size_t add_particle_emitter(size_t anchor, size_t command,
                              bool inferred = false);
  void set_particle_command(size_t emitter, size_t command);
  void animate_particles(double seconds, const std::array<float, 3> &right,
                         const std::array<float, 3> &up);
};
enum class Kind { Character, Environment, Prop, Texture, Stage, ConceptArt };
enum class ArtGroup {
  None,
  Characters,
  Environments,
  Storyboards,
  DesignStudies
};
struct ArtPage {
  ArtGroup group = ArtGroup::None;
  uint32_t number = 0;
  uint32_t thumbnail_id = UINT32_MAX, thumbnail_index = 0;
};
ArtPage concept_art_page(uint32_t resource_id);
const char *art_group_name(ArtGroup group);
struct Asset {
  uint32_t id;
  Kind kind;
  std::string name;
  uint32_t size;
  uint32_t animation_id = UINT32_MAX;
  bool shared_animation = false;
  uint32_t stage_id = 0;
  ArtPage art;
};
Bytes unpack(std::span<const uint8_t> bytes, std::string *name = nullptr);
std::vector<Bytes> members(std::span<const uint8_t> bytes);
Image decode_xpr(std::span<const uint8_t> bytes);
bool map_animation_bones(Model &model, std::span<const uint8_t> executable,
                         uint32_t mesh = UINT32_MAX);
Model decode_model(std::span<const uint8_t> bytes, bool stage = false);
Atmosphere decode_atmosphere(std::span<const uint8_t> bytes);
std::vector<Clip> decode_animations(std::span<const uint8_t> bytes);
std::vector<SceneryClip> decode_scenery(std::span<const uint8_t> bytes);
void export_fbx(const Model &model, const std::filesystem::path &path);
void save_png(const Image &image, const std::filesystem::path &path);
class Source {
  std::unique_ptr<kernel::Disc> disc_;
  std::filesystem::path folder_;
  uint32_t span_ = 0;
  struct Record {
    uint64_t offset;
    uint32_t size;
  };
  std::vector<Record> records_;
  Bytes executable_;
  mutable std::shared_ptr<const ParticleLibrary> particle_library_;
  mutable bool particle_library_scanned_ = false;
  mutable std::shared_ptr<const std::vector<Clip>> common_motion_;
  mutable Image checker_texture_;
  mutable bool music_scanned_ = false;
  mutable std::map<std::string, Record> music_files_;
  mutable std::map<uint32_t, MusicCue> stage_music_;
  Bytes read(uint64_t offset, uint32_t length) const;
  std::shared_ptr<const ParticleLibrary> particle_library() const;

public:
  explicit Source(const std::filesystem::path &path);
  Bytes load(uint32_t id, std::string *name = nullptr) const;
  std::vector<Asset> catalog(const std::atomic_bool *cancel = nullptr) const;
  Model model(const Asset &asset) const;
  Image image(const Asset &asset, bool thumbnail = false) const;
  std::vector<Clip> common_motion(const Model &model) const;
  Image checker_texture() const;
  MusicTrack stage_music(const Asset &asset) const;
  size_t count() const { return records_.size(); }
};
} // namespace shaolin::viewer

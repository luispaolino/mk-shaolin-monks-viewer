#include "renderer.h"
#include <array>
#include <iostream>

using namespace shaolin::viewer;
namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
Part quad(float x, float z, int texture, float half = .8f) {
  Part part;
  part.texture = texture;
  part.vertices = {{x - half, -half, z, 0, 0}, {x + half, -half, z, 1, 0},
                   {x + half, half, z, 1, 1},  {x - half, -half, z, 0, 0},
                   {x + half, half, z, 1, 1},  {x - half, half, z, 0, 1}};
  return part;
}
std::array<int, 2> project(const Camera &camera, float x, float y, float z,
                           float aspect = 1) {
  using namespace DirectX;
  auto point = XMVector3TransformCoord(XMVectorSet(x, y, z, 1),
                                       camera.view_projection(aspect));
  return {int((XMVectorGetX(point) * .5f + .5f) * 128),
          int((.5f - XMVectorGetY(point) * .5f) * 128)};
}
std::array<int, 3> pixel(const Image &image, std::array<int, 2> p) {
  size_t offset = (size_t(p[1]) * image.width + p[0]) * 4;
  return {image.rgba[offset], image.rgba[offset + 1], image.rgba[offset + 2]};
}
void checks() {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                          D3D11_SDK_VERSION, &device, nullptr, &context));
  Renderer renderer(device.Get(), context.Get());
  renderer.resize(128, 128);
  renderer.grid = renderer.joints = renderer.lighting = false;
  Camera camera;
  camera.pitch = 0;
  Model model;
  model.textures = {{1, 1, {40, 80, 120, 255}}, {1, 1, {200, 100, 50, 255}}};
  model.parts = {quad(0, 0, 0, 1.1f), quad(0, 1, 1, .6f)};
  renderer.upload(model);
  require(renderer.pick(model, camera, 64, 64) == 1,
          "Picking did not return the nearest visible surface");
  require(renderer.pick(model, camera, 0, 0) == -1 &&
              renderer.pick(model, camera, -1, 64) == -1 &&
              renderer.pick(model, camera, 128, 64) == -1,
          "Empty or out-of-viewport space picked an object");
  model.parts[1].visible = false;
  require(renderer.pick(model, camera, 64, 64) == 0,
          "A hidden object blocked picking");
  model.parts[1].visible = true;
  model.textures[1].rgba[3] = 0;
  renderer.upload(model);
  require(renderer.pick(model, camera, 64, 64) == 0,
          "Picking ignored a transparent texture hole");
  model.parts[1].opaque = true;
  require(renderer.pick(model, camera, 64, 64) == 1,
          "Picking did not honor an opaque character material");
  model.parts[1].opaque = false;
  model.textures[1].rgba[3] = 255;
  renderer.upload(model);
  for (auto &v : model.parts[1].vertices)
    v.x += 1;
  renderer.update_pose(model);
  auto moved = project(camera, 1, 0, 1);
  require(renderer.pick(model, camera, 64, 64) == 0 &&
              renderer.pick(model, camera, float(moved[0]), float(moved[1])) ==
                  1,
          "Picking did not follow the current animated geometry");
  model.parts = {quad(-1, 0, 0), quad(1, 0, 1)};
  model.parts[0].mesh_id = model.parts[1].mesh_id = 7;
  model.bounds();
  camera.frame(model);
  auto left = project(camera, -1, 0, 0), right = project(camera, 1, 0, 0);
  renderer.upload(model);
  renderer.draw(model, camera);
  auto original = renderer.capture();
  renderer.selected_part = 0;
  renderer.draw(model, camera);
  auto highlight = renderer.capture();
  require(pixel(original, left) != pixel(highlight, left) &&
              pixel(original, right) != pixel(highlight, right),
          "Selecting a mesh did not highlight all of its material batches");
  auto color = pixel(highlight, left);
  require(std::abs(color[0] - (40 * .62f + 255 * .38f)) < 2 &&
              std::abs(color[1] - (80 * .62f + 229.5f * .38f)) < 2 &&
              std::abs(color[2] - 120 * .62f) < 2,
          "Selection did not apply a semitransparent yellow overlay");
  require(renderer.pick(model, camera, float(left[0]), float(left[1])) == 0,
          "The highlight changed the picked object ID");
  renderer.selected_part = -1;
  renderer.draw(model, camera);
  require(original.rgba == renderer.capture().rgba,
          "Clearing selection changed the underlying mesh or material");
  model.parts[0].mesh_id = model.parts[1].mesh_id = UINT32_MAX;
  renderer.selected_part = 0;
  renderer.draw(model, camera);
  require(pixel(original, right) == pixel(renderer.capture(), right),
          "Unidentified meshes were grouped into one selection");
  // Focus uses current geometry from every visible batch in the object and
  // respects narrow viewports without changing the orbit orientation.
  model.parts[0].mesh_id = model.parts[1].mesh_id = 7;
  camera.scene_radius = 10000;
  camera.yaw = .7f;
  camera.pitch = .3f;
  require(camera.frame_object(model, 0, .4f) && camera.target[0] == 0 &&
              camera.target[1] == 0 && camera.yaw == .7f &&
              camera.pitch == .3f && camera.scene_radius == 10000,
          "Focus omitted an object batch or reset the scene/orbit state");
  for (const auto &part : model.parts)
    for (const auto &v : part.vertices) {
      using namespace DirectX;
      auto p = XMVector3TransformCoord(XMVectorSet(v.x, v.y, v.z, 1),
                                       camera.view_projection(.4f));
      require(std::abs(XMVectorGetX(p)) < 1 && std::abs(XMVectorGetY(p)) < 1,
              "Focus clips the selected object in a narrow viewport");
    }
  model.parts[1].visible = false;
  require(camera.frame_object(model, 0, 1) &&
              std::abs(camera.target[0] + 1) < .001f,
          "Focus included a hidden batch");
  auto target = camera.target;
  require(!camera.frame_object(model, 999, 1) && camera.target == target,
          "Invalid focus changed the camera");
  context->ClearState();
}
} // namespace
int main() {
  try {
    checks();
    std::cout << "Hidden WARP nearest/hidden/alpha/animated picking, grouped "
                 "yellow highlighting, deselection and selected-object framing "
                 "passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

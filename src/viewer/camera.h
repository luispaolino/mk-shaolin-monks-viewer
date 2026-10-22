#pragma once
#include "assets.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

namespace shaolin::viewer {
struct Camera {
  float yaw = 0, pitch = .12f, distance = 4, fov = 45;
  std::array<float, 3> target{};
  float radius = 1;
  float scene_radius = 1;
  void animate_particles(Model &model, double seconds) const {
    float sy = std::sin(yaw), cy = std::cos(yaw);
    float sp = std::sin(pitch), cp = std::cos(pitch);
    model.animate_particles(seconds, {-cy, 0, sy}, {-sy * sp, cp, -cy * sp});
  }
  void frame(const Model &model) {
    for (int k = 0; k < 3; ++k)
      target[k] = (model.minimum[k] + model.maximum[k]) * .5f;
    radius = std::max({model.maximum[0] - model.minimum[0],
                       model.maximum[1] - model.minimum[1],
                       model.maximum[2] - model.minimum[2], .01f}) *
             .5f;
    if (model.stage) {
      float squared = 0;
      for (int k = 0; k < 3; ++k) {
        float extent = (model.maximum[k] - model.minimum[k]) * .5f;
        squared += extent * extent;
      }
      radius = std::max(std::sqrt(squared), .01f);
    }
    distance = radius * 3.4f;
    scene_radius = radius;
  }
  bool frame_object(const Model &model, size_t selected, float aspect) {
    std::array<float, 3> lo, hi;
    if (!model.object_bounds(selected, lo, hi))
      return false;
    float squared = 0;
    for (int k = 0; k < 3; ++k) {
      target[k] = (lo[k] + hi[k]) * .5f;
      float extent = (hi[k] - lo[k]) * .5f;
      squared += extent * extent;
    }
    radius = std::max(std::sqrt(squared), .005f);
    float tangent = std::tan(fov * 3.141592654f / 360);
    float angle = std::atan(tangent * std::min(1.f, std::max(aspect, .01f)));
    distance = radius / std::sin(angle) * 1.1f;
    return true;
  }
  DirectX::XMMATRIX view_projection(float aspect,
                                    float near_minimum = 0) const {
    using namespace DirectX;
    auto focus = XMVectorSet(target[0], target[1], target[2], 1);
    auto eye =
        focus + XMVectorSet(std::sin(yaw) * std::cos(pitch) * distance,
                            std::sin(pitch) * distance,
                            std::cos(yaw) * std::cos(pitch) * distance, 0);
    return XMMatrixLookAtLH(eye, focus, XMVectorSet(0, 1, 0, 0)) *
           XMMatrixPerspectiveFovLH(
               XMConvertToRadians(fov), aspect,
               // Keep depth precision when dollying away after focusing a
               // tiny object inside a large stage.
               std::max(
                   {radius * .001f, distance * .0001f, .00001f, near_minimum}),
               std::max(distance + std::max(radius, scene_radius) * 200, 1.f));
  }
  void orbit(float x, float y) {
    yaw += x * .008f;
    pitch = std::clamp(pitch + y * .008f, -1.55f, 1.55f);
  }
  void pan(float x, float y, float viewport_height) {
    float scale = 2 * distance * std::tan(fov * 3.141592654f / 360) /
                  std::max(viewport_height, 1.f);
    float sy = std::sin(yaw), cy = std::cos(yaw);
    float sp = std::sin(pitch), cp = std::cos(pitch);
    // Use the screen's right/up basis for the left-handed view. This keeps
    // dragging aligned with the image even when the camera is tilted.
    target[0] += (x * cy - y * sy * sp) * scale;
    target[1] += y * cp * scale;
    target[2] += (-x * sy - y * cy * sp) * scale;
  }
  void dolly(float pixels) {
    distance =
        std::clamp(distance * std::exp(std::clamp(pixels * .008f, -3.f, 3.f)),
                   radius * .02f, radius * 10000.f);
  }
  void zoom(float wheel) { dolly(-wheel * 10); }
};
} // namespace shaolin::viewer

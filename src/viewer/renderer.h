#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "bloom.h"
#include "camera.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <memory>
#include <stdexcept>
#include <windows.h>
#include <wrl/client.h>
namespace shaolin::viewer {
using Microsoft::WRL::ComPtr;
inline void check(HRESULT h) {
  if (FAILED(h))
    throw std::runtime_error("Direct3D failure: " +
                             std::to_string(uint32_t(h)));
}
class Renderer {
  struct Batch {
    ComPtr<ID3D11Buffer> buffer;
    UINT count;
    int texture;
    size_t part;
    DirectX::XMFLOAT3 center{};
  };
  ComPtr<ID3D11VertexShader> vs_;
  ComPtr<ID3D11PixelShader> ps_;
  ComPtr<ID3D11InputLayout> layout_;
  ComPtr<ID3D11Buffer> constants_;
  ComPtr<ID3D11SamplerState> sampler_;
  std::array<ComPtr<ID3D11SamplerState>, 4> material_samplers_;
  ComPtr<ID3D11RasterizerState> solid_, wire_, culled_;
  ComPtr<ID3D11DepthStencilState> depth_, depth_read_, depth_none_;
  ComPtr<ID3D11BlendState> blend_, additive_, multiply_, subtractive_;
  std::vector<ComPtr<ID3D11ShaderResourceView>> images_;
  std::vector<ComPtr<ID3D11ShaderResourceView>> alpha_images_;
  std::vector<Batch> batches_;
  ComPtr<ID3D11Texture2D> color_, depth_texture_;
  ComPtr<ID3D11Texture2D> pick_pixel_;
  ComPtr<ID3D11RenderTargetView> target_;
  ComPtr<ID3D11DepthStencilView> depth_view_;
  ComPtr<ID3D11ShaderResourceView> view_;
  uint32_t width_ = 0, height_ = 0;
  ID3D11Device *device_;
  ID3D11DeviceContext *context_;
  std::unique_ptr<BloomPreview> bloom_;
  ComPtr<ID3D11Texture2D> shadow_texture_;
  ComPtr<ID3D11DepthStencilView> shadow_depth_;
  ComPtr<ID3D11ShaderResourceView> shadow_view_;
  ComPtr<ID3D11SamplerState> shadow_sampler_;
  ComPtr<ID3D11RasterizerState> shadow_raster_;
  struct Constants {
    DirectX::XMFLOAT4X4 matrix;
    DirectX::XMFLOAT4 flags;
    DirectX::XMFLOAT4 uv;
    DirectX::XMFLOAT4 highlight;
    DirectX::XMFLOAT4 fog_color, fog_range, eye, sky;
    DirectX::XMFLOAT4 effect;
    DirectX::XMFLOAT4X4 light_matrix;
    DirectX::XMFLOAT4 light, shadow;
    DirectX::XMFLOAT4 diffuse_uv, reflection_uv, gloss_uv, channels;
    DirectX::XMFLOAT4 camera_right, camera_up, camera_forward;
    std::array<DirectX::XMFLOAT4, 6> light_position{}, light_color{},
        light_range{}, light_axis{};
  };
  static DirectX::XMFLOAT3 center(std::span<const Vertex> vertices) {
    if (vertices.empty())
      return {};
    DirectX::XMFLOAT3 lo{vertices[0].x, vertices[0].y, vertices[0].z}, hi = lo;
    for (const auto &v : vertices) {
      lo.x = std::min(lo.x, v.x);
      lo.y = std::min(lo.y, v.y);
      lo.z = std::min(lo.z, v.z);
      hi.x = std::max(hi.x, v.x);
      hi.y = std::max(hi.y, v.y);
      hi.z = std::max(hi.z, v.z);
    }
    return {(lo.x + hi.x) * .5f, (lo.y + hi.y) * .5f, (lo.z + hi.z) * .5f};
  }
  ComPtr<ID3D11ShaderResourceView> texture(const Image &img,
                                           bool mipmaps = true) {
    if (img.rgba.empty())
      return {};
    D3D11_TEXTURE2D_DESC d{};
    d.Width = img.width;
    d.Height = img.height;
    UINT support = 0;
    check(device_->CheckFormatSupport(DXGI_FORMAT_R8G8B8A8_UNORM, &support));
    mipmaps &= (img.width > 1 || img.height > 1) &&
               (support & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN) != 0;
    d.MipLevels = mipmaps ? 0 : 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = mipmaps ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
    d.BindFlags =
        D3D11_BIND_SHADER_RESOURCE | (mipmaps ? D3D11_BIND_RENDER_TARGET : 0);
    d.MiscFlags = mipmaps ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
    D3D11_SUBRESOURCE_DATA s{img.rgba.data(), img.width * 4, 0};
    ComPtr<ID3D11Texture2D> tex;
    check(device_->CreateTexture2D(&d, mipmaps ? nullptr : &s, &tex));
    if (mipmaps)
      context_->UpdateSubresource(tex.Get(), 0, nullptr, img.rgba.data(),
                                  img.width * 4, 0);
    ComPtr<ID3D11ShaderResourceView> out;
    check(device_->CreateShaderResourceView(tex.Get(), nullptr, &out));
    if (mipmaps)
      context_->GenerateMips(out.Get());
    return out;
  }
  static DirectX::XMFLOAT4 channel_uv(const MaterialChannel &c) {
    return {c.offset[0], c.offset[1], c.scale[0], c.scale[1]};
  }
  void stage_lights(Constants &constants, const Model &m, const Batch &batch) {
    constants.light_position.fill({});
    constants.light_color.fill({});
    constants.light_range.fill({});
    constants.light_axis.fill({});
    if (!m.stage || !source_lights || !lighting || m.parts[batch.part].prelit ||
        m.parts[batch.part].emissive || m.parts[batch.part].sky)
      return;
    std::vector<std::pair<float, const StageLight *>> nearby;
    size_t directional = 0;
    auto set = [&](size_t slot, const StageLight &light) {
      constants.light_position[slot] = {light.position[0], light.position[1],
                                        light.position[2], float(light.type)};
      constants.light_color[slot] = {light.color[0], light.color[1],
                                     light.color[2], 0};
      constants.light_range[slot] = {
          light.near_range, light.far_range,
          light.far_range > light.near_range
              ? 1 / (light.far_range - light.near_range)
              : 0,
          std::cos(light.inner_angle)};
      using namespace DirectX;
      auto axis = XMVector3Normalize(XMVectorSet(
          light.direction[0], light.direction[1], light.direction[2], 0));
      XMStoreFloat4(&constants.light_axis[slot], axis);
      constants.light_axis[slot].w = std::cos(light.outer_angle);
    };
    for (const auto &light : m.lights) {
      if (!light.enabled || !light.affects_meshes)
        continue;
      if (light.type == 4) {
        if (directional < 2)
          set(4 + directional++, light);
      } else if (light.type == 1 || light.type == 2) {
        float x = light.position[0] - batch.center.x,
              y = light.position[1] - batch.center.y,
              z = light.position[2] - batch.center.z;
        float distance = std::sqrt(x * x + y * y + z * z);
        float influence =
            std::clamp((light.far_range - distance) /
                           std::max(1e-5f, light.far_range - light.near_range),
                       0.f, 1.f);
        if (influence > 0)
          nearby.push_back(
              {influence *
                   std::max({light.color[0], light.color[1], light.color[2]}),
               &light});
      }
    }
    std::stable_sort(
        nearby.begin(), nearby.end(),
        [](const auto &a, const auto &b) { return a.first > b.first; });
    for (size_t i = 0; i < std::min<size_t>(4, nearby.size()); ++i)
      set(i, *nearby[i].second);
  }
  void add(std::span<const Vertex> vertices, int image, size_t part) {
    if (vertices.empty())
      return;
    D3D11_BUFFER_DESC d{};
    d.ByteWidth = UINT(vertices.size_bytes());
    d.Usage = D3D11_USAGE_DYNAMIC;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA s{vertices.data(), 0, 0};
    Batch b{{}, UINT(vertices.size()), image, part, center(vertices)};
    check(device_->CreateBuffer(&d, &s, &b.buffer));
    batches_.push_back(std::move(b));
  }

public:
  ComPtr<ID3D11ShaderResourceView> image_view(const Image &image) {
    return texture(image);
  }
  bool grid = true, wireframe = false, textures = true, lighting = true,
       vertex_colors = false, joints = true, all_joints = false,
       cull_backfaces = true;
  bool fog = true, sky = true, stage_background = true,
       custom_background = false;
  bool shadows = true;
  bool source_lights = true, light_gizmos = false;
  float light_azimuth = -34, light_elevation = 48;
  bool bloom = true;
  float bloom_threshold = 0, bloom_strength = .8f, bloom_radius = 1.5f;
  int fog_preset = 0;
  std::array<float, 3> background_color{};
  int selected_bone = -1;
  int selected_part = -1;
  explicit Renderer(ID3D11Device *d, ID3D11DeviceContext *c)
      : device_(d), context_(c) {
    constexpr const char *shader = R"(
cbuffer View:register(b0){row_major float4x4 vp;float4 flags;float4 uvOffset;float4 highlight;float4 fogColor;float4 fogRange;float4 eye;float4 sky;float4 effect;row_major float4x4 lightVP;float4 light;float4 shadow;float4 diffuseUV;float4 reflectionUV;float4 glossUV;float4 channels;float4 cameraRight;float4 cameraUp;float4 cameraForward;float4 lightPosition[6];float4 lightColor[6];float4 lightRange[6];float4 lightAxis[6];};
struct Input{float3 p:POSITION;float2 uv:TEXCOORD;float4 c:COLOR;float3 n:NORMAL;};
struct Output{float4 p:SV_Position;float2 uv:TEXCOORD;float4 c:COLOR;float3 n:TEXCOORD1;float depth:TEXCOORD2;float4 shadowPosition:TEXCOORD3;float3 world:TEXCOORD4;float2 originalUV:TEXCOORD5;};
Output VS(Input i){Output o;float3 p=i.p;if(sky.w>.5)p+=eye.xyz-sky.xyz;o.p=mul(float4(p,1),vp);o.depth=o.p.w;if(sky.w>.5)o.p.z=o.p.w;o.shadowPosition=mul(float4(p,1),lightVP);o.uv=i.uv*diffuseUV.zw+diffuseUV.xy+uvOffset.xy;o.originalUV=i.uv;o.c=i.c;o.n=i.n;o.world=p;return o;}
Texture2D image:register(t0);SamplerState sampleImage:register(s0);
Texture2D<float> shadowMap:register(t1);SamplerComparisonState sampleShadow:register(s1);
Texture2D reflectionImage:register(t2);Texture2D glossImage:register(t3);
float3 normal(float3 n){return dot(n,n)>1e-10?normalize(n):float3(0,1,0);}
float3 stageLight(float3 p,float3 n){
 float3 result=0;bool active=false;
 [unroll]for(int j=0;j<6;++j){
  if(lightPosition[j].w<.5)continue;
  active=true;float3 delta=lightPosition[j].xyz-p;
  float distance=length(delta);float3 direction=delta/max(distance,1e-5);
  float falloff=saturate((lightRange[j].y-distance)*lightRange[j].z);
  if(lightPosition[j].w>3.5){direction=lightAxis[j].xyz;falloff=1;}
  else if(lightPosition[j].w>1.5){float cone=dot(-direction,lightAxis[j].xyz);falloff*=saturate((cone-lightAxis[j].w)/max(1e-5,lightRange[j].w-lightAxis[j].w));}
  result+=lightColor[j].rgb*falloff*(.2+saturate(dot(n,direction)));
 }
 return active?saturate(result):float3(1,1,1);
}
float visibility(float4 p){
 if(shadow.x<.5)return 1;
 float3 q=p.xyz/p.w;float2 uv=float2(q.x*.5+.5,.5-q.y*.5);
 if(any(uv<0)||any(uv>1)||q.z<0||q.z>1)return 1;
 float v=0;
 [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x)
 v+=shadowMap.SampleCmpLevelZero(sampleShadow,uv+float2(x,y)/2048,q.z-.0007);
 return v/9;
}
float4 PS(Output i):SV_Target{
 float4 c=flags.x<-.5?i.c:flags.x>.5?image.Sample(sampleImage,i.uv):float4(.73,.76,.8,1);
 if(effect.z>.5)c.rgb/=max(c.a,1e-5);
 if(effect.y>.5)c.a=1;
 if(flags.z>.5)c.rgb*=saturate(i.c.bgr*2);
 c.a*=saturate(i.c.a*2);
 if(flags.w>.5)c.a=1;
 c.a*=effect.x;
 clip(c.a-uvOffset.z);
 if(highlight.w<0){clip(c.a-.01);return float4(highlight.rgb,1);}
 c.rgb*=effect.w;
 if(flags.y>1.5)c.rgb*=stageLight(i.world,normal(i.n));
 else if(flags.y>.5){float3 n=normal(i.n);float l=abs(dot(n,light.xyz));c.rgb*=.42+.58*l*visibility(i.shadowPosition);}
 if(channels.x>.5){
  float3 reflected=reflect(normal(i.world-eye.xyz),normal(i.n));
  float3 r=float3(dot(reflected,cameraRight.xyz),dot(reflected,cameraUp.xyz),dot(reflected,-cameraForward.xyz));
  float2 uv=.5+float2(r.x,-r.y)/max(2*sqrt(dot(r.xy,r.xy)+(r.z+1)*(r.z+1)),1e-5);
  float4 reflectedColor=reflectionImage.Sample(sampleImage,uv*reflectionUV.zw+reflectionUV.xy);
  float gloss=channels.y>.5?glossImage.Sample(sampleImage,i.originalUV*glossUV.zw+glossUV.xy).r:1;
  c.rgb=lerp(c.rgb,reflectedColor.rgb,.5*gloss*reflectedColor.a);
 }
 c.rgb*=channels.w;
 if(fogColor.w>.5){float f=saturate(fogRange.w+fogRange.z*saturate((i.depth-fogRange.x)*fogRange.y));if(fogColor.w>3.5)c.a*=1-f;else c.rgb=lerp(c.rgb,fogColor.w>2.5?float3(1,1,1):fogColor.w>1.5?float3(0,0,0):fogColor.rgb,f);}
 if(highlight.w>0)c.rgb=lerp(c.rgb,highlight.rgb,highlight.w);
 if(uvOffset.w>.5){c.rgb=lerp(float3(1,1,1),c.rgb,c.a);c.a=1;}
 return c;
})";
    ComPtr<ID3DBlob> v, p, error;
    check(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr,
                     "VS", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &v,
                     &error));
    check(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr,
                     "PS", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &p,
                     &error));
    check(device_->CreateVertexShader(v->GetBufferPointer(), v->GetBufferSize(),
                                      nullptr, &vs_));
    check(device_->CreatePixelShader(p->GetBufferPointer(), p->GetBufferSize(),
                                     nullptr, &ps_));
    D3D11_INPUT_ELEMENT_DESC attrs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         UINT(offsetof(Vertex, nx)), D3D11_INPUT_PER_VERTEX_DATA, 0}};
    check(device_->CreateInputLayout(attrs, 4, v->GetBufferPointer(),
                                     v->GetBufferSize(), &layout_));
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(Constants);
    cb.Usage = D3D11_USAGE_DEFAULT;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    check(device_->CreateBuffer(&cb, nullptr, &constants_));
    D3D11_SAMPLER_DESC s{};
    s.Filter = D3D11_FILTER_ANISOTROPIC;
    s.MaxAnisotropy = 8;
    s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    s.MaxLOD = D3D11_FLOAT32_MAX;
    check(device_->CreateSamplerState(&s, &sampler_));
    for (size_t i = 0; i < material_samplers_.size(); ++i) {
      s.AddressU =
          (i & 1) ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
      s.AddressV =
          (i & 2) ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
      check(device_->CreateSamplerState(&s, &material_samplers_[i]));
    }
    D3D11_RASTERIZER_DESC r{};
    r.FillMode = D3D11_FILL_SOLID;
    r.CullMode = D3D11_CULL_NONE;
    r.DepthClipEnable = TRUE;
    check(device_->CreateRasterizerState(&r, &solid_));
    r.FillMode = D3D11_FILL_WIREFRAME;
    check(device_->CreateRasterizerState(&r, &wire_));
    r.FillMode = D3D11_FILL_SOLID;
    r.CullMode = D3D11_CULL_BACK;
    check(device_->CreateRasterizerState(&r, &culled_));
    D3D11_DEPTH_STENCIL_DESC z{};
    z.DepthEnable = TRUE;
    z.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    z.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    check(device_->CreateDepthStencilState(&z, &depth_));
    z.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    check(device_->CreateDepthStencilState(&z, &depth_read_));
    z.DepthEnable = FALSE;
    check(device_->CreateDepthStencilState(&z, &depth_none_));
    D3D11_BLEND_DESC b{};
    b.RenderTarget[0].BlendEnable = TRUE;
    b.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    b.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    b.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    b.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    b.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    b.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    b.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    check(device_->CreateBlendState(&b, &blend_));
    // The game's light/fog passes use source alpha with destination one.
    b.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    check(device_->CreateBlendState(&b, &additive_));
    b.RenderTarget[0].BlendOp = D3D11_BLEND_OP_REV_SUBTRACT;
    check(device_->CreateBlendState(&b, &subtractive_));
    b.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    b.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
    b.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_COLOR;
    check(device_->CreateBlendState(&b, &multiply_));
  }
  DirectX::XMVECTOR light_direction() const {
    using namespace DirectX;
    float a = light_azimuth * XM_PI / 180, e = light_elevation * XM_PI / 180;
    return XMVectorSet(std::sin(a) * std::cos(e), std::sin(e),
                       std::cos(a) * std::cos(e), 0);
  }
  DirectX::XMMATRIX render_shadow(const Model &m) {
    using namespace DirectX;
    if (!shadow_depth_) {
      D3D11_TEXTURE2D_DESC d{};
      d.Width = d.Height = 2048;
      d.ArraySize = d.MipLevels = 1;
      d.SampleDesc.Count = 1;
      d.Format = DXGI_FORMAT_R32_TYPELESS;
      d.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
      check(device_->CreateTexture2D(&d, nullptr, &shadow_texture_));
      D3D11_DEPTH_STENCIL_VIEW_DESC z{};
      z.Format = DXGI_FORMAT_D32_FLOAT;
      z.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
      check(device_->CreateDepthStencilView(shadow_texture_.Get(), &z,
                                            &shadow_depth_));
      D3D11_SHADER_RESOURCE_VIEW_DESC v{};
      v.Format = DXGI_FORMAT_R32_FLOAT;
      v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      v.Texture2D.MipLevels = 1;
      check(device_->CreateShaderResourceView(shadow_texture_.Get(), &v,
                                              &shadow_view_));
      D3D11_SAMPLER_DESC sampler{};
      sampler.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
      sampler.AddressU = sampler.AddressV = sampler.AddressW =
          D3D11_TEXTURE_ADDRESS_BORDER;
      sampler.BorderColor[0] = sampler.BorderColor[1] = sampler.BorderColor[2] =
          sampler.BorderColor[3] = 1;
      sampler.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
      sampler.MaxLOD = D3D11_FLOAT32_MAX;
      check(device_->CreateSamplerState(&sampler, &shadow_sampler_));
      D3D11_RASTERIZER_DESC raster{};
      raster.FillMode = D3D11_FILL_SOLID;
      raster.CullMode = D3D11_CULL_NONE;
      raster.DepthClipEnable = TRUE;
      raster.DepthBias = 100;
      raster.SlopeScaledDepthBias = 1.5f;
      check(device_->CreateRasterizerState(&raster, &shadow_raster_));
    }
    XMVECTOR lo = XMVectorReplicate(FLT_MAX), hi = XMVectorReplicate(-FLT_MAX);
    bool found = false;
    for (const auto &p : m.parts)
      if (p.visible && !p.helper && !p.sky && p.role != MeshRole::DebugFloor &&
          p.role != MeshRole::ParticleEmitter)
        for (const auto &v : p.vertices) {
          auto point = XMVectorSet(v.x, v.y, v.z, 1);
          lo = XMVectorMin(lo, point);
          hi = XMVectorMax(hi, point);
          found = true;
        }
    if (!found) {
      lo = XMVectorSet(-1, 0, -1, 1);
      hi = XMVectorSet(1, 2, 1, 1);
    }
    if (m.character_debug)
      lo = XMVectorSetY(lo, std::min(XMVectorGetY(lo), m.grid_origin[1]));
    auto target = (lo + hi) * .5f;
    float radius = std::max(.01f, XMVectorGetX(XMVector3Length(hi - lo)) * .7f);
    auto matrix = XMMatrixLookAtRH(target + light_direction() * radius * 3,
                                   target, XMVectorSet(0, 1, 0, 0)) *
                  XMMatrixOrthographicRH(radius * 2, radius * 2, radius * .05f,
                                         radius * 6);
    ID3D11ShaderResourceView *none = nullptr;
    context_->PSSetShaderResources(1, 1, &none);
    context_->OMSetRenderTargets(0, nullptr, shadow_depth_.Get());
    context_->ClearDepthStencilView(shadow_depth_.Get(), D3D11_CLEAR_DEPTH, 1,
                                    0);
    D3D11_VIEWPORT viewport{0, 0, 2048, 2048, 0, 1};
    context_->RSSetViewports(1, &viewport);
    context_->RSSetState(shadow_raster_.Get());
    context_->OMSetDepthStencilState(depth_.Get(), 0);
    context_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    context_->IASetInputLayout(layout_.Get());
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vs_.Get(), nullptr, 0);
    context_->PSSetShader(ps_.Get(), nullptr, 0);
    auto *cb = constants_.Get();
    context_->VSSetConstantBuffers(0, 1, &cb);
    context_->PSSetConstantBuffers(0, 1, &cb);
    auto *sampler = sampler_.Get();
    context_->PSSetSamplers(0, 1, &sampler);
    Constants constants{};
    XMStoreFloat4x4(&constants.matrix, matrix);
    constants.effect = {1, 0, 0, 1};
    constants.channels = {0, 0, 0, 1};
    for (const auto &batch : batches_) {
      const auto &p = m.parts[batch.part];
      if (!p.visible || p.helper || p.sky || p.emissive || !p.depth_write ||
          p.blend != BlendMode::Alpha || p.role != MeshRole::Surface)
        continue;
      ID3D11ShaderResourceView *image =
          batch.texture >= 0 && size_t(batch.texture) < images_.size()
              ? images_[batch.texture].Get()
              : nullptr;
      context_->PSSetShaderResources(0, 1, &image);
      constants.flags = {image ? 1.f : 0.f, 0, 0, p.opaque ? 1.f : 0.f};
      constants.uv = {p.uv_offset[0], p.uv_offset[1], p.alpha_cutoff, 0};
      constants.diffuse_uv = channel_uv(p.diffuse);
      auto *material_sampler = material_samplers_[p.material_flags & 3].Get();
      context_->PSSetSamplers(0, 1, &material_sampler);
      constants.effect = {p.opacity, p.texture_alpha ? 0.f : 1.f, 0, 1};
      context_->UpdateSubresource(cb, 0, nullptr, &constants, 0, 0);
      UINT stride = sizeof(Vertex), offset = 0;
      auto *buffer = batch.buffer.Get();
      context_->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
      context_->Draw(batch.count, 0);
    }
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    return matrix;
  }
  void upload(const Model &m) {
    batches_.clear();
    images_.clear();
    alpha_images_.clear();
    for (size_t i = 0; i < m.textures.size(); ++i) {
      const auto &image = m.textures[i];
      // A sprite atlas keeps level zero: a shared downsampled mip would mix
      // separate animation frames after their individual gutters disappear.
      bool mipmaps =
          !m.particle_library ||
          i < m.particle_texture_base + m.particle_library->textures.size();
      images_.push_back(texture(image, mipmaps));
      Image filtered = image;
      bool alpha = false;
      for (size_t p = 0; p < filtered.rgba.size(); p += 4) {
        auto a = filtered.rgba[p + 3];
        alpha |= a != 255;
        for (size_t k = 0; k < 3; ++k)
          filtered.rgba[p + k] =
              uint8_t((unsigned(filtered.rgba[p + k]) * a + 127) / 255);
      }
      // Filtering straight RGB lets invisible texels tint mask edges. Keep a
      // premultiplied sampling view while preserving the original export pixels
      // and the RGB-only view used by opaque materials.
      alpha_images_.push_back(alpha ? texture(filtered, mipmaps)
                                    : images_.back());
    }
    for (size_t i = 0; i < m.parts.size(); ++i)
      add(m.parts[i].vertices, m.parts[i].texture, i);
  }
  void update_pose(const Model &m, bool scenery_only = false) {
    for (auto &batch : batches_) {
      if (scenery_only && m.parts[batch.part].scene_local.empty() &&
          m.parts[batch.part].role != MeshRole::ParticleEmitter)
        continue;
      auto &v = m.parts[batch.part].vertices;
      if (v.size() != batch.count)
        throw std::runtime_error(
            "Changed vertex count requires renderer upload");
      batch.texture = m.parts[batch.part].texture;
      D3D11_MAPPED_SUBRESOURCE map{};
      check(context_->Map(batch.buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                          &map));
      std::memcpy(map.pData, v.data(), v.size() * sizeof(Vertex));
      context_->Unmap(batch.buffer.Get(), 0);
      const auto &part = m.parts[batch.part];
      if (part.blend != BlendMode::Alpha || !part.depth_write)
        batch.center = center(v);
    }
  }
  void resize(uint32_t w, uint32_t h) {
    w = std::clamp(w, 1u, 4096u);
    h = std::clamp(h, 1u, 4096u);
    if (w == width_ && h == height_)
      return;
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11ShaderResourceView *none = nullptr;
    context_->PSSetShaderResources(0, 1, &none);
    view_.Reset();
    target_.Reset();
    depth_view_.Reset();
    color_.Reset();
    depth_texture_.Reset();
    width_ = w;
    height_ = h;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.SampleDesc.Count = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    check(device_->CreateTexture2D(&d, nullptr, &color_));
    check(device_->CreateRenderTargetView(color_.Get(), nullptr, &target_));
    check(device_->CreateShaderResourceView(color_.Get(), nullptr, &view_));
    d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    check(device_->CreateTexture2D(&d, nullptr, &depth_texture_));
    check(device_->CreateDepthStencilView(depth_texture_.Get(), nullptr,
                                          &depth_view_));
  }
  ID3D11ShaderResourceView *view() const { return view_.Get(); }
  void draw(const Model &m, const Camera &c, bool flat = false,
            bool picking = false, bool emission_pass = false) {
    using namespace DirectX;
    bool shadowed = shadows && lighting && !m.stage && !m.bones.empty() &&
                    !flat && !picking && !emission_pass;
    auto light_matrix = shadowed ? render_shadow(m) : XMMatrixIdentity();
    float bg[] = {picking ? 0.f : .055f, picking ? 0.f : .065f,
                  picking ? 0.f : .08f, 1};
    const auto preset =
        m.character_debug
            ? FogPreset{{0, 0, 0}, m.debug_meter * 15, m.debug_meter * 60, 0, 1}
            : m.atmosphere.fog[std::clamp(fog_preset, 0, 4)];
    if (m.character_debug)
      bg[0] = bg[1] = bg[2] = 0;
    if (!picking && !flat && !emission_pass && m.stage && stage_background)
      for (size_t k = 0; k < 3; ++k)
        bg[k] = custom_background        ? background_color[k]
                : m.atmosphere.available ? preset.color[k]
                                         : 0;
    if (emission_pass)
      bg[0] = bg[1] = bg[2] = 0;
    auto *rt = emission_pass ? bloom_->emission_target() : target_.Get();
    context_->OMSetRenderTargets(1, &rt, depth_view_.Get());
    context_->ClearRenderTargetView(rt, bg);
    if (!emission_pass)
      context_->ClearDepthStencilView(
          depth_view_.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1, 0);
    D3D11_VIEWPORT vp{0, 0, float(width_), float(height_), 0, 1};
    context_->RSSetViewports(1, &vp);
    context_->RSSetState(wireframe                   ? wire_.Get()
                         : m.stage && cull_backfaces ? culled_.Get()
                                                     : solid_.Get());
    context_->OMSetDepthStencilState(depth_.Get(), 0);
    context_->OMSetBlendState(blend_.Get(), nullptr, 0xffffffff);
    context_->IASetInputLayout(layout_.Get());
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vs_.Get(), nullptr, 0);
    context_->PSSetShader(ps_.Get(), nullptr, 0);
    auto *cb = constants_.Get();
    context_->VSSetConstantBuffers(0, 1, &cb);
    context_->PSSetConstantBuffers(0, 1, &cb);
    auto *samp = sampler_.Get();
    context_->PSSetSamplers(0, 1, &samp);
    XMVECTOR target = XMVectorSet(c.target[0], c.target[1], c.target[2], 1);
    auto eye = target +
               XMVectorSet(std::sin(c.yaw) * std::cos(c.pitch) * c.distance,
                           std::sin(c.pitch) * c.distance,
                           std::cos(c.yaw) * std::cos(c.pitch) * c.distance, 0);
    // The debug floor needs millimeter-scale separation at character distance.
    // Its fogged surroundings do not need the stage camera's tiny near plane.
    auto matrix = c.view_projection(
        float(width_) / height_, m.character_debug ? m.debug_meter * .05f : 0);
    Constants constants{};
    XMStoreFloat4x4(&constants.matrix, matrix);
    XMStoreFloat4x4(&constants.light_matrix, light_matrix);
    XMStoreFloat4(&constants.light, light_direction());
    constants.shadow = {shadowed ? 1.f : 0.f, 0, 0, 0};
    auto *shadow_view = shadowed ? shadow_view_.Get() : nullptr;
    context_->PSSetShaderResources(1, 1, &shadow_view);
    auto *shadow_sampler = shadow_sampler_.Get();
    context_->PSSetSamplers(1, 1, &shadow_sampler);
    XMStoreFloat4(&constants.eye, eye);
    std::vector<const Batch *> order, effects, skies;
    for (const auto &batch : batches_)
      if (const auto &part = m.parts[batch.part];
          part.visible &&
          (!picking || (part.role != MeshRole::ParticleEmitter &&
                        part.role != MeshRole::DebugFloor)) &&
          (!emission_pass || !part.sky)) {
        if (part.sky) {
          if (sky && !flat && !picking)
            skies.push_back(&batch);
        } else if (part.blend != BlendMode::Alpha || !part.depth_write)
          effects.push_back(&batch);
        else
          order.push_back(&batch);
      }
    auto forward = XMVector3Normalize(target - eye);
    auto right =
        XMVector3Normalize(XMVector3Cross(forward, XMVectorSet(0, 1, 0, 0)));
    XMStoreFloat4(&constants.camera_right, right);
    XMStoreFloat4(&constants.camera_up, XMVector3Cross(right, forward));
    XMStoreFloat4(&constants.camera_forward, forward);
    auto distance = [&](const Batch *batch) {
      return XMVectorGetX(
          XMVector3Dot(XMLoadFloat3(&batch->center) - eye, forward));
    };
    std::stable_sort(effects.begin(), effects.end(),
                     [&](const Batch *a, const Batch *b) {
                       return distance(a) > distance(b);
                     });
    std::stable_sort(
        skies.begin(), skies.end(), [&](const Batch *a, const Batch *b) {
          auto effect = [&](const Batch *batch) {
            const auto &part = m.parts[batch->part];
            return part.blend != BlendMode::Alpha || !part.depth_write;
          };
          return effect(a) < effect(b);
        });
    order.insert(order.end(), effects.begin(), effects.end());
    order.insert(order.begin(), skies.begin(), skies.end());
    for (const auto *item : order) {
      const auto &batch = *item;
      const auto &part = m.parts[batch.part];
      bool shadow = part.role == MeshRole::ShadowOverlay;
      context_->RSSetState(part.sky                    ? solid_.Get()
                           : wireframe                 ? wire_.Get()
                           : shadow || part.two_sided  ? solid_.Get()
                           : m.stage && cull_backfaces ? culled_.Get()
                                                       : solid_.Get());
      context_->OMSetDepthStencilState(
          part.sky                                          ? depth_none_.Get()
          : !emission_pass && (picking || part.depth_write) ? depth_.Get()
                                                            : depth_read_.Get(),
          0);
      context_->OMSetBlendState(
          picking                                ? nullptr
          : part.blend == BlendMode::Additive    ? additive_.Get()
          : part.blend == BlendMode::Multiply    ? multiply_.Get()
          : part.blend == BlendMode::Subtractive ? subtractive_.Get()
                                                 : blend_.Get(),
          nullptr, 0xffffffff);
      ID3D11ShaderResourceView *image = nullptr;
      bool sample_alpha = part.texture_alpha && !part.opaque;
      if (batch.texture >= 0 && size_t(batch.texture) < images_.size())
        image = (sample_alpha ? alpha_images_ : images_)[batch.texture].Get();
      context_->PSSetShaderResources(0, 1, &image);
      auto *material_sampler =
          material_samplers_[part.material_flags & 3].Get();
      context_->PSSetSamplers(0, 1, &material_sampler);
      auto channel_image = [&](int texture) -> ID3D11ShaderResourceView * {
        return textures && texture >= 0 && size_t(texture) < images_.size()
                   ? images_[texture].Get()
                   : nullptr;
      };
      ID3D11ShaderResourceView *channels[]{
          channel_image(part.reflection.texture),
          channel_image(part.gloss.texture)};
      context_->PSSetShaderResources(2, 2, channels);
      constants.diffuse_uv = channel_uv(part.diffuse);
      constants.reflection_uv = channel_uv(part.reflection);
      constants.gloss_uv = channel_uv(part.gloss);
      constants.channels = {!flat && !picking && !part.sky && channels[0] ? 1.f
                                                                          : 0,
                            channels[1] ? 1.f : 0, 0,
                            emission_pass && !part.emissive &&
                                    part.blend != BlendMode::Multiply &&
                                    part.blend != BlendMode::Subtractive
                                ? 0.f
                                : 1.f};
      stage_lights(constants, m, batch);
      constants.flags = {
          (textures || part.role == MeshRole::DebugFloor) && image ? 1.f : 0,
          lighting && !flat && !part.sky && !part.emissive && !shadow &&
                  part.role != MeshRole::ParticleEmitter &&
                  part.blend == BlendMode::Alpha
              ? (m.stage ? (!part.prelit && source_lights ? 2.f : 0.f) : 1.f)
              : 0,
          vertex_colors || (lighting && part.prelit) || shadow ||
                  part.role == MeshRole::ParticleEmitter
              ? 1.f
              : 0,
          part.opaque ? 1.f : 0};
      constants.uv = {part.uv_offset[0], part.uv_offset[1], part.alpha_cutoff,
                      !picking && part.blend == BlendMode::Multiply ? 1.f : 0};
      constants.effect = {part.opacity, part.texture_alpha ? 0.f : 1.f,
                          sample_alpha && textures && image ? 1.f : 0.f,
                          part.role == MeshRole::DebugFloor ? .35f : 1.f};
      constants.sky = part.sky ? XMFLOAT4{m.sky_origin[0], m.sky_origin[1],
                                          m.sky_origin[2], 1}
                               : XMFLOAT4{};
      bool fogged =
          (m.character_debug || (fog && m.stage && m.atmosphere.available)) &&
          preset.active() && !flat && !picking && !part.sky;
      constants.fog_color = {
          emission_pass ? 0.f : preset.color[0],
          emission_pass ? 0.f : preset.color[1],
          emission_pass ? 0.f : preset.color[2],
          fogged ? (shadow || (part.emissive && part.blend == BlendMode::Alpha)
                        ? 4.f
                    : part.blend == BlendMode::Multiply ? 3.f
                    : part.blend == BlendMode::Additive ||
                            part.blend == BlendMode::Subtractive
                        ? 2.f
                        : 1.f)
                 : 0};
      constants.fog_range = {
          preset.start,
          preset.start != preset.end ? 1 / (preset.end - preset.start) : 0,
          preset.maximum - preset.minimum, preset.minimum};
      if (picking) {
        uint32_t id = uint32_t(batch.part + 1);
        constants.highlight = {float(id & 255) / 255,
                               float((id >> 8) & 255) / 255,
                               float((id >> 16) & 255) / 255, -1};
      } else {
        constants.highlight =
            !flat && !emission_pass &&
                    m.same_object(batch.part, size_t(selected_part))
                ? XMFLOAT4{1, .9f, 0, .38f}
                : XMFLOAT4{};
      }
      context_->UpdateSubresource(cb, 0, nullptr, &constants, 0, 0);
      UINT stride = sizeof(Vertex), offset = 0;
      auto *v = batch.buffer.Get();
      context_->IASetVertexBuffers(0, 1, &v, &stride, &offset);
      context_->Draw(batch.count, 0);
    }
    context_->OMSetDepthStencilState(depth_.Get(), 0);
    context_->OMSetBlendState(blend_.Get(), nullptr, 0xffffffff);
    constants.uv = {};
    constants.highlight = {};
    constants.sky = {};
    constants.fog_color = {};
    constants.effect = {1, 0, 0, 1};
    constants.channels = {0, 0, 0, 1};
    constants.diffuse_uv = {0, 0, 1, 1};
    if (light_gizmos && m.stage && !flat && !picking && !emission_pass &&
        !m.lights.empty()) {
      std::vector<Vertex> lines;
      for (const auto &light : m.lights) {
        if (!light.enabled)
          continue;
        uint32_t color = 0xff000000;
        for (size_t k = 0; k < 3; ++k)
          color |= uint32_t(std::clamp(light.color[k] * 255, 0.f, 255.f))
                   << (k * 8);
        float radius = std::max(1.f, c.radius * .008f);
        for (size_t k = 0; k < 3; ++k) {
          auto a = light.position, b = a;
          a[k] -= radius;
          b[k] += radius;
          lines.push_back({a[0], a[1], a[2], 0, 0, color});
          lines.push_back({b[0], b[1], b[2], 0, 0, color});
        }
      }
      if (!lines.empty()) {
        D3D11_BUFFER_DESC d{};
        d.ByteWidth = UINT(lines.size() * sizeof(Vertex));
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{lines.data(), 0, 0};
        ComPtr<ID3D11Buffer> buffer;
        check(device_->CreateBuffer(&d, &data, &buffer));
        auto *v = buffer.Get();
        UINT stride = sizeof(Vertex), offset = 0;
        context_->IASetVertexBuffers(0, 1, &v, &stride, &offset);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        constants.flags = {-1, 0, 0, 1};
        context_->UpdateSubresource(cb, 0, nullptr, &constants, 0, 0);
        context_->Draw(UINT(lines.size()), 0);
      }
    }
    if (grid && !m.character_debug && !flat && !picking && !emission_pass) {
      std::vector<Vertex> lines;
      float radius = m.grid_radius, step = radius / 5;
      float ground = m.grid_origin[1] - .002f * radius;
      for (int i = -10; i <= 10; ++i) {
        float v = i * step;
        lines.push_back({m.grid_origin[0] - radius * 2, ground,
                         m.grid_origin[2] + v, 0, 0, 0xff353535});
        lines.push_back({m.grid_origin[0] + radius * 2, ground,
                         m.grid_origin[2] + v, 0, 0, 0xff353535});
        lines.push_back({m.grid_origin[0] + v, ground,
                         m.grid_origin[2] - radius * 2, 0, 0, 0xff353535});
        lines.push_back({m.grid_origin[0] + v, ground,
                         m.grid_origin[2] + radius * 2, 0, 0, 0xff353535});
      }
      D3D11_BUFFER_DESC d{};
      d.ByteWidth = UINT(lines.size() * sizeof(Vertex));
      d.Usage = D3D11_USAGE_IMMUTABLE;
      d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
      D3D11_SUBRESOURCE_DATA data{lines.data(), 0, 0};
      ComPtr<ID3D11Buffer> v;
      check(device_->CreateBuffer(&d, &data, &v));
      UINT stride = sizeof(Vertex), offset = 0;
      auto *ptr = v.Get();
      context_->IASetVertexBuffers(0, 1, &ptr, &stride, &offset);
      context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
      constants.flags = {0, 0, 1, 0};
      context_->UpdateSubresource(cb, 0, nullptr, &constants, 0, 0);
      context_->Draw(UINT(lines.size()), 0);
    }
    if (joints && !flat && !picking && !emission_pass && !m.bones.empty()) {
      std::vector<Vertex> lines;
      auto line = [&](const std::array<float, 3> &a,
                      const std::array<float, 3> &b, uint32_t color) {
        lines.push_back({a[0], a[1], a[2], 0, 0, color});
        lines.push_back({b[0], b[1], b[2], 0, 0, color});
      };
      for (size_t i = 0; i < m.bones.size(); ++i) {
        auto &bone = m.bones[i];
        if (!bone.used && !all_joints)
          continue;
        uint32_t color = int(i) == selected_bone ? 0xff80faff : 0xff57ceff;
        if (bone.parent >= 0)
          line(bone.position, m.bones[bone.parent].position, color);
        for (int k = 0; k < 3; ++k) {
          auto a = bone.position, b = a;
          float size = c.radius * (int(i) == selected_bone ? .018f : .007f);
          a[k] -= size;
          b[k] += size;
          line(a, b, color);
        }
      }
      if (!lines.empty()) {
        D3D11_BUFFER_DESC d{};
        d.ByteWidth = UINT(lines.size() * sizeof(Vertex));
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{lines.data(), 0, 0};
        ComPtr<ID3D11Buffer> buffer;
        check(device_->CreateBuffer(&d, &data, &buffer));
        UINT stride = sizeof(Vertex), offset = 0;
        auto *v = buffer.Get();
        context_->IASetVertexBuffers(0, 1, &v, &stride, &offset);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        context_->OMSetDepthStencilState(depth_none_.Get(), 0);
        constants.flags = {-1, 0, 0, 1};
        context_->UpdateSubresource(cb, 0, nullptr, &constants, 0, 0);
        context_->Draw(UINT(lines.size()), 0);
      }
    }
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    if (bloom && !flat && !picking && !emission_pass) {
      if (!bloom_)
        bloom_ = std::make_unique<BloomPreview>(device_, context_);
      bloom_->resize(width_, height_);
      bloom_->capture_scene(color_.Get());
      draw(m, c, false, false, true);
      bloom_->apply(target_.Get(), bloom_threshold, bloom_strength,
                    bloom_radius);
    }
  }
  int pick(const Model &model, const Camera &camera, float x, float y) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_)
      return -1;
    if (!pick_pixel_) {
      D3D11_TEXTURE2D_DESC d{};
      d.Width = d.Height = d.MipLevels = d.ArraySize = 1;
      d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      d.SampleDesc.Count = 1;
      d.Usage = D3D11_USAGE_STAGING;
      d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      check(device_->CreateTexture2D(&d, nullptr, &pick_pixel_));
    }
    // Reuse the visible shader's alpha test, culling and animated geometry.
    // Every accepted fragment writes depth so the nearest surface is picked.
    draw(model, camera, false, true);
    D3D11_BOX pixel{UINT(x), UINT(y), 0, UINT(x) + 1, UINT(y) + 1, 1};
    context_->CopySubresourceRegion(pick_pixel_.Get(), 0, 0, 0, 0, color_.Get(),
                                    0, &pixel);
    D3D11_MAPPED_SUBRESOURCE map{};
    check(context_->Map(pick_pixel_.Get(), 0, D3D11_MAP_READ, 0, &map));
    const auto *rgba = static_cast<const uint8_t *>(map.pData);
    uint32_t id = rgba[0] | uint32_t(rgba[1]) << 8 | uint32_t(rgba[2]) << 16;
    context_->Unmap(pick_pixel_.Get(), 0);
    return id && id <= model.parts.size() ? int(id - 1) : -1;
  }
  Image capture() {
    D3D11_TEXTURE2D_DESC desc{};
    color_->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> copy;
    check(device_->CreateTexture2D(&desc, nullptr, &copy));
    context_->CopyResource(copy.Get(), color_.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    check(context_->Map(copy.Get(), 0, D3D11_MAP_READ, 0, &map));
    Image out{width_, height_, Bytes(size_t(width_) * height_ * 4)};
    for (uint32_t y = 0; y < height_; ++y)
      std::memcpy(out.rgba.data() + size_t(y) * width_ * 4,
                  static_cast<const uint8_t *>(map.pData) +
                      size_t(y) * map.RowPitch,
                  size_t(width_) * 4);
    context_->Unmap(copy.Get(), 0);
    return out;
  }
};
} // namespace shaolin::viewer

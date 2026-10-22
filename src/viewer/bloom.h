#pragma once
#include <DirectXMath.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <wrl/client.h>

namespace shaolin::viewer {
class BloomPreview {
  template <class T> using Ptr = Microsoft::WRL::ComPtr<T>;
  ID3D11Device *device_;
  ID3D11DeviceContext *context_;
  Ptr<ID3D11VertexShader> vertex_;
  Ptr<ID3D11PixelShader> bright_, blur_, composite_;
  Ptr<ID3D11Buffer> constants_;
  Ptr<ID3D11SamplerState> sampler_;
  Ptr<ID3D11RasterizerState> raster_;
  Ptr<ID3D11DepthStencilState> depth_;
  Ptr<ID3D11Texture2D> scene_;
  Ptr<ID3D11ShaderResourceView> scene_view_, emission_view_, blur_view_[2];
  Ptr<ID3D11RenderTargetView> emission_target_, blur_target_[2];
  uint32_t width_ = 0, height_ = 0, small_width_ = 0, small_height_ = 0;
  static void checked(HRESULT result) {
    if (FAILED(result))
      throw std::runtime_error("Bloom preview Direct3D failure: " +
                               std::to_string(uint32_t(result)));
  }
  void target(uint32_t w, uint32_t h, Ptr<ID3D11RenderTargetView> &rt,
              Ptr<ID3D11ShaderResourceView> &view) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    Ptr<ID3D11Texture2D> texture;
    checked(device_->CreateTexture2D(&d, nullptr, &texture));
    checked(device_->CreateRenderTargetView(texture.Get(), nullptr, &rt));
    checked(device_->CreateShaderResourceView(texture.Get(), nullptr, &view));
  }
  void pass(ID3D11RenderTargetView *output, ID3D11PixelShader *shader,
            ID3D11ShaderResourceView *first, ID3D11ShaderResourceView *second,
            uint32_t w, uint32_t h, DirectX::XMFLOAT4 parameters) {
    context_->OMSetRenderTargets(1, &output, nullptr);
    D3D11_VIEWPORT viewport{0, 0, float(w), float(h), 0, 1};
    context_->RSSetViewports(1, &viewport);
    context_->PSSetShader(shader, nullptr, 0);
    ID3D11ShaderResourceView *inputs[]{first, second};
    context_->PSSetShaderResources(0, 2, inputs);
    context_->UpdateSubresource(constants_.Get(), 0, nullptr, &parameters, 0,
                                0);
    context_->Draw(3, 0);
    ID3D11ShaderResourceView *none[2]{};
    context_->PSSetShaderResources(0, 2, none);
    context_->OMSetRenderTargets(0, nullptr, nullptr);
  }

public:
  BloomPreview(ID3D11Device *device, ID3D11DeviceContext *context)
      : device_(device), context_(context) {
    constexpr const char *shader = R"(
cbuffer Settings:register(b0){float4 parameters;};
Texture2D firstImage:register(t0);Texture2D secondImage:register(t1);
SamplerState sampleImage:register(s0);
struct Output{float4 p:SV_Position;float2 uv:TEXCOORD;};
Output VS(uint id:SV_VertexID){Output o;o.uv=float2((id<<1)&2,id&2);o.p=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}
float3 bright(float2 uv){float3 c=firstImage.Sample(sampleImage,uv).rgb;float value=max(c.r,max(c.g,c.b));return c*max(value-parameters.z,0)/max(value,.00001);}
float4 Bright(Output i):SV_Target{float2 d=parameters.xy*.5;return float4((bright(i.uv+d)+bright(i.uv-d)+bright(i.uv+float2(d.x,-d.y))+bright(i.uv+float2(-d.x,d.y)))*.25,1);}
float4 Blur(Output i):SV_Target{float2 d=parameters.xy;float3 c=firstImage.Sample(sampleImage,i.uv).rgb*.227027;
 c+=(firstImage.Sample(sampleImage,i.uv+d*1.384615).rgb+firstImage.Sample(sampleImage,i.uv-d*1.384615).rgb)*.316216;
 c+=(firstImage.Sample(sampleImage,i.uv+d*3.230769).rgb+firstImage.Sample(sampleImage,i.uv-d*3.230769).rgb)*.070270;return float4(c,1);}
float4 Composite(Output i):SV_Target{return float4(firstImage.Sample(sampleImage,i.uv).rgb+secondImage.Sample(sampleImage,i.uv).rgb*parameters.w,1);}
)";
    auto compile = [&](const char *entry, const char *profile) {
      Ptr<ID3DBlob> result, error;
      checked(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr,
                         entry, profile, D3DCOMPILE_ENABLE_STRICTNESS, 0,
                         &result, &error));
      return result;
    };
    auto vs = compile("VS", "vs_5_0");
    checked(device_->CreateVertexShader(
        vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex_));
    for (auto item : {std::pair{"Bright", &bright_}, std::pair{"Blur", &blur_},
                      std::pair{"Composite", &composite_}}) {
      auto ps = compile(item.first, "ps_5_0");
      checked(device_->CreatePixelShader(
          ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, item.second));
    }
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = 16;
    cb.Usage = D3D11_USAGE_DEFAULT;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    checked(device_->CreateBuffer(&cb, nullptr, &constants_));
    D3D11_SAMPLER_DESC s{};
    s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    s.MaxLOD = D3D11_FLOAT32_MAX;
    checked(device_->CreateSamplerState(&s, &sampler_));
    D3D11_RASTERIZER_DESC r{};
    r.FillMode = D3D11_FILL_SOLID;
    r.CullMode = D3D11_CULL_NONE;
    r.DepthClipEnable = TRUE;
    checked(device_->CreateRasterizerState(&r, &raster_));
    D3D11_DEPTH_STENCIL_DESC z{};
    checked(device_->CreateDepthStencilState(&z, &depth_));
  }
  void resize(uint32_t w, uint32_t h) {
    if (w == width_ && h == height_)
      return;
    scene_.Reset();
    scene_view_.Reset();
    emission_view_.Reset();
    emission_target_.Reset();
    for (size_t i = 0; i < 2; ++i) {
      blur_view_[i].Reset();
      blur_target_[i].Reset();
    }
    width_ = w;
    height_ = h;
    small_width_ = std::max(1u, (w + 3) / 4);
    small_height_ = std::max(1u, (h + 3) / 4);
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    checked(device_->CreateTexture2D(&d, nullptr, &scene_));
    checked(
        device_->CreateShaderResourceView(scene_.Get(), nullptr, &scene_view_));
    target(w, h, emission_target_, emission_view_);
    for (size_t i = 0; i < 2; ++i)
      target(small_width_, small_height_, blur_target_[i], blur_view_[i]);
  }
  ID3D11RenderTargetView *emission_target() const {
    return emission_target_.Get();
  }
  void capture_scene(ID3D11Texture2D *scene) {
    context_->CopyResource(scene_.Get(), scene);
  }
  void apply(ID3D11RenderTargetView *output, float threshold, float strength,
             float radius) {
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertex_.Get(), nullptr, 0);
    context_->RSSetState(raster_.Get());
    context_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    context_->OMSetDepthStencilState(depth_.Get(), 0);
    auto *cb = constants_.Get();
    context_->PSSetConstantBuffers(0, 1, &cb);
    auto *sampler = sampler_.Get();
    context_->PSSetSamplers(0, 1, &sampler);
    pass(blur_target_[0].Get(), bright_.Get(), emission_view_.Get(), nullptr,
         small_width_, small_height_,
         {1.f / width_, 1.f / height_, threshold, 0});
    pass(blur_target_[1].Get(), blur_.Get(), blur_view_[0].Get(), nullptr,
         small_width_, small_height_, {radius / small_width_, 0, 0, 0});
    pass(blur_target_[0].Get(), blur_.Get(), blur_view_[1].Get(), nullptr,
         small_width_, small_height_, {0, radius / small_height_, 0, 0});
    pass(output, composite_.Get(), scene_view_.Get(), blur_view_[0].Get(),
         width_, height_, {0, 0, 0, strength});
  }
};
} // namespace shaolin::viewer

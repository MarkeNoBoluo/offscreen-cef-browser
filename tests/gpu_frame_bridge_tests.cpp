#include "browser/gpu_frame_bridge.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

namespace {

using Microsoft::WRL::ComPtr;

template <typename A, typename B>
void expect_eq(A actual, B expected, const char* label) {
  if (actual != expected) {
    std::cerr << label << " expected [" << expected << "] but got [" << actual
              << "]\n";
    std::exit(1);
  }
}

void expect_true(bool actual, const char* label) {
  if (!actual) {
    std::cerr << label << " expected true but got false\n";
    std::exit(1);
  }
}

void test_empty_bridge_snapshot() {
  offscreen::GpuFrameBridge bridge;
  const auto snapshot = bridge.Snapshot(offscreen::GpuFrameKind::kView);
  expect_true(!snapshot.texture, "empty bridge has no texture");
  expect_eq(snapshot.publication.frame_generation, uint64_t{0},
            "empty generation");
}

void test_shared_texture_is_copied_to_application_texture() {
  ComPtr<ID3D11Device> source_device;
  ComPtr<ID3D11DeviceContext> source_context;
  const HRESULT device_hr = D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
      &source_device, nullptr, &source_context);
  expect_true(SUCCEEDED(device_hr), "hardware D3D11 device created");

  const uint32_t pixels[] = {
      0xff112233, 0xff445566, 0xff778899, 0xffaabbcc,
      0xff010203, 0xff040506, 0xff070809, 0xff0a0b0c,
  };
  D3D11_TEXTURE2D_DESC source_desc{};
  source_desc.Width = 4;
  source_desc.Height = 2;
  source_desc.MipLevels = 1;
  source_desc.ArraySize = 1;
  source_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  source_desc.SampleDesc.Count = 1;
  source_desc.Usage = D3D11_USAGE_DEFAULT;
  source_desc.BindFlags =
      D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  source_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
  D3D11_SUBRESOURCE_DATA source_data{};
  source_data.pSysMem = pixels;
  source_data.SysMemPitch = source_desc.Width * sizeof(uint32_t);

  ComPtr<ID3D11Texture2D> source_texture;
  expect_true(SUCCEEDED(source_device->CreateTexture2D(
                  &source_desc, &source_data, &source_texture)),
              "shared source texture created");
  source_context->Flush();

  ComPtr<IDXGIResource> dxgi_resource;
  expect_true(SUCCEEDED(source_texture.As(&dxgi_resource)),
              "source exposes IDXGIResource");
  HANDLE shared_handle = nullptr;
  expect_true(SUCCEEDED(dxgi_resource->GetSharedHandle(&shared_handle)) &&
                  shared_handle != nullptr,
              "source shared handle created");

  offscreen::GpuFrameBridge bridge;
  const auto result = bridge.CopyFromSharedHandle(
      offscreen::GpuFrameKind::kView, shared_handle);
  expect_true(result.success, result.error.c_str());
  expect_eq(result.publication.resource_generation, uint64_t{1},
            "bridge resource generation");
  expect_eq(result.publication.frame_generation, uint64_t{1},
            "bridge frame generation");

  const auto snapshot = bridge.Snapshot(offscreen::GpuFrameKind::kView);
  expect_true(snapshot.texture != nullptr, "bridge publishes texture");
  expect_eq(snapshot.publication.width, 4, "published width");
  expect_eq(snapshot.publication.height, 2, "published height");

  D3D11_TEXTURE2D_DESC staging_desc{};
  snapshot.texture->GetDesc(&staging_desc);
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  staging_desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> staging;
  expect_true(SUCCEEDED(snapshot.device->CreateTexture2D(
                  &staging_desc, nullptr, &staging)),
              "test staging texture created");
  ComPtr<ID3D11DeviceContext> bridge_context;
  snapshot.device->GetImmediateContext(&bridge_context);
  bridge_context->CopyResource(staging.Get(), snapshot.texture.Get());

  D3D11_MAPPED_SUBRESOURCE mapped{};
  expect_true(SUCCEEDED(bridge_context->Map(
                  staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
              "test staging texture mapped");
  const auto* copied = static_cast<const uint32_t*>(mapped.pData);
  expect_eq(copied[0], pixels[0], "first copied pixel");
  expect_eq(copied[1], pixels[1], "second copied pixel");
  bridge_context->Unmap(staging.Get(), 0);
}

}  // namespace

int main() {
  test_empty_bridge_snapshot();
  test_shared_texture_is_copied_to_application_texture();
  return 0;
}

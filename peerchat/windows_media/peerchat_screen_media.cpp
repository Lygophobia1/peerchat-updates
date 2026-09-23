#include "peerchat_screen_media.h"

#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <mfapi.h>
#include <mftransform.h>
#include <wrl/client.h>
#include <winrt/Windows.Graphics.Capture.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace {
struct Enumeration {
    pc_screen_source_callback callback;
    void* context;
    uint32_t count = 0;
};

BOOL CALLBACK monitor(HMONITOR handle, HDC, LPRECT, LPARAM opaque) {
    auto& state = *reinterpret_cast<Enumeration*>(opaque);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(handle, &info)) return TRUE;
    const auto width = info.rcMonitor.right - info.rcMonitor.left;
    const auto height = info.rcMonitor.bottom - info.rcMonitor.top;
    if (width <= 0 || height <= 0) return TRUE;
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    const bool refresh = EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) != 0;
    const auto hz = refresh && mode.dmDisplayFrequency > 1 ? mode.dmDisplayFrequency : 0;
    ++state.count;
    if (state.callback) state.callback(state.context, PC_SCREEN_MONITOR,
        reinterpret_cast<uint64_t>(handle), info.szDevice,
        static_cast<uint32_t>(width), static_cast<uint32_t>(height), hz, hz ? 1u : 0u);
    return TRUE;
}

BOOL CALLBACK window(HWND handle, LPARAM opaque) {
    auto& state = *reinterpret_cast<Enumeration*>(opaque);
    if (!IsWindowVisible(handle) || GetWindow(handle, GW_OWNER) ||
        (GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return TRUE;
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(handle, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
        cloaked) return TRUE;
    const int length = GetWindowTextLengthW(handle);
    if (length <= 0 || length > 512) return TRUE;
    std::wstring title(static_cast<size_t>(length) + 1, L'\0');
    const int written = GetWindowTextW(handle, title.data(), length + 1);
    if (written <= 0) return TRUE;
    title.resize(static_cast<size_t>(written));
    RECT bounds{};
    if (!GetWindowRect(handle, &bounds)) return TRUE;
    const auto width = bounds.right - bounds.left;
    const auto height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) return TRUE;
    ++state.count;
    if (state.callback) state.callback(state.context, PC_SCREEN_WINDOW,
        reinterpret_cast<uint64_t>(handle), title.c_str(),
        static_cast<uint32_t>(width), static_cast<uint32_t>(height), 0, 0);
    return TRUE;
}

bool transform_available(const GUID& category, const GUID& codec, UINT32 flags) {
    MFT_REGISTER_TYPE_INFO type{MFMediaType_Video, codec};
    IMFActivate** activations = nullptr;
    UINT32 count = 0;
    const bool isDecoder = category == MFT_CATEGORY_VIDEO_DECODER;
    const HRESULT result = MFTEnumEx(category, flags,
        isDecoder ? &type : nullptr, isDecoder ? nullptr : &type,
        &activations, &count);
    if (SUCCEEDED(result)) {
        for (UINT32 index = 0; index < count; ++index) activations[index]->Release();
        CoTaskMemFree(activations);
    }
    return SUCCEEDED(result) && count != 0;
}
}

extern "C" uint32_t __cdecl pc_screen_enumerate_sources(
    pc_screen_source_callback callback, void* context) {
    try {
        Enumeration state{callback, context};
        EnumDisplayMonitors(nullptr, nullptr, monitor, reinterpret_cast<LPARAM>(&state));
        EnumWindows(window, reinterpret_cast<LPARAM>(&state));
        return state.count;
    } catch (...) { return 0; }
}

extern "C" int __cdecl pc_screen_query_capabilities(pc_screen_capabilities* value) {
    if (!value || value->structure_size != sizeof(pc_screen_capabilities)) return 0;
    try {
    pc_screen_capabilities result{};
    result.structure_size = sizeof(result);
    result.capture_supported = winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported();
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    result.d3d11_supported = SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE,
        nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr));
    if (SUCCEEDED(MFStartup(MF_VERSION))) {
        const auto hardware = MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER;
        const auto decoder = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER;
        if (transform_available(MFT_CATEGORY_VIDEO_ENCODER, MFVideoFormat_H264, hardware))
            result.hardware_encoder_codecs |= PC_SCREEN_H264;
        if (transform_available(MFT_CATEGORY_VIDEO_ENCODER, MFVideoFormat_HEVC, hardware))
            result.hardware_encoder_codecs |= PC_SCREEN_HEVC;
        if (transform_available(MFT_CATEGORY_VIDEO_ENCODER, MFVideoFormat_AV1, hardware))
            result.hardware_encoder_codecs |= PC_SCREEN_AV1;
        if (transform_available(MFT_CATEGORY_VIDEO_DECODER, MFVideoFormat_H264, decoder))
            result.decoder_codecs |= PC_SCREEN_H264;
        if (transform_available(MFT_CATEGORY_VIDEO_DECODER, MFVideoFormat_HEVC, decoder))
            result.decoder_codecs |= PC_SCREEN_HEVC;
        if (transform_available(MFT_CATEGORY_VIDEO_DECODER, MFVideoFormat_AV1, decoder))
            result.decoder_codecs |= PC_SCREEN_AV1;
        MFShutdown();
    }
    std::memcpy(value, &result, sizeof(result));
    return 1;
    } catch (...) { return 0; }
}

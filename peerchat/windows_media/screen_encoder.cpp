#include "peerchat_screen_media.h"

#include <windows.h>
#include <d3d10_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <codecapi.h>
#include <icodecapi.h>
#include <wrl/client.h>
#include <Windows.Graphics.Capture.Interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include <string>

using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

namespace {
struct EncodedFrame {
    std::vector<uint8_t> bytes;
    pc_screen_encoded_frame info{};
    bool bootstrap = false;
};

bool h264_nals(const std::vector<uint8_t>& bytes,uint64_t& sps,uint64_t& pps,uint64_t& idr,uint64_t& nonIdr) {
    bool bootstrap=false;
    for(size_t index=0;index+3<bytes.size();) {
        size_t start=0;
        if(bytes[index]==0&&bytes[index+1]==0&&bytes[index+2]==1) start=3;
        else if(index+4<bytes.size()&&bytes[index]==0&&bytes[index+1]==0&&bytes[index+2]==0&&bytes[index+3]==1) start=4;
        if(!start){++index;continue;}
        const auto type=bytes[index+start]&31;
        if(type==7){++sps;bootstrap=true;}else if(type==8){++pps;bootstrap=true;}
        else if(type==5){++idr;bootstrap=true;}else if(type==1)++nonIdr;
        index+=start;
    }
    return bootstrap;
}

struct CapturedFrame {
    Direct3D11CaptureFrame frame{nullptr};
    std::chrono::steady_clock::time_point arrival;
};

class EncoderState;

class EventSink final : public IMFAsyncCallback {
public:
    explicit EventSink(std::weak_ptr<EncoderState> owner) : owner_(std::move(owner)) {}
    STDMETHODIMP QueryInterface(REFIID id, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (id != IID_IUnknown && id != __uuidof(IMFAsyncCallback)) return E_NOINTERFACE;
        *result = static_cast<IMFAsyncCallback*>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override {
        const auto left = --references_;
        if (!left) delete this;
        return left;
    }
    STDMETHODIMP GetParameters(DWORD*, DWORD*) override { return E_NOTIMPL; }
    STDMETHODIMP Invoke(IMFAsyncResult* result) override;
private:
    std::atomic<ULONG> references_{1};
    std::weak_ptr<EncoderState> owner_;
};

class EncoderState : public std::enable_shared_from_this<EncoderState> {
public:
    explicit EncoderState(pc_screen_encoder_options options) : options_(options) {}
    ~EncoderState() { stop(); }

    bool start() {
        worker_ = std::thread([self = shared_from_this()] { self->run(); });
        std::unique_lock lock(mutex_);
        if (!condition_.wait_for(lock, std::chrono::seconds(8),
                [this] { return initialized_ || failed_; })) {
            lock.unlock();
            stop();
            return false;
        }
        return initialized_ && !failed_;
    }

    void stop() {
        running_ = false;
        condition_.notify_all();
        if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id())
            worker_.join();
    }

    int pop(uint8_t* output, uint32_t capacity, pc_screen_encoded_frame* metadata) {
        if (!metadata || metadata->structure_size != sizeof(*metadata)) return -2;
        std::lock_guard lock(mutex_);
        if (output_.empty()) return failed_ ? -2 : 0;
        const auto& next = output_.front();
        *metadata = next.info;
        if (!output || capacity < next.bytes.size()) return -1;
        std::memcpy(output, next.bytes.data(), next.bytes.size());
        output_.pop_front();
        return 1;
    }

    int status(pc_screen_encoder_status* value) {
        if (!value || value->structure_size != sizeof(*value)) return 0;
        std::lock_guard lock(mutex_);
        const auto now = GetTickCount64();
        const auto callbackTick = lastCaptureCallbackTick_.load();
        const auto captureStageTick = captureStageSince_.load();
        const auto workerStageTick = workerStageSince_.load();
        *value = {};
        value->structure_size = sizeof(*value);
        value->captured_frames = capturedFrames_;
        value->submitted_to_encoder = submittedFrames_;
        value->encoded_frames = encodedFrames_;
        value->dropped_before_encode = droppedBeforeEncode_;
        value->dropped_encoded_queue = droppedEncodedQueue_;
        value->capture_callback_count = captureCallbackCount_.load();
        value->last_capture_callback_age_ms = callbackTick && now >= callbackTick ? now - callbackTick : 0;
        value->capture_callback_stage = captureStage_.load();
        value->capture_callback_stage_elapsed_ms = captureStageTick && now >= captureStageTick ? now - captureStageTick : 0;
        value->encoder_worker_stage = workerStage_.load();
        value->encoder_worker_stage_elapsed_ms = workerStageTick && now >= workerStageTick ? now - workerStageTick : 0;
        value->pending_capture_frame = pending_.frame ? 1u : 0u;
        value->encoded_queue_depth = static_cast<uint32_t>(output_.size());
        value->encoder_input_ready = inputReady_ ? 1u : 0u;
        value->failed = failed_ ? 1u : 0u;
        value->startup_stage = startupStage_.load();
        value->startup_failure_operation = startupFailureOperation_;
        value->startup_failure_hresult = startupFailureHresult_;
        value->keyframe_requests = keyframeRequests_;
        value->sps_nals = spsNals_; value->pps_nals = ppsNals_;
        value->idr_nals = idrNals_; value->non_idr_nals = nonIdrNals_;
        value->failed_operation = failedOperation_;
        value->last_hresult = lastHresult_;
        value->process_input_hresult = processInputHresult_;
        value->process_output_hresult = processOutputHresult_;
        value->process_input_failures = processInputFailures_;
        value->process_output_failures = processOutputFailures_;
        value->stream_change_count = streamChangeCount_;
        value->output_type_renegotiation_count = outputTypeRenegotiationCount_;
        value->output_type_renegotiation_hresult = outputTypeRenegotiationHresult_;
        value->output_type_width = outputTypeWidth_;
        value->output_type_height = outputTypeHeight_;
        value->output_type_frames_per_second = outputTypeFramesPerSecond_;
        value->cbr_control_supported = cbrControlSupported_ ? 1u : 0u;
        value->cbr_control_applied = cbrControlApplied_ ? 1u : 0u;
        value->low_latency_control_supported = lowLatencyControlSupported_ ? 1u : 0u;
        value->low_latency_control_applied = lowLatencyControlApplied_ ? 1u : 0u;
        value->force_keyframe_control_supported = forceKeyframeControlSupported_ ? 1u : 0u;
        value->force_keyframe_control_applied = forceKeyframeControlApplied_ ? 1u : 0u;
        wcsncpy_s(value->encoder_name, encoderName_.c_str(), _TRUNCATE);
        wcsncpy_s(value->adapter_name, adapterName_.c_str(), _TRUNCATE);
        wcsncpy_s(value->output_type_name, outputTypeName_.c_str(), _TRUNCATE);
        value->driver_version = driverVersion_;
        return 1;
    }

    int requestKeyframe() { forceKeyframe_ = true; ++keyframeRequests_; return 1; }

    void onEvent(IMFAsyncResult* async) {
        ComPtr<IMFMediaEventGenerator> source;
        {
            std::lock_guard lock(mutex_);
            if (!running_ || !events_) return;
            source = events_;
        }
        ComPtr<IMFMediaEvent> event;
        const HRESULT result = source->EndGetEvent(async, &event);
        if (FAILED(result)) { fail(); return; }
        MediaEventType kind{};
        if (FAILED(event->GetType(&kind))) { fail(); return; }
        {
            std::lock_guard lock(mutex_);
            if (!running_) return;
            if (mediaEvents_.size() >= 32) mediaEvents_.pop_front();
            mediaEvents_.push_back(kind);
        }
        condition_.notify_one();
        if (running_ && FAILED(source->BeginGetEvent(eventSink_.Get(), nullptr))) fail();
    }

private:
    void setCaptureStage(uint32_t stage) {
        captureStageSince_ = GetTickCount64();
        captureStage_ = stage;
    }
    void setWorkerStage(uint32_t stage) {
        workerStageSince_ = GetTickCount64();
        workerStage_ = stage;
    }
    void setStartupStage(uint32_t stage) { startupStage_ = stage; }
    void rememberStartupFailure(HRESULT result, std::uint32_t operation) {
        if (firstCaptureObserved_) return;
        std::lock_guard lock(mutex_);
        if (!startupFailureOperation_) {
            startupFailureOperation_ = operation;
            startupFailureHresult_ = static_cast<int32_t>(result);
        }
    }
    void fail(HRESULT result = E_FAIL, std::uint32_t operation = PC_SCREEN_ENCODER_FAILURE_EXCEPTION) {
        rememberStartupFailure(result, operation);
        {
            std::lock_guard lock(mutex_);
            failed_ = true;
            running_ = false;
            if (failedOperation_ == PC_SCREEN_ENCODER_FAILURE_NONE) {
                lastHresult_ = static_cast<int32_t>(result);
                failedOperation_ = operation;
            }
        }
        condition_.notify_all();
    }

    bool initialize() {
        const auto rejected = [this](HRESULT result, std::uint32_t operation) {
            rememberStartupFailure(result, operation);
            std::lock_guard lock(mutex_);
            lastHresult_ = static_cast<int32_t>(result);
            failedOperation_ = operation;
            return false;
        };
        const HRESULT startup = MFStartup(MF_VERSION);
        if (FAILED(startup)) return rejected(startup, PC_SCREEN_ENCODER_FAILURE_MF_STARTUP);
        mediaFoundationStarted_ = true;
        if (!GraphicsCaptureSession::IsSupported()) return rejected(E_NOTIMPL, PC_SCREEN_ENCODER_FAILURE_WGC);
        setStartupStage(PC_SCREEN_START_SOURCE_VALIDATED);
        const HRESULT d3d = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                nullptr, 0, D3D11_SDK_VERSION, &d3d_, nullptr, &d3dContext_);
        if (FAILED(d3d)) return rejected(d3d, PC_SCREEN_ENCODER_FAILURE_D3D11);
        setStartupStage(PC_SCREEN_START_D3D_DEVICE_CREATED);
        // Only the encoder worker uses our immediate context for copy and conversion.
        // The asynchronous Media Foundation MFT shares the device through the DXGI
        // manager, so it still needs the device's context multithread protection.
        ComPtr<ID3D10Multithread> multithread;
        if (FAILED(d3d_.As(&multithread))) return rejected(E_NOINTERFACE, PC_SCREEN_ENCODER_FAILURE_D3D11);
        multithread->SetMultithreadProtected(TRUE);
        ComPtr<IDXGIDevice> dxgi;
        if (FAILED(d3d_.As(&dxgi))) return rejected(E_NOINTERFACE, PC_SCREEN_ENCODER_FAILURE_D3D11);
        ComPtr<IDXGIAdapter> adapter;
        if (SUCCEEDED(dxgi->GetAdapter(&adapter))) {
            DXGI_ADAPTER_DESC description{};
            if (SUCCEEDED(adapter->GetDesc(&description))) adapterName_ = description.Description;
            LARGE_INTEGER version{};
            if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(ID3D11Device), &version)))
                driverVersion_ = static_cast<uint64_t>(version.QuadPart);
        }
        winrt::com_ptr<IInspectable> inspectable;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()));
        auto winrtDevice = inspectable.as<IDirect3DDevice>();
        setStartupStage(PC_SCREEN_START_D3D_BRIDGE_CREATED);
        auto factory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        if (options_.source_kind == PC_SCREEN_MONITOR) {
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            auto monitor = reinterpret_cast<HMONITOR>(options_.source_handle);
            if (!GetMonitorInfoW(monitor, &info)) return rejected(HRESULT_FROM_WIN32(GetLastError()), PC_SCREEN_ENCODER_FAILURE_WGC);
            winrt::check_hresult(factory->CreateForMonitor(monitor,
                winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item_)));
        } else if (options_.source_kind == PC_SCREEN_WINDOW) {
            auto window = reinterpret_cast<HWND>(options_.source_handle);
            if (!IsWindow(window)) return rejected(E_INVALIDARG, PC_SCREEN_ENCODER_FAILURE_WGC);
            winrt::check_hresult(factory->CreateForWindow(window,
                winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item_)));
        } else return rejected(E_INVALIDARG, PC_SCREEN_ENCODER_FAILURE_WGC);
        setStartupStage(PC_SCREEN_START_WGC_ITEM_ACQUIRED);
        auto size = item_.Size();
        if (size.Width < 48 || size.Height < 48 || size.Width > 16384 ||
            size.Height > 16384 || static_cast<uint64_t>(size.Width) * size.Height > 100000000)
            return rejected(E_INVALIDARG, PC_SCREEN_ENCODER_FAILURE_WGC);
        width_ = static_cast<UINT>(size.Width) & ~1u;
        height_ = static_cast<UINT>(size.Height) & ~1u;
        if (!width_ || !height_) return rejected(E_INVALIDARG, PC_SCREEN_ENCODER_FAILURE_WGC);

        if (FAILED(d3d_.As(&videoDevice_)) || FAILED(d3dContext_.As(&videoContext_))) return rejected(E_NOINTERFACE, PC_SCREEN_ENCODER_FAILURE_VIDEO_PROCESSOR);
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
        content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        content.InputFrameRate = {options_.frames_per_second, 1};
        content.InputWidth = width_;
        content.InputHeight = height_;
        content.OutputFrameRate = content.InputFrameRate;
        content.OutputWidth = width_;
        content.OutputHeight = height_;
        content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
        const HRESULT enumerator = videoDevice_->CreateVideoProcessorEnumerator(&content, &videoEnumerator_);
        if (FAILED(enumerator)) return rejected(enumerator, PC_SCREEN_ENCODER_FAILURE_VIDEO_PROCESSOR);
        const HRESULT processor = videoDevice_->CreateVideoProcessor(videoEnumerator_.Get(), 0, &videoProcessor_);
        if (FAILED(processor)) return rejected(processor, PC_SCREEN_ENCODER_FAILURE_VIDEO_PROCESSOR);

        setStartupStage(PC_SCREEN_START_ENCODER_ENUMERATION);
        MFT_REGISTER_TYPE_INFO outputType{MFMediaType_Video, MFVideoFormat_H264};
        IMFActivate** activations = nullptr;
        UINT32 count = 0;
        const bool compatibility = options_.backend == PC_SCREEN_BACKEND_WEBRTC_COMPATIBILITY;
        const HRESULT enumeration = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
                (compatibility ? MFT_ENUM_FLAG_SYNCMFT : MFT_ENUM_FLAG_HARDWARE) | MFT_ENUM_FLAG_SORTANDFILTER,
                nullptr, &outputType, &activations, &count);
        if (FAILED(enumeration) || !count) return rejected(FAILED(enumeration) ? enumeration : MF_E_TOPO_CODEC_NOT_FOUND,
            PC_SCREEN_ENCODER_FAILURE_ENUMERATE_MFT);
        UINT32 nameLength = 0;
        if (SUCCEEDED(activations[0]->GetStringLength(MFT_FRIENDLY_NAME_Attribute, &nameLength)) && nameLength) {
            std::vector<wchar_t> name(static_cast<size_t>(nameLength) + 1);
            if (SUCCEEDED(activations[0]->GetString(MFT_FRIENDLY_NAME_Attribute, name.data(),
                    static_cast<UINT32>(name.size()), nullptr))) encoderName_ = name.data();
        }
        const HRESULT activation = activations[0]->ActivateObject(IID_PPV_ARGS(&encoder_));
        for (UINT32 index = 0; index < count; ++index) activations[index]->Release();
        CoTaskMemFree(activations);
        if (FAILED(activation)) return rejected(activation, PC_SCREEN_ENCODER_FAILURE_ACTIVATE_MFT);
        setStartupStage(PC_SCREEN_START_ENCODER_CREATED);
        // Request the standard CBR mean-rate controls when this hardware MFT
        // implements them.  MF_MT_AVG_BITRATE remains the compatibility path;
        // unsupported codec controls are non-fatal and never change quality
        // policy on their own.
        ComPtr<ICodecAPI> codec;
        if (SUCCEEDED(encoder_.As(&codec))) {
            cbrControlSupported_ = true;
            VARIANT value{};
            VariantInit(&value);
            value.vt = VT_UI4;
            value.ulVal = eAVEncCommonRateControlMode_CBR;
            const HRESULT mode = codec->SetValue(&CODECAPI_AVEncCommonRateControlMode, &value);
            value.ulVal = options_.bitrate_bits_per_second;
            const HRESULT bitrate = codec->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &value);
            cbrControlApplied_ = SUCCEEDED(mode) && SUCCEEDED(bitrate);
            VariantClear(&value);
        }
        ComPtr<IMFAttributes> attributes;
        const HRESULT encoderAttributes = encoder_->GetAttributes(&attributes);
        if (FAILED(encoderAttributes)) return rejected(encoderAttributes, PC_SCREEN_ENCODER_FAILURE_PROCESS_MESSAGE);
        if (!compatibility) {
            const HRESULT asynchronous = attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
            if (FAILED(asynchronous)) return rejected(asynchronous, PC_SCREEN_ENCODER_FAILURE_PROCESS_MESSAGE);
        }
        lowLatencyControlSupported_ = true;
        const HRESULT lowLatency = attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
        lowLatencyControlApplied_ = SUCCEEDED(lowLatency);
        if (!compatibility) {
            UINT token = 0;
            const HRESULT manager = MFCreateDXGIDeviceManager(&token, &deviceManager_);
            if (FAILED(manager)) return rejected(manager, PC_SCREEN_ENCODER_FAILURE_DXGI_MANAGER);
            const HRESULT reset = deviceManager_->ResetDevice(d3d_.Get(), token);
            if (FAILED(reset)) return rejected(reset, PC_SCREEN_ENCODER_FAILURE_DXGI_MANAGER);
            const HRESULT managerMessage = encoder_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,
                reinterpret_cast<ULONG_PTR>(deviceManager_.Get()));
            if (FAILED(managerMessage)) return rejected(managerMessage, PC_SCREEN_ENCODER_FAILURE_DXGI_MANAGER);
        }

        ComPtr<IMFMediaType> mediaOutput;
        if (FAILED(MFCreateMediaType(&mediaOutput)) ||
            FAILED(mediaOutput->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
            FAILED(mediaOutput->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264)) ||
            FAILED(MFSetAttributeSize(mediaOutput.Get(), MF_MT_FRAME_SIZE, width_, height_)) ||
            FAILED(MFSetAttributeRatio(mediaOutput.Get(), MF_MT_FRAME_RATE,
                options_.frames_per_second, 1)) ||
            FAILED(MFSetAttributeRatio(mediaOutput.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1)) ||
            FAILED(mediaOutput->SetUINT32(MF_MT_AVG_BITRATE, options_.bitrate_bits_per_second)) ||
            FAILED(mediaOutput->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive)) ||
            FAILED(encoder_->SetOutputType(0, mediaOutput.Get(), 0)))
            return rejected(E_FAIL, PC_SCREEN_ENCODER_FAILURE_MEDIA_TYPE);
        recordOutputType(mediaOutput.Get());
        ComPtr<IMFMediaType> mediaInput;
        if (FAILED(MFCreateMediaType(&mediaInput)) ||
            FAILED(mediaInput->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
            FAILED(mediaInput->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12)) ||
            FAILED(MFSetAttributeSize(mediaInput.Get(), MF_MT_FRAME_SIZE, width_, height_)) ||
            FAILED(MFSetAttributeRatio(mediaInput.Get(), MF_MT_FRAME_RATE,
                options_.frames_per_second, 1)) ||
            FAILED(MFSetAttributeRatio(mediaInput.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1)) ||
            FAILED(mediaInput->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive)) ||
            FAILED(encoder_->SetInputType(0, mediaInput.Get(), 0))) return rejected(E_FAIL, PC_SCREEN_ENCODER_FAILURE_MEDIA_TYPE);
        if (!compatibility) {
            if (FAILED(encoder_.As(&events_))) return rejected(E_FAIL, PC_SCREEN_ENCODER_FAILURE_MEDIA_TYPE);
            eventSink_.Attach(new EventSink(weak_from_this()));
        }

        pool_ = Direct3D11CaptureFramePool::CreateFreeThreaded(
            winrtDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
        setStartupStage(PC_SCREEN_START_FRAME_POOL_CREATED);
        auto weak = weak_from_this();
        frameToken_ = pool_.FrameArrived([weak](Direct3D11CaptureFramePool const& sender,
            winrt::Windows::Foundation::IInspectable const&) {
            if (auto owner = weak.lock()) owner->onCapture(sender);
        });
        setStartupStage(PC_SCREEN_START_CALLBACK_REGISTERED);
        session_ = pool_.CreateCaptureSession(item_);
        setStartupStage(PC_SCREEN_START_CAPTURE_SESSION_CREATED);
        session_.IsCursorCaptureEnabled(options_.capture_cursor != 0);
        const HRESULT begin = encoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        if (FAILED(begin)) return rejected(begin, PC_SCREEN_ENCODER_FAILURE_PROCESS_MESSAGE);
        const HRESULT start = encoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        if (FAILED(start)) return rejected(start, PC_SCREEN_ENCODER_FAILURE_PROCESS_MESSAGE);
        if (!compatibility) {
            const HRESULT event = events_->BeginGetEvent(eventSink_.Get(), nullptr);
            if (FAILED(event)) return rejected(event, PC_SCREEN_ENCODER_FAILURE_PROCESS_MESSAGE);
        }
        session_.StartCapture();
        setStartupStage(PC_SCREEN_START_CAPTURE_STARTED);
        return true;
    }

    void onCapture(Direct3D11CaptureFramePool const& sender) {
        if (!running_) return;
        ++captureCallbackCount_;
        lastCaptureCallbackTick_ = GetTickCount64();
        setCaptureStage(PC_SCREEN_CAPTURE_NEXT);
        try {
            auto frame = sender.TryGetNextFrame();
            const auto now = std::chrono::steady_clock::now();
            {
                std::lock_guard lock(mutex_);
                if (lastCapture_ != std::chrono::steady_clock::time_point{} &&
                    now - lastCapture_ < std::chrono::nanoseconds(
                        1000000000ull / options_.frames_per_second)) {
                    setCaptureStage(PC_SCREEN_CAPTURE_IDLE);
                    return;
                }
                lastCapture_ = now;
            }
            setCaptureStage(PC_SCREEN_CAPTURE_QUEUE);
            {
                std::lock_guard lock(mutex_);
                if (!running_) return;
                ++capturedFrames_;
                firstCaptureObserved_ = true;
                setStartupStage(PC_SCREEN_START_FIRST_CAPTURED);
                if (pending_.frame) ++droppedBeforeEncode_;
                pending_ = {std::move(frame), now};
            }
            condition_.notify_one();
            setCaptureStage(PC_SCREEN_CAPTURE_IDLE);
        } catch (...) { fail(); }
    }

    bool submit(CapturedFrame frame) {
        auto access = frame.frame.Surface().as<
            ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        ComPtr<ID3D11Texture2D> source;
        if (FAILED(access->GetInterface(IID_PPV_ARGS(&source)))) return false;
        D3D11_TEXTURE2D_DESC copyDesc{};
        source->GetDesc(&copyDesc);
        if (copyDesc.Width < width_ || copyDesc.Height < height_) return false;
        copyDesc.BindFlags = 0;
        copyDesc.CPUAccessFlags = 0;
        copyDesc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> copy;
        setWorkerStage(PC_SCREEN_ENCODER_CREATE_COPY_TEXTURE);
        if (FAILED(d3d_->CreateTexture2D(&copyDesc, nullptr, &copy))) return false;
        setWorkerStage(PC_SCREEN_ENCODER_COPY_CAPTURE);
        d3dContext_->CopyResource(copy.Get(), source.Get());
        setWorkerStage(PC_SCREEN_ENCODER_CREATE_SURFACES);
        D3D11_TEXTURE2D_DESC nv12Desc{};
        nv12Desc.Width = width_;
        nv12Desc.Height = height_;
        nv12Desc.MipLevels = 1;
        nv12Desc.ArraySize = 1;
        nv12Desc.Format = DXGI_FORMAT_NV12;
        nv12Desc.SampleDesc.Count = 1;
        nv12Desc.Usage = D3D11_USAGE_DEFAULT;
        nv12Desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> nv12;
        if (FAILED(d3d_->CreateTexture2D(&nv12Desc, nullptr, &nv12))) return false;
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDesc{};
        inputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11VideoProcessorInputView> input;
        if (FAILED(videoDevice_->CreateVideoProcessorInputView(
                copy.Get(), videoEnumerator_.Get(), &inputDesc, &input))) return false;
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDesc{};
        outputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11VideoProcessorOutputView> output;
        if (FAILED(videoDevice_->CreateVideoProcessorOutputView(
                nv12.Get(), videoEnumerator_.Get(), &outputDesc, &output))) return false;
        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = input.Get();
        setWorkerStage(PC_SCREEN_ENCODER_VIDEO_BLT);
        if (FAILED(videoContext_->VideoProcessorBlt(
                videoProcessor_.Get(), output.Get(), 0, 1, &stream))) return false;
        setWorkerStage(PC_SCREEN_ENCODER_CREATE_SAMPLE);
        ComPtr<IMFMediaBuffer> surface;
        if (options_.backend == PC_SCREEN_BACKEND_WEBRTC_COMPATIBILITY) {
            D3D11_TEXTURE2D_DESC stagingDesc = nv12Desc;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.BindFlags = 0;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging;
            if (FAILED(d3d_->CreateTexture2D(&stagingDesc, nullptr, &staging))) return false;
            d3dContext_->CopyResource(staging.Get(), nv12.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(d3dContext_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
            const DWORD length = width_ * height_ * 3 / 2;
            const HRESULT allocation = MFCreateMemoryBuffer(length, &surface);
            if (FAILED(allocation)) { d3dContext_->Unmap(staging.Get(), 0); return false; }
            BYTE* destination = nullptr;
            const HRESULT locked = surface->Lock(&destination, nullptr, nullptr);
            if (FAILED(locked)) { d3dContext_->Unmap(staging.Get(), 0); return false; }
            for (UINT32 row = 0; row < height_; ++row)
                std::memcpy(destination + row * width_, static_cast<const BYTE*>(mapped.pData) + row * mapped.RowPitch, width_);
            for (UINT32 row = 0; row < height_ / 2; ++row)
                std::memcpy(destination + width_ * height_ + row * width_,
                    static_cast<const BYTE*>(mapped.pData) + (height_ + row) * mapped.RowPitch, width_);
            surface->Unlock();
            surface->SetCurrentLength(length);
            d3dContext_->Unmap(staging.Get(), 0);
        } else if (FAILED(MFCreateDXGISurfaceBuffer(
                __uuidof(ID3D11Texture2D), nv12.Get(), 0, FALSE, &surface))) return false;
        ComPtr<IMFSample> sample;
        if (FAILED(MFCreateSample(&sample)) || FAILED(sample->AddBuffer(surface.Get()))) return false;
        const auto time = static_cast<LONGLONG>(frameIndex_++) * 10000000ll /
            options_.frames_per_second;
        if (FAILED(sample->SetSampleTime(time)) ||
            FAILED(sample->SetSampleDuration(10000000ll / options_.frames_per_second))) return false;
        if(forceKeyframe_.exchange(false)) {
            ComPtr<ICodecAPI> codec;
            if (SUCCEEDED(encoder_.As(&codec))) {
                forceKeyframeControlSupported_ = true;
                VARIANT value{};
                VariantInit(&value);
                value.vt = VT_UI4;
                value.ulVal = 1;
                forceKeyframeControlApplied_ = SUCCEEDED(codec->SetValue(&CODECAPI_AVEncVideoForceKeyFrame, &value));
                VariantClear(&value);
            }
            // Retained as the compatible fallback for MFTs without ICodecAPI.
            sample->SetUINT32(MFSampleExtension_CleanPoint,TRUE);
        }
        setWorkerStage(PC_SCREEN_ENCODER_INPUT);
        const HRESULT processInput = encoder_->ProcessInput(0, sample.Get(), 0);
        {
            std::lock_guard lock(mutex_);
            processInputHresult_ = static_cast<int32_t>(processInput);
            if (FAILED(processInput)) ++processInputFailures_;
        }
        if (FAILED(processInput)) {
            fail(processInput, PC_SCREEN_ENCODER_FAILURE_PROCESS_INPUT);
            return false;
        }
        firstEncoderInputObserved_ = true;
        setStartupStage(PC_SCREEN_START_FIRST_ENCODER_INPUT);
        const bool accepted = true;
        if (accepted) { std::lock_guard lock(mutex_); ++submittedFrames_; }
        return accepted;
    }

    void recordOutputType(IMFMediaType* type) {
        if (!type) return;
        GUID subtype{};
        UINT32 width = 0, height = 0, numerator = 0, denominator = 0;
        type->GetGUID(MF_MT_SUBTYPE, &subtype);
        MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height);
        MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &numerator, &denominator);
        std::lock_guard lock(mutex_);
        outputTypeName_ = subtype == MFVideoFormat_H264 ? L"H.264" : L"unexpected";
        outputTypeWidth_ = width;
        outputTypeHeight_ = height;
        outputTypeFramesPerSecond_ = denominator ? numerator / denominator : 0;
    }

    bool renegotiateOutputType() {
        // An MFT can request a new output type after its first input. Keep the
        // same encoder and logical media generation: this is format control
        // flow, not an ICE/DTLS or local capture failure.
        if (++outputTypeRenegotiationAttempts_ > kMaxOutputTypeRenegotiations) {
            std::lock_guard lock(mutex_);
            outputTypeRenegotiationHresult_ = static_cast<int32_t>(MF_E_TRANSFORM_STREAM_CHANGE);
            return false;
        }
        HRESULT result = MF_E_NO_MORE_TYPES;
        for (DWORD index = 0;; ++index) {
            ComPtr<IMFMediaType> available;
            const HRESULT candidate = encoder_->GetOutputAvailableType(0, index, &available);
            if (candidate == MF_E_NO_MORE_TYPES) break;
            if (FAILED(candidate)) { result = candidate; break; }
            GUID major{}, subtype{};
            if (FAILED(available->GetGUID(MF_MT_MAJOR_TYPE, &major)) ||
                FAILED(available->GetGUID(MF_MT_SUBTYPE, &subtype)) ||
                major != MFMediaType_Video || subtype != MFVideoFormat_H264) continue;
            // The available type is supplied by this exact MFT after its
            // format change, so use it intact rather than forcing stale output
            // attributes back onto an Intel/AMD implementation.
            result = encoder_->SetOutputType(0, available.Get(), 0);
            if (SUCCEEDED(result)) {
                ComPtr<IMFMediaType> current;
                if (SUCCEEDED(encoder_->GetOutputCurrentType(0, &current))) recordOutputType(current.Get());
                break;
            }
        }
        {
            std::lock_guard lock(mutex_);
            outputTypeRenegotiationHresult_ = static_cast<int32_t>(result);
            if (SUCCEEDED(result)) ++outputTypeRenegotiationCount_;
        }
        return SUCCEEDED(result);
    }

    bool collect() {
        setWorkerStage(PC_SCREEN_ENCODER_OUTPUT);
        MFT_OUTPUT_STREAM_INFO info{};
        if (FAILED(encoder_->GetOutputStreamInfo(0, &info))) return false;
        MFT_OUTPUT_DATA_BUFFER result{};
        ComPtr<IMFSample> supplied;
        ComPtr<IMFMediaBuffer> suppliedBuffer;
        if (!(info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
            if (FAILED(MFCreateSample(&supplied)) ||
                FAILED(MFCreateMemoryBuffer(info.cbSize ? info.cbSize : 1048576, &suppliedBuffer)) ||
                FAILED(supplied->AddBuffer(suppliedBuffer.Get()))) return false;
            result.pSample = supplied.Get();
        }
        DWORD status = 0;
        const HRESULT output = encoder_->ProcessOutput(0, 1, &result, &status);
        {
            std::lock_guard lock(mutex_);
            processOutputHresult_ = static_cast<int32_t>(output);
            if (output == MF_E_TRANSFORM_STREAM_CHANGE) ++streamChangeCount_;
            else if (FAILED(output) && output != MF_E_TRANSFORM_NEED_MORE_INPUT) ++processOutputFailures_;
        }
        if (result.pEvents) { result.pEvents->Release(); result.pEvents = nullptr; }
        if (output == MF_E_TRANSFORM_NEED_MORE_INPUT) return true;
        if (output == MF_E_TRANSFORM_STREAM_CHANGE) {
            if (!supplied && result.pSample) { result.pSample->Release(); result.pSample = nullptr; }
            if (!renegotiateOutputType()) { fail(MF_E_TRANSFORM_STREAM_CHANGE, PC_SCREEN_ENCODER_FAILURE_MEDIA_TYPE); return false; }
            // This is an asynchronous MFT. The stream-change notification has
            // consumed the current output-ready event; Intel Quick Sync can
            // return E_UNEXPECTED if ProcessOutput is re-entered before it
            // raises the next METransformHaveOutput event. The worker will
            // resume through that normal event path (or a NeedInput event).
            return true;
        }
        if (FAILED(output)) { fail(output, PC_SCREEN_ENCODER_FAILURE_PROCESS_OUTPUT); return false; }
        ComPtr<IMFSample> sample;
        if (supplied) sample = supplied;
        else sample.Attach(result.pSample);
        ComPtr<IMFMediaBuffer> bytes;
        if (FAILED(sample->ConvertToContiguousBuffer(&bytes))) return false;
        BYTE* pointer = nullptr;
        DWORD length = 0;
        if (FAILED(bytes->Lock(&pointer, nullptr, &length))) return false;
        EncodedFrame frame;
        frame.bytes.assign(pointer, pointer + length);
        bytes->Unlock();
        frame.info.structure_size = sizeof(frame.info);
        frame.info.length = length;
        frame.info.width = width_;
        frame.info.height = height_;
        LONGLONG time = 0;
        sample->GetSampleTime(&time);
        frame.info.timestamp_100ns = time >= 0 ? static_cast<uint64_t>(time) : 0;
        uint64_t sps=0,pps=0,idr=0,nonIdr=0;
        const bool bootstrap = h264_nals(frame.bytes, sps, pps, idr, nonIdr);
        frame.bootstrap = bootstrap;
        frame.info.keyframe = bootstrap ? 1u : 0u;
        frame.info.idr = idr ? 1u : 0u;
        frame.info.codec_configuration_only = (sps || pps) && !idr && !nonIdr ? 1u : 0u;
        {
            std::lock_guard lock(mutex_);
            ++encodedFrames_;
            frame.info.encoded_sequence = encodedFrames_;
            spsNals_ += sps;
            ppsNals_ += pps;
            idrNals_ += idr;
            nonIdrNals_ += nonIdr;
            // Real-time deltas are expendable. A new receiver's only SPS/PPS/IDR
            // bootstrap is not: evict a delta first and drop a new delta when the
            // bounded queue contains bootstrap material only.
            if (output_.size() == 4) {
                auto discard = std::find_if(output_.begin(), output_.end(),
                    [](const auto& value) { return !value.bootstrap; });
                if (discard != output_.end()) {
                    output_.erase(discard);
                    ++droppedEncodedQueue_;
                } else if (!frame.bootstrap) {
                    ++droppedEncodedQueue_;
                    return true;
                } else {
                    output_.pop_front();
                    ++droppedEncodedQueue_;
                }
            }
            output_.push_back(std::move(frame));
        }
        return true;
    }

    void run() {
        try {
            setWorkerStage(PC_SCREEN_ENCODER_INITIALIZING);
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            if (!initialize()) fail();
            else {
                {
                    std::lock_guard lock(mutex_);
                    initialized_ = true;
                }
                condition_.notify_all();
                if (options_.backend == PC_SCREEN_BACKEND_WEBRTC_COMPATIBILITY) {
                    while (running_) {
                        CapturedFrame frame;
                        {
                            std::unique_lock lock(mutex_);
                            condition_.wait(lock, [this] { return !running_ || pending_.frame; });
                            if (!running_) break;
                            frame = std::move(pending_);
                        }
                        if (!submit(std::move(frame))) { fail(); break; }
                        // A synchronous MFT can produce more than one output per input.
                        for (unsigned attempt = 0; attempt < 16 && running_; ++attempt) {
                            if (!collect()) { fail(); break; }
                            if (processOutputHresult_ == static_cast<int32_t>(MF_E_TRANSFORM_NEED_MORE_INPUT)) break;
                        }
                    }
                } else while (running_) {
                    setWorkerStage(PC_SCREEN_ENCODER_WAITING);
                    MediaEventType event{};
                    CapturedFrame frame;
                    bool submitFrame = false, handleEvent = false;
                    {
                        std::unique_lock lock(mutex_);
                        condition_.wait(lock, [this] {
                            return !running_ || !mediaEvents_.empty() ||
                                (inputReady_ && pending_.frame);
                        });
                        if (!running_) break;
                        if (!mediaEvents_.empty()) {
                            event = mediaEvents_.front();
                            mediaEvents_.pop_front();
                            handleEvent = true;
                            if (event == METransformNeedInput) inputReady_ = true;
                        } else if (inputReady_ && pending_.frame) {
                            frame = std::move(pending_);
                            inputReady_ = false;
                            submitFrame = true;
                        }
                    }
                    if (handleEvent && event == METransformHaveOutput && !collect()) fail();
                    if (submitFrame && !submit(std::move(frame))) fail();
                }
            }
        } catch (...) { fail(); }
        running_ = false;
        setWorkerStage(PC_SCREEN_ENCODER_STOPPING);
        if (pool_) pool_.FrameArrived(frameToken_);
        if (session_) session_.Close();
        if (pool_) pool_.Close();
        if (options_.backend == PC_SCREEN_BACKEND_WEBRTC_COMPATIBILITY) {
            // WGC frames and capture objects were created on this apartment.
            // Do not leave a pending frame to be released by the caller's thread
            // after this worker has torn down its WinRT apartment.
            std::lock_guard lock(mutex_);
            pending_ = {};
            session_ = nullptr;
            pool_ = nullptr;
            item_ = nullptr;
        }
        {
            std::lock_guard lock(mutex_);
            events_.Reset();
        }
        if (encoder_) encoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        eventSink_.Reset();
        encoder_.Reset();
        if (mediaFoundationStarted_) MFShutdown();
        setWorkerStage(PC_SCREEN_ENCODER_STOPPED);
        condition_.notify_all();
    }

    pc_screen_encoder_options options_{};
    std::thread worker_;
    std::atomic<bool> running_{true};
    std::mutex mutex_;
    std::condition_variable condition_;
    bool initialized_ = false, failed_ = false, inputReady_ = false;
    bool mediaFoundationStarted_ = false;
    std::chrono::steady_clock::time_point lastCapture_{};
    CapturedFrame pending_;
    std::deque<MediaEventType> mediaEvents_;
    std::deque<EncodedFrame> output_;
    uint64_t frameIndex_ = 0;
    uint64_t capturedFrames_ = 0, submittedFrames_ = 0, encodedFrames_ = 0;
    uint64_t droppedBeforeEncode_ = 0, droppedEncodedQueue_ = 0;
    uint64_t keyframeRequests_ = 0, spsNals_ = 0, ppsNals_ = 0, idrNals_ = 0, nonIdrNals_ = 0;
    std::uint32_t failedOperation_ = PC_SCREEN_ENCODER_FAILURE_NONE;
    int32_t lastHresult_ = 0, processInputHresult_ = 0, processOutputHresult_ = 0;
    uint64_t processInputFailures_ = 0, processOutputFailures_ = 0;
    std::uint32_t startupFailureOperation_ = 0;
    int32_t startupFailureHresult_ = 0;
    std::atomic<uint32_t> startupStage_{PC_SCREEN_START_NONE};
    std::atomic<bool> firstCaptureObserved_{false}, firstEncoderInputObserved_{false};
    static constexpr uint32_t kMaxOutputTypeRenegotiations = 4;
    uint64_t streamChangeCount_ = 0, outputTypeRenegotiationCount_ = 0;
    uint32_t outputTypeRenegotiationAttempts_ = 0;
    int32_t outputTypeRenegotiationHresult_ = 0;
    uint32_t outputTypeWidth_ = 0, outputTypeHeight_ = 0, outputTypeFramesPerSecond_ = 0;
    std::atomic<bool> cbrControlSupported_{false}, cbrControlApplied_{false};
    std::atomic<bool> lowLatencyControlSupported_{false}, lowLatencyControlApplied_{false};
    std::atomic<bool> forceKeyframeControlSupported_{false}, forceKeyframeControlApplied_{false};
    std::wstring encoderName_, adapterName_, outputTypeName_;
    uint64_t driverVersion_ = 0;
    std::atomic<bool> forceKeyframe_{false};
    std::atomic<uint64_t> captureCallbackCount_{0}, lastCaptureCallbackTick_{0};
    std::atomic<uint32_t> captureStage_{PC_SCREEN_CAPTURE_IDLE};
    std::atomic<uint64_t> captureStageSince_{0};
    std::atomic<uint32_t> workerStage_{PC_SCREEN_ENCODER_INITIALIZING};
    std::atomic<uint64_t> workerStageSince_{0};
    UINT width_ = 0, height_ = 0;
    GraphicsCaptureItem item_{nullptr};
    Direct3D11CaptureFramePool pool_{nullptr};
    GraphicsCaptureSession session_{nullptr};
    winrt::event_token frameToken_{};
    ComPtr<ID3D11Device> d3d_;
    ComPtr<ID3D11DeviceContext> d3dContext_;
    ComPtr<ID3D11VideoDevice> videoDevice_;
    ComPtr<ID3D11VideoContext> videoContext_;
    ComPtr<ID3D11VideoProcessorEnumerator> videoEnumerator_;
    ComPtr<ID3D11VideoProcessor> videoProcessor_;
    ComPtr<IMFDXGIDeviceManager> deviceManager_;
    ComPtr<IMFTransform> encoder_;
    ComPtr<IMFMediaEventGenerator> events_;
    ComPtr<IMFAsyncCallback> eventSink_;
};

STDMETHODIMP EventSink::Invoke(IMFAsyncResult* result) {
    if (auto owner = owner_.lock()) owner->onEvent(result);
    return S_OK;
}
}

struct pc_screen_encoder { std::shared_ptr<EncoderState> state; };

extern "C" pc_screen_encoder* __cdecl pc_screen_encoder_open(
    const pc_screen_encoder_options* options) {
    if (!options || options->structure_size != sizeof(*options) ||
        (options->source_kind != PC_SCREEN_MONITOR && options->source_kind != PC_SCREEN_WINDOW) ||
        !options->source_handle || options->backend > PC_SCREEN_BACKEND_WEBRTC_COMPATIBILITY || options->frames_per_second < 1 ||
        options->frames_per_second > 240 || options->bitrate_bits_per_second < 100000 ||
        options->bitrate_bits_per_second > 500000000) return nullptr;
    try {
        auto state = std::make_shared<EncoderState>(*options);
        if (!state->start()) return nullptr;
        return new pc_screen_encoder{std::move(state)};
    } catch (...) { return nullptr; }
}

extern "C" int __cdecl pc_screen_encoder_pop(pc_screen_encoder* encoder,
    uint8_t* output, uint32_t output_capacity, pc_screen_encoded_frame* frame) {
    try { return encoder && encoder->state ? encoder->state->pop(output, output_capacity, frame) : -2; }
    catch (...) { return -2; }
}

extern "C" int __cdecl pc_screen_encoder_request_keyframe(pc_screen_encoder* encoder) {
    try { return encoder && encoder->state ? encoder->state->requestKeyframe() : 0; }
    catch (...) { return 0; }
}

extern "C" int __cdecl pc_screen_encoder_status_get(pc_screen_encoder* encoder,
    pc_screen_encoder_status* status) {
    try { return encoder && encoder->state ? encoder->state->status(status) : 0; }
    catch (...) { return 0; }
}

extern "C" void __cdecl pc_screen_encoder_close(pc_screen_encoder* encoder) {
    if (!encoder) return;
    try { encoder->state->stop(); } catch (...) {}
    delete encoder;
}

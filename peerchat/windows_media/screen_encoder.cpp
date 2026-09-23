#include "peerchat_screen_media.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wrl/client.h>
#include <Windows.Graphics.Capture.Interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

namespace {
struct EncodedFrame {
    std::vector<uint8_t> bytes;
    pc_screen_encoded_frame info{};
};

struct CapturedFrame {
    ComPtr<ID3D11Texture2D> texture;
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
        *value = {sizeof(*value), capturedFrames_, submittedFrames_, encodedFrames_,
            droppedBeforeEncode_, droppedEncodedQueue_};
        return 1;
    }

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
    void fail() {
        {
            std::lock_guard lock(mutex_);
            failed_ = true;
            running_ = false;
        }
        condition_.notify_all();
    }

    bool initialize() {
        if (FAILED(MFStartup(MF_VERSION))) return false;
        mediaFoundationStarted_ = true;
        if (!GraphicsCaptureSession::IsSupported()) return false;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                nullptr, 0, D3D11_SDK_VERSION, &d3d_, nullptr, &d3dContext_))) return false;
        ComPtr<IDXGIDevice> dxgi;
        if (FAILED(d3d_.As(&dxgi))) return false;
        winrt::com_ptr<IInspectable> inspectable;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()));
        auto winrtDevice = inspectable.as<IDirect3DDevice>();
        auto factory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        if (options_.source_kind == PC_SCREEN_MONITOR) {
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            auto monitor = reinterpret_cast<HMONITOR>(options_.source_handle);
            if (!GetMonitorInfoW(monitor, &info)) return false;
            winrt::check_hresult(factory->CreateForMonitor(monitor,
                winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item_)));
        } else if (options_.source_kind == PC_SCREEN_WINDOW) {
            auto window = reinterpret_cast<HWND>(options_.source_handle);
            if (!IsWindow(window)) return false;
            winrt::check_hresult(factory->CreateForWindow(window,
                winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item_)));
        } else return false;
        auto size = item_.Size();
        if (size.Width < 48 || size.Height < 48 || size.Width > 16384 ||
            size.Height > 16384 || static_cast<uint64_t>(size.Width) * size.Height > 100000000)
            return false;
        width_ = static_cast<UINT>(size.Width) & ~1u;
        height_ = static_cast<UINT>(size.Height) & ~1u;
        if (!width_ || !height_) return false;

        if (FAILED(d3d_.As(&videoDevice_)) || FAILED(d3dContext_.As(&videoContext_))) return false;
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
        content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        content.InputFrameRate = {options_.frames_per_second, 1};
        content.InputWidth = width_;
        content.InputHeight = height_;
        content.OutputFrameRate = content.InputFrameRate;
        content.OutputWidth = width_;
        content.OutputHeight = height_;
        content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
        if (FAILED(videoDevice_->CreateVideoProcessorEnumerator(&content, &videoEnumerator_)) ||
            FAILED(videoDevice_->CreateVideoProcessor(videoEnumerator_.Get(), 0, &videoProcessor_)))
            return false;

        MFT_REGISTER_TYPE_INFO outputType{MFMediaType_Video, MFVideoFormat_H264};
        IMFActivate** activations = nullptr;
        UINT32 count = 0;
        if (FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
                MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
                nullptr, &outputType, &activations, &count)) || !count) return false;
        const HRESULT activation = activations[0]->ActivateObject(IID_PPV_ARGS(&encoder_));
        for (UINT32 index = 0; index < count; ++index) activations[index]->Release();
        CoTaskMemFree(activations);
        if (FAILED(activation)) return false;
        ComPtr<IMFAttributes> attributes;
        if (FAILED(encoder_->GetAttributes(&attributes)) ||
            FAILED(attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE))) return false;
        UINT token = 0;
        if (FAILED(MFCreateDXGIDeviceManager(&token, &deviceManager_)) ||
            FAILED(deviceManager_->ResetDevice(d3d_.Get(), token)) ||
            FAILED(encoder_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,
                reinterpret_cast<ULONG_PTR>(deviceManager_.Get())))) return false;

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
            FAILED(encoder_->SetOutputType(0, mediaOutput.Get(), 0))) return false;
        ComPtr<IMFMediaType> mediaInput;
        if (FAILED(MFCreateMediaType(&mediaInput)) ||
            FAILED(mediaInput->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
            FAILED(mediaInput->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12)) ||
            FAILED(MFSetAttributeSize(mediaInput.Get(), MF_MT_FRAME_SIZE, width_, height_)) ||
            FAILED(MFSetAttributeRatio(mediaInput.Get(), MF_MT_FRAME_RATE,
                options_.frames_per_second, 1)) ||
            FAILED(MFSetAttributeRatio(mediaInput.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1)) ||
            FAILED(mediaInput->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive)) ||
            FAILED(encoder_->SetInputType(0, mediaInput.Get(), 0)) ||
            FAILED(encoder_.As(&events_))) return false;
        eventSink_.Attach(new EventSink(weak_from_this()));

        pool_ = Direct3D11CaptureFramePool::CreateFreeThreaded(
            winrtDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
        auto weak = weak_from_this();
        frameToken_ = pool_.FrameArrived([weak](Direct3D11CaptureFramePool const& sender,
            winrt::Windows::Foundation::IInspectable const&) {
            if (auto owner = weak.lock()) owner->onCapture(sender);
        });
        session_ = pool_.CreateCaptureSession(item_);
        session_.IsCursorCaptureEnabled(options_.capture_cursor != 0);
        if (FAILED(encoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0)) ||
            FAILED(encoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0)) ||
            FAILED(events_->BeginGetEvent(eventSink_.Get(), nullptr))) return false;
        session_.StartCapture();
        return true;
    }

    void onCapture(Direct3D11CaptureFramePool const& sender) {
        if (!running_) return;
        try {
            auto frame = sender.TryGetNextFrame();
            auto access = frame.Surface().as<
                ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            ComPtr<ID3D11Texture2D> source;
            if (FAILED(access->GetInterface(IID_PPV_ARGS(&source)))) { fail(); return; }
            D3D11_TEXTURE2D_DESC desc{};
            source->GetDesc(&desc);
            if (desc.Width < width_ || desc.Height < height_) { fail(); return; }
            const auto now = std::chrono::steady_clock::now();
            {
                std::lock_guard lock(mutex_);
                if (lastCapture_ != std::chrono::steady_clock::time_point{} &&
                    now - lastCapture_ < std::chrono::nanoseconds(
                        1000000000ull / options_.frames_per_second)) return;
                lastCapture_ = now;
            }
            desc.BindFlags = 0;
            desc.CPUAccessFlags = 0;
            desc.MiscFlags = 0;
            ComPtr<ID3D11Texture2D> copy;
            if (FAILED(d3d_->CreateTexture2D(&desc, nullptr, &copy))) { fail(); return; }
            {
                std::lock_guard gpu(gpuMutex_);
                d3dContext_->CopyResource(copy.Get(), source.Get());
            }
            {
                std::lock_guard lock(mutex_);
                if (!running_) return;
                ++capturedFrames_;
                if (pending_.texture) ++droppedBeforeEncode_;
                pending_ = {std::move(copy), now};
            }
            condition_.notify_one();
        } catch (...) { fail(); }
    }

    bool submit(CapturedFrame frame) {
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
                frame.texture.Get(), videoEnumerator_.Get(), &inputDesc, &input))) return false;
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDesc{};
        outputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11VideoProcessorOutputView> output;
        if (FAILED(videoDevice_->CreateVideoProcessorOutputView(
                nv12.Get(), videoEnumerator_.Get(), &outputDesc, &output))) return false;
        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = input.Get();
        {
            std::lock_guard gpu(gpuMutex_);
            if (FAILED(videoContext_->VideoProcessorBlt(
                    videoProcessor_.Get(), output.Get(), 0, 1, &stream))) return false;
        }
        ComPtr<IMFMediaBuffer> surface;
        if (FAILED(MFCreateDXGISurfaceBuffer(
                __uuidof(ID3D11Texture2D), nv12.Get(), 0, FALSE, &surface))) return false;
        ComPtr<IMFSample> sample;
        if (FAILED(MFCreateSample(&sample)) || FAILED(sample->AddBuffer(surface.Get()))) return false;
        const auto time = static_cast<LONGLONG>(frameIndex_++) * 10000000ll /
            options_.frames_per_second;
        if (FAILED(sample->SetSampleTime(time)) ||
            FAILED(sample->SetSampleDuration(10000000ll / options_.frames_per_second))) return false;
        const bool accepted = SUCCEEDED(encoder_->ProcessInput(0, sample.Get(), 0));
        if (accepted) { std::lock_guard lock(mutex_); ++submittedFrames_; }
        return accepted;
    }

    bool collect() {
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
        if (output == MF_E_TRANSFORM_NEED_MORE_INPUT) return true;
        if (FAILED(output)) return false;
        ComPtr<IMFSample> sample;
        if (supplied) sample = supplied;
        else sample.Attach(result.pSample);
        if (result.pEvents) result.pEvents->Release();
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
        for (size_t index = 0; index + 4 < frame.bytes.size(); ++index) {
            if (frame.bytes[index] == 0 && frame.bytes[index + 1] == 0 &&
                frame.bytes[index + 2] == 1 && (frame.bytes[index + 3] & 31) == 5) {
                frame.info.keyframe = 1;
                break;
            }
        }
        {
            std::lock_guard lock(mutex_);
            ++encodedFrames_;
            if (output_.size() == 3) { output_.pop_front(); ++droppedEncodedQueue_; }
            output_.push_back(std::move(frame));
        }
        return true;
    }

    void run() {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            if (!initialize()) fail();
            else {
                {
                    std::lock_guard lock(mutex_);
                    initialized_ = true;
                }
                condition_.notify_all();
                while (running_) {
                    MediaEventType event{};
                    CapturedFrame frame;
                    bool submitFrame = false, handleEvent = false;
                    {
                        std::unique_lock lock(mutex_);
                        condition_.wait(lock, [this] {
                            return !running_ || !mediaEvents_.empty() ||
                                (inputReady_ && pending_.texture);
                        });
                        if (!running_) break;
                        if (!mediaEvents_.empty()) {
                            event = mediaEvents_.front();
                            mediaEvents_.pop_front();
                            handleEvent = true;
                            if (event == METransformNeedInput) inputReady_ = true;
                        } else if (inputReady_ && pending_.texture) {
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
        if (pool_) pool_.FrameArrived(frameToken_);
        if (session_) session_.Close();
        if (pool_) pool_.Close();
        {
            std::lock_guard lock(mutex_);
            events_.Reset();
        }
        if (encoder_) encoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        eventSink_.Reset();
        encoder_.Reset();
        if (mediaFoundationStarted_) MFShutdown();
        condition_.notify_all();
    }

    pc_screen_encoder_options options_{};
    std::thread worker_;
    std::atomic<bool> running_{true};
    std::mutex mutex_, gpuMutex_;
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
        !options->source_handle || options->frames_per_second < 1 ||
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

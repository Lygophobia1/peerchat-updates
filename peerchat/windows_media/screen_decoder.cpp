#include "peerchat_screen_media.h"

#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wrl/client.h>
#include <winrt/base.h>

#include <algorithm>
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

namespace {
struct QueuedFrame {
    std::vector<uint8_t> bytes;
    uint64_t timestamp = 0;
};

class DecoderState {
public:
    explicit DecoderState(pc_screen_decoder_options options) : options_(options) {}
    ~DecoderState() { stop(); }

    bool start() {
        worker_ = std::thread([this] { run(); });
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

    int push(const uint8_t* data, uint32_t length, uint64_t timestamp) {
        if (!data || !length || length > 8 * 1024 * 1024 || failed_) return -1;
        std::lock_guard lock(mutex_);
        if (!running_ || failed_) return -1;
        if (queue_.size() >= 8) { ++droppedDecodeQueue_; return 0; }
        queue_.push_back({std::vector<uint8_t>(data, data + length), timestamp});
        ++accepted_;
        condition_.notify_one();
        return 1;
    }

    int status(pc_screen_decoder_status* value) {
        if (!value || value->structure_size != sizeof(*value)) return 0;
        std::lock_guard lock(mutex_);
        *value = {sizeof(*value), accepted_, decoded_, presented_, failed_ ? 1u : 0u,
            decodedWidth_, decodedHeight_, droppedDecodeQueue_,
            decodeLatencyMicrosecondsTotal_, decodeOperations_};
        return 1;
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
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                nullptr, 0, D3D11_SDK_VERSION, &d3d_, nullptr, &context_))) return false;
        if (FAILED(d3d_.As(&videoDevice_)) || FAILED(context_.As(&videoContext_))) return false;
        UINT token = 0;
        if (FAILED(MFCreateDXGIDeviceManager(&token, &deviceManager_)) ||
            FAILED(deviceManager_->ResetDevice(d3d_.Get(), token))) return false;
        MFT_REGISTER_TYPE_INFO input{MFMediaType_Video, MFVideoFormat_H264};
        IMFActivate** activations = nullptr;
        UINT32 count = 0;
        if (FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
                MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER,
                &input, nullptr, &activations, &count)) || !count) return false;
        const HRESULT activation = activations[0]->ActivateObject(IID_PPV_ARGS(&decoder_));
        for (UINT32 index = 0; index < count; ++index) activations[index]->Release();
        CoTaskMemFree(activations);
        if (FAILED(activation)) return false;
        ComPtr<IMFAttributes> attributes;
        UINT32 aware = 0;
        if (FAILED(decoder_->GetAttributes(&attributes)) ||
            FAILED(attributes->GetUINT32(MF_SA_D3D11_AWARE, &aware)) || !aware ||
            FAILED(decoder_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,
                reinterpret_cast<ULONG_PTR>(deviceManager_.Get())))) return false;
        ComPtr<IMFMediaType> inputType;
        if (FAILED(MFCreateMediaType(&inputType)) ||
            FAILED(inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
            FAILED(inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264)) ||
            FAILED(MFSetAttributeSize(inputType.Get(), MF_MT_FRAME_SIZE,
                options_.width, options_.height)) ||
            FAILED(MFSetAttributeRatio(inputType.Get(), MF_MT_FRAME_RATE,
                options_.frames_per_second, 1)) ||
            FAILED(inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive)) ||
            FAILED(decoder_->SetInputType(0, inputType.Get(), 0))) return false;
        ComPtr<IMFMediaType> outputType;
        if (FAILED(MFCreateMediaType(&outputType)) ||
            FAILED(outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
            FAILED(outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12)) ||
            FAILED(MFSetAttributeSize(outputType.Get(), MF_MT_FRAME_SIZE,
                options_.width, options_.height)) ||
            FAILED(MFSetAttributeRatio(outputType.Get(), MF_MT_FRAME_RATE,
                options_.frames_per_second, 1)) ||
            FAILED(outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive)) ||
            FAILED(decoder_->SetOutputType(0, outputType.Get(), 0))) return false;
        MFT_OUTPUT_STREAM_INFO stream{};
        if (FAILED(decoder_->GetOutputStreamInfo(0, &stream)) ||
            !(stream.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) return false;
        if (FAILED(decoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0)) ||
            FAILED(decoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0))) return false;

        ComPtr<IDXGIDevice> dxgi;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory2> factory;
        if (FAILED(d3d_.As(&dxgi)) || FAILED(dxgi->GetAdapter(&adapter)) ||
            FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return false;
        RECT bounds{};
        auto window = reinterpret_cast<HWND>(options_.presentation_window);
        if (!GetClientRect(window, &bounds)) return false;
        presentationWidth_ = static_cast<UINT>(bounds.right - bounds.left);
        presentationHeight_ = static_cast<UINT>(bounds.bottom - bounds.top);
        if (!presentationWidth_ || !presentationHeight_) return false;
        DXGI_SWAP_CHAIN_DESC1 swapDesc{};
        swapDesc.Width = presentationWidth_;
        swapDesc.Height = presentationHeight_;
        swapDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.BufferCount = 2;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        return SUCCEEDED(factory->CreateSwapChainForHwnd(
            d3d_.Get(), window, &swapDesc, nullptr, nullptr, &swap_));
    }

    bool updatePresentationSize() {
        RECT bounds{};
        if (!GetClientRect(reinterpret_cast<HWND>(options_.presentation_window), &bounds)) return false;
        const auto width = static_cast<UINT>(bounds.right - bounds.left);
        const auto height = static_cast<UINT>(bounds.bottom - bounds.top);
        if (!width || !height) return true;
        if (width == presentationWidth_ && height == presentationHeight_) return true;
        if (FAILED(swap_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))) return false;
        presentationWidth_ = width;
        presentationHeight_ = height;
        return true;
    }

    bool present(IMFSample* sample) {
        ComPtr<IMFMediaBuffer> buffer;
        ComPtr<IMFDXGIBuffer> surface;
        ComPtr<ID3D11Texture2D> texture;
        UINT slice = 0;
        if (FAILED(sample->GetBufferByIndex(0, &buffer)) ||
            FAILED(buffer.As(&surface)) ||
            FAILED(surface->GetResource(IID_PPV_ARGS(&texture))) ||
            FAILED(surface->GetSubresourceIndex(&slice))) return false;
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        if (desc.Format != DXGI_FORMAT_NV12 || !updatePresentationSize()) return false;
        ComPtr<ID3D11Texture2D> backBuffer;
        if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return false;
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
        content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        content.InputFrameRate = {options_.frames_per_second, 1};
        content.InputWidth = desc.Width;
        content.InputHeight = desc.Height;
        content.OutputFrameRate = content.InputFrameRate;
        content.OutputWidth = presentationWidth_;
        content.OutputHeight = presentationHeight_;
        content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
        ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
        ComPtr<ID3D11VideoProcessor> processor;
        if (FAILED(videoDevice_->CreateVideoProcessorEnumerator(&content, &enumerator)) ||
            FAILED(videoDevice_->CreateVideoProcessor(enumerator.Get(), 0, &processor))) return false;
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDesc{};
        inputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        inputDesc.Texture2D.ArraySlice = slice;
        ComPtr<ID3D11VideoProcessorInputView> input;
        if (FAILED(videoDevice_->CreateVideoProcessorInputView(
            texture.Get(), enumerator.Get(), &inputDesc, &input))) return false;
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDesc{};
        outputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11VideoProcessorOutputView> output;
        if (FAILED(videoDevice_->CreateVideoProcessorOutputView(
            backBuffer.Get(), enumerator.Get(), &outputDesc, &output))) return false;
        const double factor = std::min(double(presentationWidth_) / desc.Width,
            double(presentationHeight_) / desc.Height);
        const auto fittedWidth = static_cast<LONG>(desc.Width * factor);
        const auto fittedHeight = static_cast<LONG>(desc.Height * factor);
        RECT destination{
            (static_cast<LONG>(presentationWidth_) - fittedWidth) / 2,
            (static_cast<LONG>(presentationHeight_) - fittedHeight) / 2,
            (static_cast<LONG>(presentationWidth_) + fittedWidth) / 2,
            (static_cast<LONG>(presentationHeight_) + fittedHeight) / 2};
        videoContext_->VideoProcessorSetStreamDestRect(processor.Get(), 0, TRUE, &destination);
        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = input.Get();
        const bool displayed=SUCCEEDED(videoContext_->VideoProcessorBlt(
                   processor.Get(), output.Get(), 0, 1, &stream)) &&
               SUCCEEDED(swap_->Present(1, 0));
        if(displayed){std::lock_guard lock(mutex_);decodedWidth_=desc.Width;decodedHeight_=desc.Height;}
        return displayed;
    }

    bool drain() {
        for (;;) {
            MFT_OUTPUT_DATA_BUFFER result{};
            DWORD status = 0;
            const HRESULT produced = decoder_->ProcessOutput(0, 1, &result, &status);
            if (produced == MF_E_TRANSFORM_NEED_MORE_INPUT) return true;
            if (produced == MF_E_TRANSFORM_STREAM_CHANGE) {
                bool selected = false;
                for (DWORD index = 0;; ++index) {
                    ComPtr<IMFMediaType> available;
                    if (FAILED(decoder_->GetOutputAvailableType(0, index, &available))) break;
                    GUID subtype{};
                    if (SUCCEEDED(available->GetGUID(MF_MT_SUBTYPE, &subtype)) &&
                        subtype == MFVideoFormat_NV12 &&
                        SUCCEEDED(decoder_->SetOutputType(0, available.Get(), 0))) {
                        selected = true;
                        break;
                    }
                }
                if (!selected) return false;
                continue;
            }
            if (FAILED(produced)) return false;
            ComPtr<IMFSample> sample;
            sample.Attach(result.pSample);
            if (result.pEvents) result.pEvents->Release();
            if (!sample) return false;
            {
                std::lock_guard lock(mutex_);
                ++decoded_;
            }
            if (!present(sample.Get())) return false;
            {
                std::lock_guard lock(mutex_);
                ++presented_;
            }
        }
    }

    bool decode(QueuedFrame frame) {
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(frame.bytes.size()), &buffer)))
            return false;
        BYTE* pointer = nullptr;
        if (FAILED(buffer->Lock(&pointer, nullptr, nullptr))) return false;
        std::memcpy(pointer, frame.bytes.data(), frame.bytes.size());
        buffer->Unlock();
        if (FAILED(buffer->SetCurrentLength(static_cast<DWORD>(frame.bytes.size())))) return false;
        ComPtr<IMFSample> sample;
        if (FAILED(MFCreateSample(&sample)) || FAILED(sample->AddBuffer(buffer.Get())) ||
            FAILED(sample->SetSampleTime(static_cast<LONGLONG>(frame.timestamp)))) return false;
        return SUCCEEDED(decoder_->ProcessInput(0, sample.Get(), 0)) && drain();
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
                    QueuedFrame frame;
                    {
                        std::unique_lock lock(mutex_);
                        condition_.wait(lock, [this] { return !running_ || !queue_.empty(); });
                        if (!running_) break;
                        frame = std::move(queue_.front());
                        queue_.pop_front();
                    }
                    const auto started = std::chrono::steady_clock::now();
                    const bool decoded = decode(std::move(frame));
                    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - started).count();
                    {
                        std::lock_guard lock(mutex_);
                        ++decodeOperations_;
                        decodeLatencyMicrosecondsTotal_ += static_cast<uint64_t>(std::max<int64_t>(0, elapsed));
                    }
                    if (!decoded) fail();
                }
            }
        } catch (...) { fail(); }
        running_ = false;
        if (decoder_) decoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        decoder_.Reset();
        swap_.Reset();
        if (mediaFoundationStarted_) MFShutdown();
        condition_.notify_all();
    }

    pc_screen_decoder_options options_{};
    std::thread worker_;
    std::atomic<bool> running_{true};
    std::mutex mutex_;
    std::condition_variable condition_;
    bool initialized_ = false, failed_ = false, mediaFoundationStarted_ = false;
    std::deque<QueuedFrame> queue_;
    uint64_t accepted_ = 0, decoded_ = 0, presented_ = 0;
    uint64_t droppedDecodeQueue_ = 0, decodeLatencyMicrosecondsTotal_ = 0, decodeOperations_ = 0;
    uint32_t decodedWidth_ = 0, decodedHeight_ = 0;
    UINT presentationWidth_ = 0, presentationHeight_ = 0;
    ComPtr<ID3D11Device> d3d_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11VideoDevice> videoDevice_;
    ComPtr<ID3D11VideoContext> videoContext_;
    ComPtr<IMFDXGIDeviceManager> deviceManager_;
    ComPtr<IMFTransform> decoder_;
    ComPtr<IDXGISwapChain1> swap_;
};
}

struct pc_screen_decoder { std::unique_ptr<DecoderState> state; };

extern "C" pc_screen_decoder* __cdecl pc_screen_decoder_open(
    const pc_screen_decoder_options* options) {
    if (!options || options->structure_size != sizeof(*options) ||
        !IsWindow(reinterpret_cast<HWND>(options->presentation_window)) ||
        options->width < 48 || options->height < 48 ||
        options->width > 16384 || options->height > 16384 ||
        static_cast<uint64_t>(options->width) * options->height > 100000000 ||
        options->frames_per_second < 1 || options->frames_per_second > 240) return nullptr;
    try {
        auto state = std::make_unique<DecoderState>(*options);
        if (!state->start()) return nullptr;
        return new pc_screen_decoder{std::move(state)};
    } catch (...) { return nullptr; }
}

extern "C" int __cdecl pc_screen_decoder_push(pc_screen_decoder* decoder,
    const uint8_t* encoded, uint32_t length, uint64_t timestamp_100ns) {
    try { return decoder && decoder->state ?
        decoder->state->push(encoded, length, timestamp_100ns) : -1; }
    catch (...) { return -1; }
}

extern "C" int __cdecl pc_screen_decoder_status_get(pc_screen_decoder* decoder,
    pc_screen_decoder_status* status) {
    try { return decoder && decoder->state ? decoder->state->status(status) : 0; }
    catch (...) { return 0; }
}

extern "C" void __cdecl pc_screen_decoder_close(pc_screen_decoder* decoder) {
    if (!decoder) return;
    try { decoder->state->stop(); } catch (...) {}
    delete decoder;
}

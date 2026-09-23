// Manual Windows GPU-path probe: no captured or encoded content is persisted.
// Run only in an interactive desktop session with a working hardware encoder.
#include <windows.h>
#include "../windows_media/peerchat_screen_media.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <wrl/client.h>
#include <Windows.Graphics.Capture.Interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <vector>

using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

struct MonitorChoice { HMONITOR handle = nullptr; };
static BOOL CALLBACK firstMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM context) {
    auto* choice = reinterpret_cast<MonitorChoice*>(context);
    if (!choice->handle) choice->handle = monitor;
    return FALSE;
}

int wmain() {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        struct SourceCounts { unsigned monitors = 0, windows = 0; } sources;
        const auto sourceCallback = [](void* opaque, uint32_t kind, uint64_t,
            const wchar_t*, uint32_t, uint32_t, uint32_t, uint32_t) {
            auto& counts = *static_cast<SourceCounts*>(opaque);
            if (kind == PC_SCREEN_MONITOR) ++counts.monitors;
            if (kind == PC_SCREEN_WINDOW) ++counts.windows;
        };
        const auto totalSources = pc_screen_enumerate_sources(sourceCallback, &sources);
        pc_screen_capabilities capabilities{};
        capabilities.structure_size = sizeof(capabilities);
        if (!pc_screen_query_capabilities(&capabilities)) return 10;
        std::wcout << L"adapter_sources=" << totalSources << L" monitors=" << sources.monitors
                   << L" windows=" << sources.windows << L" hardware_codecs="
                   << capabilities.hardware_encoder_codecs << L" decoder_codecs="
                   << capabilities.decoder_codecs << L'\n';
        if (!sources.monitors || totalSources != sources.monitors + sources.windows ||
            !(capabilities.hardware_encoder_codecs & PC_SCREEN_H264) ||
            !(capabilities.decoder_codecs & PC_SCREEN_H264)) return 11;
        if (!GraphicsCaptureSession::IsSupported()) {
            std::wcerr << L"Windows Graphics Capture is unsupported.\n";
            return 2;
        }
        MonitorChoice monitor;
        EnumDisplayMonitors(nullptr, nullptr, firstMonitor, reinterpret_cast<LPARAM>(&monitor));
        if (!monitor.handle) return 3;

        Microsoft::WRL::ComPtr<ID3D11Device> d3d;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL level{};
        winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION, &d3d, &level, &context));
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
        winrt::check_hresult(d3d.As(&dxgi));
        winrt::com_ptr<IInspectable> direct3dInspectable;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), direct3dInspectable.put()));
        auto direct3dDevice = direct3dInspectable.as<IDirect3DDevice>();

        auto factory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        GraphicsCaptureItem item{nullptr};
        winrt::check_hresult(factory->CreateForMonitor(monitor.handle,
            winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item)));
        auto size = item.Size();
        std::wcout << L"selected_monitor=" << size.Width << L"x" << size.Height << L'\n';

        std::mutex mutex;
        std::condition_variable ready;
        bool received = false;
        HRESULT frameResult = E_FAIL;
        D3D11_TEXTURE2D_DESC desc{};
        Microsoft::WRL::ComPtr<ID3D11Texture2D> captured;
        auto pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
            direct3dDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
        auto callback = pool.FrameArrived([&](Direct3D11CaptureFramePool const& sender,
                                               winrt::Windows::Foundation::IInspectable const&) {
            if (received) return;
            try {
                auto frame = sender.TryGetNextFrame();
                auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
                Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
                frameResult = access->GetInterface(IID_PPV_ARGS(&texture));
                if (SUCCEEDED(frameResult)) {
                    texture->GetDesc(&desc);
                    auto copyDesc = desc;
                    copyDesc.BindFlags = 0;
                    copyDesc.CPUAccessFlags = 0;
                    copyDesc.MiscFlags = 0;
                    frameResult = d3d->CreateTexture2D(&copyDesc, nullptr, &captured);
                    if (SUCCEEDED(frameResult)) context->CopyResource(captured.Get(), texture.Get());
                }
            } catch (winrt::hresult_error const& error) { frameResult = error.code(); }
            {
                std::lock_guard<std::mutex> lock(mutex);
                received = true;
            }
            ready.notify_one();
        });
        auto session = pool.CreateCaptureSession(item);
        session.StartCapture();
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (!ready.wait_for(lock, std::chrono::seconds(5), [&] { return received; })) {
                std::wcerr << L"No capture frame arrived within five seconds.\n";
                return 4;
            }
        }
        pool.FrameArrived(callback);
        session.Close();
        pool.Close();
        std::wcout << L"frame_hr=0x" << std::hex << static_cast<unsigned>(frameResult)
                   << std::dec << L" texture=" << desc.Width << L"x" << desc.Height
                   << L" format=" << desc.Format << L" usage=" << desc.Usage
                   << L" cpu_access=" << desc.CPUAccessFlags << L'\n';
        if (FAILED(frameResult)) return 5;
        auto checked = [](const wchar_t* step, HRESULT hr) {
            std::wcout << L"stage=" << step << L" hr=0x" << std::hex
                       << static_cast<unsigned>(hr) << std::dec << L'\n';
            winrt::check_hresult(hr);
        };

        Microsoft::WRL::ComPtr<ID3D11VideoDevice> videoDevice;
        Microsoft::WRL::ComPtr<ID3D11VideoContext> videoContext;
        checked(L"video_device", d3d.As(&videoDevice));
        checked(L"video_context", context.As(&videoContext));
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
        content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        content.InputFrameRate = {60, 1};
        content.InputWidth = desc.Width;
        content.InputHeight = desc.Height;
        content.OutputFrameRate = {60, 1};
        content.OutputWidth = desc.Width;
        content.OutputHeight = desc.Height;
        content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
        Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
        checked(L"video_enumerator", videoDevice->CreateVideoProcessorEnumerator(&content, &enumerator));
        Microsoft::WRL::ComPtr<ID3D11VideoProcessor> processor;
        checked(L"video_processor", videoDevice->CreateVideoProcessor(enumerator.Get(), 0, &processor));
        D3D11_TEXTURE2D_DESC nv12Desc{};
        nv12Desc.Width = desc.Width;
        nv12Desc.Height = desc.Height;
        nv12Desc.MipLevels = 1;
        nv12Desc.ArraySize = 1;
        nv12Desc.Format = DXGI_FORMAT_NV12;
        nv12Desc.SampleDesc.Count = 1;
        nv12Desc.Usage = D3D11_USAGE_DEFAULT;
        nv12Desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> nv12;
        checked(L"nv12_texture", d3d->CreateTexture2D(&nv12Desc, nullptr, &nv12));
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc{};
        inputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> inputView;
        checked(L"input_view", videoDevice->CreateVideoProcessorInputView(
            captured.Get(), enumerator.Get(), &inputViewDesc, &inputView));
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDesc{};
        outputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> outputView;
        checked(L"output_view", videoDevice->CreateVideoProcessorOutputView(
            nv12.Get(), enumerator.Get(), &outputViewDesc, &outputView));
        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = inputView.Get();
        checked(L"video_blt", videoContext->VideoProcessorBlt(
            processor.Get(), outputView.Get(), 0, 1, &stream));
        std::wcout << L"gpu_bgra_to_nv12=ok nv12_cpu_access=" << nv12Desc.CPUAccessFlags
                   << L" cpu_pixel_reads=0\n";

        checked(L"mf_startup", MFStartup(MF_VERSION));
        MFT_REGISTER_TYPE_INFO h264Output{MFMediaType_Video, MFVideoFormat_H264};
        IMFActivate** activations = nullptr;
        UINT32 encoderCount = 0;
        checked(L"h264_hardware_enum", MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
            MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
            nullptr, &h264Output, &activations, &encoderCount));
        std::wcout << L"hardware_h264_encoder_count=" << encoderCount << L'\n';
        if (!encoderCount) return 6;
        Microsoft::WRL::ComPtr<IMFTransform> encoder;
        checked(L"h264_activate", activations[0]->ActivateObject(IID_PPV_ARGS(&encoder)));
        for (UINT32 i = 0; i < encoderCount; ++i) activations[i]->Release();
        CoTaskMemFree(activations);
        Microsoft::WRL::ComPtr<IMFAttributes> encoderAttributes;
        checked(L"encoder_attributes", encoder->GetAttributes(&encoderAttributes));
        checked(L"encoder_async_unlock", encoderAttributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE));

        Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> manager;
        UINT token = 0;
        checked(L"dxgi_manager", MFCreateDXGIDeviceManager(&token, &manager));
        checked(L"dxgi_manager_reset", manager->ResetDevice(d3d.Get(), token));
        checked(L"encoder_d3d_manager", encoder->ProcessMessage(
            MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(manager.Get())));

        Microsoft::WRL::ComPtr<IMFMediaType> outputType;
        checked(L"encoder_output_type", MFCreateMediaType(&outputType));
        checked(L"encoder_output_major", outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        checked(L"encoder_output_codec", outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
        checked(L"encoder_output_size", MFSetAttributeSize(outputType.Get(), MF_MT_FRAME_SIZE,
                                                              desc.Width, desc.Height));
        checked(L"encoder_output_fps", MFSetAttributeRatio(outputType.Get(), MF_MT_FRAME_RATE, 60, 1));
        checked(L"encoder_output_aspect", MFSetAttributeRatio(outputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
        checked(L"encoder_output_bitrate", outputType->SetUINT32(MF_MT_AVG_BITRATE, 20000000));
        checked(L"encoder_output_interlace", outputType->SetUINT32(
            MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
        checked(L"encoder_set_output", encoder->SetOutputType(0, outputType.Get(), 0));

        Microsoft::WRL::ComPtr<IMFMediaType> inputType;
        checked(L"encoder_input_type", MFCreateMediaType(&inputType));
        checked(L"encoder_input_major", inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        checked(L"encoder_input_format", inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12));
        checked(L"encoder_input_size", MFSetAttributeSize(inputType.Get(), MF_MT_FRAME_SIZE,
                                                            desc.Width, desc.Height));
        checked(L"encoder_input_fps", MFSetAttributeRatio(inputType.Get(), MF_MT_FRAME_RATE, 60, 1));
        checked(L"encoder_input_aspect", MFSetAttributeRatio(inputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
        checked(L"encoder_input_interlace", inputType->SetUINT32(
            MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
        checked(L"encoder_set_input", encoder->SetInputType(0, inputType.Get(), 0));

        Microsoft::WRL::ComPtr<IMFMediaBuffer> surfaceBuffer;
        checked(L"encoder_surface_buffer", MFCreateDXGISurfaceBuffer(
            __uuidof(ID3D11Texture2D), nv12.Get(), 0, FALSE, &surfaceBuffer));
        Microsoft::WRL::ComPtr<IMFSample> sample;
        checked(L"encoder_sample", MFCreateSample(&sample));
        checked(L"encoder_add_buffer", sample->AddBuffer(surfaceBuffer.Get()));
        checked(L"encoder_sample_time", sample->SetSampleTime(0));
        checked(L"encoder_sample_duration", sample->SetSampleDuration(166666));
        checked(L"encoder_begin", encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
        checked(L"encoder_start", encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));
        checked(L"encoder_process_input", encoder->ProcessInput(0, sample.Get(), 0));
        std::wcout << L"h264_gpu_input_accepted=true\n";
        Microsoft::WRL::ComPtr<IMFMediaEventGenerator> events;
        checked(L"encoder_events", encoder.As(&events));
        std::vector<std::vector<unsigned char>> encodedFrames;
        unsigned inputs = 1;
        unsigned outputEvents = 0;
        auto deadline = GetTickCount64() + 10000;
        while (encodedFrames.size() < 16 && GetTickCount64() < deadline) {
            Microsoft::WRL::ComPtr<IMFMediaEvent> event;
            HRESULT next = events->GetEvent(MF_EVENT_FLAG_NO_WAIT, &event);
            if (next == MF_E_NO_EVENTS_AVAILABLE) { Sleep(5); continue; }
            checked(L"encoder_next_event", next);
            MediaEventType kind{};
            checked(L"encoder_event_type", event->GetType(&kind));
            std::wcout << L"encoder_event_kind=" << static_cast<unsigned>(kind) << L'\n';
            if (kind == METransformNeedInput && inputs < 32) {
                Microsoft::WRL::ComPtr<IMFMediaBuffer> nextSurface;
                checked(L"encoder_next_surface", MFCreateDXGISurfaceBuffer(
                    __uuidof(ID3D11Texture2D), nv12.Get(), 0, FALSE, &nextSurface));
                Microsoft::WRL::ComPtr<IMFSample> nextSample;
                checked(L"encoder_next_sample", MFCreateSample(&nextSample));
                checked(L"encoder_next_add", nextSample->AddBuffer(nextSurface.Get()));
                checked(L"encoder_next_time", nextSample->SetSampleTime(
                    static_cast<LONGLONG>(inputs) * 166666));
                checked(L"encoder_next_duration", nextSample->SetSampleDuration(166666));
                HRESULT accepted = encoder->ProcessInput(0, nextSample.Get(), 0);
                std::wcout << L"stage=encoder_more_input hr=0x" << std::hex
                           << static_cast<unsigned>(accepted) << std::dec << L'\n';
                if (SUCCEEDED(accepted)) ++inputs;
                else if (accepted != MF_E_NOTACCEPTING) winrt::check_hresult(accepted);
            } else if (kind == METransformHaveOutput) {
                ++outputEvents;
                MFT_OUTPUT_STREAM_INFO streamInfo{};
                checked(L"encoder_output_info", encoder->GetOutputStreamInfo(0, &streamInfo));
                MFT_OUTPUT_DATA_BUFFER result{};
                result.dwStreamID = 0;
                Microsoft::WRL::ComPtr<IMFSample> supplied;
                Microsoft::WRL::ComPtr<IMFMediaBuffer> suppliedBuffer;
                if (!(streamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
                    checked(L"encoder_output_sample", MFCreateSample(&supplied));
                    checked(L"encoder_output_buffer", MFCreateMemoryBuffer(
                        streamInfo.cbSize ? streamInfo.cbSize : 1048576, &suppliedBuffer));
                    checked(L"encoder_output_add", supplied->AddBuffer(suppliedBuffer.Get()));
                    result.pSample = supplied.Get();
                }
                DWORD status = 0;
                checked(L"encoder_process_output", encoder->ProcessOutput(0, 1, &result, &status));
                Microsoft::WRL::ComPtr<IMFSample> outputSample;
                if (supplied) outputSample = supplied;
                else outputSample.Attach(result.pSample);
                if (result.pEvents) result.pEvents->Release();
                Microsoft::WRL::ComPtr<IMFMediaBuffer> bytes;
                checked(L"encoder_contiguous", outputSample->ConvertToContiguousBuffer(&bytes));
                BYTE* pointer = nullptr;
                DWORD length = 0;
                checked(L"encoder_lock", bytes->Lock(&pointer, nullptr, &length));
                encodedFrames.emplace_back(pointer, pointer + length);
                bytes->Unlock();
            }
        }
        if (encodedFrames.empty()) return 7;
        auto const& encoded = encodedFrames.front();
        std::wcout << L"encoded_h264_bytes=" << encoded.size() << L" input_frames="
                   << inputs << L" output_events=" << outputEvents
                   << L" encoded_frames=" << encodedFrames.size() << L'\n';
        unsigned startCodes = 0, sps = 0, pps = 0, idr = 0;
        for (size_t i = 0; i + 4 < encoded.size(); ++i) {
            if (encoded[i] == 0 && encoded[i + 1] == 0 && encoded[i + 2] == 1) {
                ++startCodes;
                unsigned type = encoded[i + 3] & 0x1f;
                sps += type == 7;
                pps += type == 8;
                idr += type == 5;
                i += 2;
            }
        }
        std::wcout << L"h264_annexb_start_codes=" << startCodes << L" sps=" << sps
                   << L" pps=" << pps << L" idr=" << idr << L'\n';
        for (size_t frame = 0; frame < encodedFrames.size(); ++frame) {
            std::wcout << L"encoded_frame_index=" << frame << L" bytes="
                       << encodedFrames[frame].size() << L'\n';
        }

        MFT_REGISTER_TYPE_INFO decoderInput{MFMediaType_Video, MFVideoFormat_H264};
        IMFActivate** decoderActivations = nullptr;
        UINT32 decoderCount = 0;
        checked(L"h264_decoder_enum", MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
            MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER,
            &decoderInput, nullptr, &decoderActivations, &decoderCount));
        std::wcout << L"h264_decoder_count=" << decoderCount << L'\n';
        if (!decoderCount) return 8;
        Microsoft::WRL::ComPtr<IMFTransform> decoder;
        checked(L"h264_decoder_activate", decoderActivations[0]->ActivateObject(IID_PPV_ARGS(&decoder)));
        for (UINT32 i = 0; i < decoderCount; ++i) decoderActivations[i]->Release();
        CoTaskMemFree(decoderActivations);
        Microsoft::WRL::ComPtr<IMFAttributes> decoderAttributes;
        checked(L"decoder_attributes", decoder->GetAttributes(&decoderAttributes));
        UINT32 decoderD3dAware = 0;
        HRESULT aware = decoderAttributes->GetUINT32(MF_SA_D3D11_AWARE, &decoderD3dAware);
        std::wcout << L"decoder_d3d11_aware_query=0x" << std::hex
                   << static_cast<unsigned>(aware) << std::dec
                   << L" decoder_d3d11_aware=" << decoderD3dAware << L'\n';
        if (decoderD3dAware) checked(L"decoder_d3d_manager", decoder->ProcessMessage(
            MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(manager.Get())));

        Microsoft::WRL::ComPtr<IMFMediaType> decoderInputType;
        checked(L"decoder_input_type", MFCreateMediaType(&decoderInputType));
        checked(L"decoder_input_major", decoderInputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        checked(L"decoder_input_format", decoderInputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
        checked(L"decoder_input_size", MFSetAttributeSize(decoderInputType.Get(), MF_MT_FRAME_SIZE,
                                                            desc.Width, desc.Height));
        checked(L"decoder_input_fps", MFSetAttributeRatio(decoderInputType.Get(), MF_MT_FRAME_RATE, 60, 1));
        checked(L"decoder_input_interlace", decoderInputType->SetUINT32(
            MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
        Microsoft::WRL::ComPtr<IMFMediaType> encoderCurrentType;
        checked(L"encoder_current_output_type", encoder->GetOutputCurrentType(0, &encoderCurrentType));
        UINT32 sequenceSize = 0;
        HRESULT sequenceResult = encoderCurrentType->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &sequenceSize);
        std::wcout << L"encoder_sequence_header_result=0x" << std::hex
                   << static_cast<unsigned>(sequenceResult) << std::dec
                   << L" bytes=" << sequenceSize << L'\n';
        if (SUCCEEDED(sequenceResult) && sequenceSize) {
            std::vector<BYTE> sequence(sequenceSize);
            UINT32 actual = 0;
            checked(L"encoder_sequence_header_read", encoderCurrentType->GetBlob(
                MF_MT_MPEG_SEQUENCE_HEADER, sequence.data(), sequenceSize, &actual));
            checked(L"decoder_sequence_header_set", decoderInputType->SetBlob(
                MF_MT_MPEG_SEQUENCE_HEADER, sequence.data(), actual));
        }
        checked(L"decoder_set_input", decoder->SetInputType(0, decoderInputType.Get(), 0));
        Microsoft::WRL::ComPtr<IMFMediaType> decoderOutputType;
        checked(L"decoder_output_type", MFCreateMediaType(&decoderOutputType));
        checked(L"decoder_output_major", decoderOutputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        checked(L"decoder_output_format", decoderOutputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12));
        checked(L"decoder_output_size", MFSetAttributeSize(decoderOutputType.Get(), MF_MT_FRAME_SIZE,
                                                             desc.Width, desc.Height));
        checked(L"decoder_output_fps", MFSetAttributeRatio(decoderOutputType.Get(), MF_MT_FRAME_RATE, 60, 1));
        checked(L"decoder_output_interlace", decoderOutputType->SetUINT32(
            MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
        checked(L"decoder_set_output", decoder->SetOutputType(0, decoderOutputType.Get(), 0));
        checked(L"decoder_begin", decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
        checked(L"decoder_start", decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));
        MFT_OUTPUT_STREAM_INFO decoderStream{};
        checked(L"decoder_output_info", decoder->GetOutputStreamInfo(0, &decoderStream));
        std::wcout << L"decoder_output_flags=0x" << std::hex << decoderStream.dwFlags
                   << std::dec << L" decoder_output_cbsize=" << decoderStream.cbSize << L'\n';
        Microsoft::WRL::ComPtr<IMFSample> decoderSupplied;
        Microsoft::WRL::ComPtr<IMFMediaBuffer> decoderSuppliedBuffer;
        MFT_OUTPUT_DATA_BUFFER decodedResult{};
        decodedResult.dwStreamID = 0;
        if (!(decoderStream.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
            checked(L"decoder_output_sample", MFCreateSample(&decoderSupplied));
            checked(L"decoder_output_buffer", MFCreateMemoryBuffer(
                decoderStream.cbSize ? decoderStream.cbSize : desc.Width * desc.Height * 2,
                &decoderSuppliedBuffer));
            checked(L"decoder_output_add", decoderSupplied->AddBuffer(decoderSuppliedBuffer.Get()));
            decodedResult.pSample = decoderSupplied.Get();
        }
        DWORD decoderStatus = 0;
        HRESULT decoded = MF_E_TRANSFORM_NEED_MORE_INPUT;
        for (size_t frame = 0; frame < encodedFrames.size() && decoded == MF_E_TRANSFORM_NEED_MORE_INPUT;
             ++frame) {
            auto const& frameBytes = encodedFrames[frame];
            Microsoft::WRL::ComPtr<IMFMediaBuffer> compressedBuffer;
            checked(L"decoder_compressed_buffer", MFCreateMemoryBuffer(
                static_cast<DWORD>(frameBytes.size()), &compressedBuffer));
            BYTE* compressedPointer = nullptr;
            checked(L"decoder_compressed_lock", compressedBuffer->Lock(&compressedPointer, nullptr, nullptr));
            memcpy(compressedPointer, frameBytes.data(), frameBytes.size());
            compressedBuffer->Unlock();
            checked(L"decoder_compressed_length", compressedBuffer->SetCurrentLength(
                static_cast<DWORD>(frameBytes.size())));
            Microsoft::WRL::ComPtr<IMFSample> compressedSample;
            checked(L"decoder_compressed_sample", MFCreateSample(&compressedSample));
            checked(L"decoder_compressed_add", compressedSample->AddBuffer(compressedBuffer.Get()));
            checked(L"decoder_compressed_time", compressedSample->SetSampleTime(
                static_cast<LONGLONG>(frame) * 166666));
            checked(L"decoder_process_input", decoder->ProcessInput(0, compressedSample.Get(), 0));
            decoded = decoder->ProcessOutput(0, 1, &decodedResult, &decoderStatus);
            if (decoded == MF_E_TRANSFORM_STREAM_CHANGE) {
                for (DWORD choice = 0;; ++choice) {
                    Microsoft::WRL::ComPtr<IMFMediaType> available;
                    HRESULT candidateType = decoder->GetOutputAvailableType(0, choice, &available);
                    checked(L"decoder_available_output", candidateType);
                    GUID subtype{};
                    checked(L"decoder_available_subtype", available->GetGUID(MF_MT_SUBTYPE, &subtype));
                    if (subtype == MFVideoFormat_NV12) {
                        checked(L"decoder_renegotiate", decoder->SetOutputType(0, available.Get(), 0));
                        break;
                    }
                }
                decoded = decoder->ProcessOutput(0, 1, &decodedResult, &decoderStatus);
            }
            std::wcout << L"decoder_frame=" << frame << L" process_output=0x" << std::hex
                       << static_cast<unsigned>(decoded) << std::dec << L'\n';
        }
        checked(L"decoder_process_output", decoded);
        Microsoft::WRL::ComPtr<IMFSample> decodedSample;
        if (decoderSupplied) decodedSample = decoderSupplied;
        else decodedSample.Attach(decodedResult.pSample);
        if (decodedResult.pEvents) decodedResult.pEvents->Release();
        Microsoft::WRL::ComPtr<IMFMediaBuffer> decodedBuffer;
        checked(L"decoder_get_buffer", decodedSample->GetBufferByIndex(0, &decodedBuffer));
        Microsoft::WRL::ComPtr<IMFDXGIBuffer> gpuBuffer;
        HRESULT gpuOutput = decodedBuffer.As(&gpuBuffer);
        DWORD decodedBytes = 0;
        checked(L"decoder_get_length", decodedBuffer->GetCurrentLength(&decodedBytes));
        std::wcout << L"decoded_frame=true decoded_bytes=" << decodedBytes
                   << L" d3d11_surface=" << SUCCEEDED(gpuOutput) << L'\n';
        if (gpuBuffer) {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> decodedTexture;
            checked(L"decoder_get_texture", gpuBuffer->GetResource(IID_PPV_ARGS(&decodedTexture)));
            D3D11_TEXTURE2D_DESC decodedDesc{};
            decodedTexture->GetDesc(&decodedDesc);
            std::wcout << L"decoded_texture=" << decodedDesc.Width << L"x" << decodedDesc.Height
                       << L" format=" << decodedDesc.Format << L" cpu_access="
                       << decodedDesc.CPUAccessFlags << L'\n';
            UINT slice = 0;
            checked(L"decoded_subresource", gpuBuffer->GetSubresourceIndex(&slice));
            auto hwnd = CreateWindowExW(0, L"STATIC", L"PeerChat Stage 8 local presentation probe",
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 960, 560,
                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (!hwnd) winrt::throw_last_error();
            Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
            checked(L"presentation_adapter", dxgi->GetAdapter(&adapter));
            Microsoft::WRL::ComPtr<IDXGIFactory2> factory2;
            checked(L"presentation_factory", adapter->GetParent(IID_PPV_ARGS(&factory2)));
            DXGI_SWAP_CHAIN_DESC1 swapDesc{};
            swapDesc.Width = 960;
            swapDesc.Height = 520;
            swapDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            swapDesc.SampleDesc.Count = 1;
            swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swapDesc.BufferCount = 2;
            swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            Microsoft::WRL::ComPtr<IDXGISwapChain1> swap;
            checked(L"presentation_swapchain", factory2->CreateSwapChainForHwnd(
                d3d.Get(), hwnd, &swapDesc, nullptr, nullptr, &swap));
            Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
            checked(L"presentation_backbuffer", swap->GetBuffer(0, IID_PPV_ARGS(&backBuffer)));
            D3D11_VIDEO_PROCESSOR_CONTENT_DESC presentContent = content;
            presentContent.InputWidth = decodedDesc.Width;
            presentContent.InputHeight = decodedDesc.Height;
            presentContent.OutputWidth = swapDesc.Width;
            presentContent.OutputHeight = swapDesc.Height;
            Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> presentEnumerator;
            checked(L"presentation_enumerator", videoDevice->CreateVideoProcessorEnumerator(
                &presentContent, &presentEnumerator));
            Microsoft::WRL::ComPtr<ID3D11VideoProcessor> presentProcessor;
            checked(L"presentation_processor", videoDevice->CreateVideoProcessor(
                presentEnumerator.Get(), 0, &presentProcessor));
            D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC presentInputDesc{};
            presentInputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
            presentInputDesc.Texture2D.ArraySlice = slice;
            Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> presentInput;
            checked(L"presentation_input_view", videoDevice->CreateVideoProcessorInputView(
                decodedTexture.Get(), presentEnumerator.Get(), &presentInputDesc, &presentInput));
            D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC presentOutputDesc{};
            presentOutputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
            Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> presentOutput;
            checked(L"presentation_output_view", videoDevice->CreateVideoProcessorOutputView(
                backBuffer.Get(), presentEnumerator.Get(), &presentOutputDesc, &presentOutput));
            D3D11_VIDEO_PROCESSOR_STREAM presentStream{};
            presentStream.Enable = TRUE;
            presentStream.pInputSurface = presentInput.Get();
            checked(L"presentation_gpu_blt", videoContext->VideoProcessorBlt(
                presentProcessor.Get(), presentOutput.Get(), 0, 1, &presentStream));
            ShowWindow(hwnd, SW_SHOW);
            checked(L"presentation_swapchain_present", swap->Present(1, 0));
            std::wcout << L"presentation_complete=true raw_frame_cpu_copies=0\n";
            Sleep(700);
            DestroyWindow(hwnd);
        }
        encoder.Reset();
        MFShutdown();
        return 0;
    } catch (winrt::hresult_error const& error) {
        std::wcerr << L"capture_hresult=0x" << std::hex << static_cast<unsigned>(error.code())
                   << L" " << error.message().c_str() << L'\n';
        return 1;
    }
}

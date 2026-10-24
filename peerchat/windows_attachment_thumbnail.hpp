#pragma once

#include "transfer_manager.hpp"
#include <windows.h>
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace peerchat::attachment_thumbnails {
namespace detail {
template<class T> struct ComPtr {
    T* value=nullptr;
    ~ComPtr(){if(value)value->Release();}
    T** out(){return &value;}
    T* operator->()const{return value;}
    explicit operator bool()const{return value!=nullptr;}
    ComPtr()=default;
    ComPtr(const ComPtr&)=delete;
    ComPtr& operator=(const ComPtr&)=delete;
};
struct ComRuntime {
    using Initialize=HRESULT (WINAPI*)(LPVOID,DWORD);
    using Uninitialize=void (WINAPI*)();
    using Create=HRESULT (WINAPI*)(REFCLSID,LPUNKNOWN,DWORD,REFIID,LPVOID*);
    HMODULE library=LoadLibraryW(L"ole32.dll");
    Uninitialize uninitialize=nullptr;
    Create create=nullptr;
    bool initialized=false;
    template<class Function> static Function load(HMODULE library,const char* name){
        FARPROC address=GetProcAddress(library,name);Function function=nullptr;
        static_assert(sizeof(function)==sizeof(address),"Windows procedure pointer size");
        std::memcpy(&function,&address,sizeof(function));return function;
    }
    ComRuntime(){
        if(!library)return;
        auto init=load<Initialize>(library,"CoInitializeEx");
        uninitialize=load<Uninitialize>(library,"CoUninitialize");
        create=load<Create>(library,"CoCreateInstance");
        if(!init||!uninitialize||!create)return;
        HRESULT result=init(nullptr,COINIT_MULTITHREADED);
        initialized=SUCCEEDED(result);
        if(FAILED(result)&&result!=RPC_E_CHANGED_MODE)create=nullptr;
    }
    ~ComRuntime(){if(initialized)uninitialize();if(library)FreeLibrary(library);}
};
inline bool jpeg_or_png(const Blob& data){
    return data.size()>=8&&((data[0]==0xff&&data[1]==0xd8&&data[2]==0xff)||
        (data[0]==0x89&&data[1]=='P'&&data[2]=='N'&&data[3]=='G'&&data[4]==13&&data[5]==10&&data[6]==26&&data[7]==10));
}
inline Blob encode(IWICImagingFactory* factory,IWICBitmapSource* source,UINT width,UINT height,float quality){
    constexpr GUID jpeg={0x19e4a5aa,0x5662,0x4fc5,{0xa0,0xc0,0x17,0x58,0x02,0x8e,0x10,0x57}};
    constexpr GUID bgr={0x6fddc324,0x4e03,0x4bfe,{0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x0c}};
    std::array<BYTE,65536> output{};
    ComPtr<IWICStream> stream;
    if(FAILED(factory->CreateStream(stream.out()))||FAILED(stream->InitializeFromMemory(output.data(),static_cast<DWORD>(output.size()))))return {};
    ComPtr<IWICBitmapEncoder> encoder;
    if(FAILED(factory->CreateEncoder(jpeg,nullptr,encoder.out()))||FAILED(encoder->Initialize(stream.value,WICBitmapEncoderNoCache)))return {};
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    if(FAILED(encoder->CreateNewFrame(frame.out(),options.out())))return {};
    if(options){PROPBAG2 property{};property.pstrName=const_cast<LPOLESTR>(L"ImageQuality");VARIANT value{};value.vt=VT_R4;value.fltVal=quality;if(FAILED(options->Write(1,&property,&value)))return {};}
    if(FAILED(frame->Initialize(options.value))||FAILED(frame->SetSize(width,height)))return {};
    GUID format=bgr;
    if(FAILED(frame->SetPixelFormat(&format))||format!=bgr)return {};
    ComPtr<IWICFormatConverter> converter;
    if(FAILED(factory->CreateFormatConverter(converter.out()))||FAILED(converter->Initialize(source,bgr,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return {};
    if(FAILED(frame->WriteSource(converter.value,nullptr))||FAILED(frame->Commit())||FAILED(encoder->Commit()))return {};
    LARGE_INTEGER zero{};ULARGE_INTEGER position{};
    if(FAILED(stream->Seek(zero,STREAM_SEEK_CUR,&position))||position.QuadPart==0||position.QuadPart>2048)return {};
    return Blob(output.begin(),output.begin()+static_cast<size_t>(position.QuadPart));
}
}

// A thumbnail is optional presentation data; no failure may affect the original
// transfer. Source is read before encryption and never retained by this helper.
inline Blob create(transfer::Source& source){
    try{
        constexpr std::uint64_t maxEncoded=32ull*1024*1024;
        const std::uint64_t length=source.size();
        if(length<8||length>maxEncoded)return {};
        Blob input;input.reserve(static_cast<size_t>(length));
        while(input.size()<length){
            const size_t request=std::min(transfer::ChunkBytes,static_cast<size_t>(length-input.size()));
            Blob part=source.read(input.size(),request);
            if(part.empty()||part.size()>request)return {};
            input.insert(input.end(),part.begin(),part.end());
        }
        if(!detail::jpeg_or_png(input))return {};
        detail::ComRuntime com;
        if(!com.create)return {};
        constexpr GUID clsid={0xcacaf262,0x9370,0x4615,{0xa1,0x3b,0x9f,0x55,0x39,0xda,0x4c,0x0a}};
        constexpr GUID iid={0xec5ec8a9,0xc395,0x4314,{0x9c,0x77,0x54,0xd7,0xa9,0x35,0xff,0x70}};
        detail::ComPtr<IWICImagingFactory> factory;
        if(FAILED(com.create(clsid,nullptr,CLSCTX_INPROC_SERVER,iid,reinterpret_cast<void**>(factory.out()))))return {};
        detail::ComPtr<IWICStream> stream;
        if(FAILED(factory->CreateStream(stream.out()))||FAILED(stream->InitializeFromMemory(input.data(),static_cast<DWORD>(input.size()))))return {};
        detail::ComPtr<IWICBitmapDecoder> decoder;
        if(FAILED(factory->CreateDecoderFromStream(stream.value,nullptr,WICDecodeMetadataCacheOnDemand,decoder.out())))return {};
        detail::ComPtr<IWICBitmapFrameDecode> frame;
        if(FAILED(decoder->GetFrame(0,frame.out())))return {};
        UINT width=0,height=0;
        if(FAILED(frame->GetSize(&width,&height))||!width||!height||width>16384||height>16384||
            static_cast<std::uint64_t>(width)*height>16ull*1024*1024)return {};
        for(UINT edge:{128u,96u,64u,48u,32u}){
            const double scale=std::min({1.0,static_cast<double>(edge)/width,static_cast<double>(edge)/height});
            const UINT targetWidth=std::max(1u,static_cast<UINT>(width*scale));
            const UINT targetHeight=std::max(1u,static_cast<UINT>(height*scale));
            detail::ComPtr<IWICBitmapScaler> scaler;
            if(FAILED(factory->CreateBitmapScaler(scaler.out()))||FAILED(scaler->Initialize(frame.value,targetWidth,targetHeight,WICBitmapInterpolationModeFant)))return {};
            for(float quality:{0.55f,0.35f,0.20f,0.10f}){
                Blob result=detail::encode(factory.value,scaler.value,targetWidth,targetHeight,quality);
                if(!result.empty())return result;
            }
        }
    }catch(...){/* Optional thumbnail. */}
    return {};
}
}

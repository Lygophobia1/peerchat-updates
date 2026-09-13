#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <atomic>
#include <vector>
#include <string>
#include <stdexcept>
#include <iostream>
#include <thread>
#include <chrono>

// Fake only the Windows device boundary. Real Audio worker/events/buffers and
// lifecycle execute, without relying on an installed microphone or driver.
namespace fake {
HANDLE eventHandle;
std::vector<WAVEHDR*> headers;
bool failOpen=false,failStart=false;
std::atomic<int> inputs{0},outputs{0},invalidQueues{0};
UINT count(){return 1;}
MMRESULT caps(UINT_PTR,WAVEINCAPSW* caps,UINT){*caps={};lstrcpyW(caps->szPname,L"Test Hands-Free");return 0;}
MMRESULT message(HWAVEIN,UINT,DWORD_PTR id,DWORD_PTR flags){*reinterpret_cast<DWORD*>(id)=0;*reinterpret_cast<DWORD*>(flags)=1;return 0;}
MMRESULT openIn(LPHWAVEIN handle,UINT id,LPCWAVEFORMATEX,DWORD_PTR event,DWORD_PTR,DWORD){if(id!=0)return MMSYSERR_BADDEVICEID;if(failOpen)return MMSYSERR_ERROR;*handle=reinterpret_cast<HWAVEIN>(1);++inputs;eventHandle=reinterpret_cast<HANDLE>(event);headers.clear();return 0;}
MMRESULT prepareIn(HWAVEIN,WAVEHDR* header,UINT){header->dwFlags=WHDR_PREPARED;return 0;}
MMRESULT add(HWAVEIN handle,WAVEHDR* header,UINT){if(!handle||inputs==0){++invalidQueues;return MMSYSERR_INVALHANDLE;}header->dwFlags=WHDR_PREPARED|WHDR_INQUEUE;headers.push_back(header);return 0;}
MMRESULT start(HWAVEIN){if(failStart)return MMSYSERR_ERROR;for(auto* header:headers){for(DWORD i=0;i+1<header->dwBufferLength;i+=2){header->lpData[i]=0;header->lpData[i+1]=0x40;}header->dwFlags=WHDR_PREPARED|WHDR_DONE;header->dwBytesRecorded=header->dwBufferLength;}SetEvent(eventHandle);return 0;}
MMRESULT resetIn(HWAVEIN){return 0;}
MMRESULT unprepareIn(HWAVEIN,WAVEHDR* header,UINT){header->dwFlags=0;return 0;}
MMRESULT closeIn(HWAVEIN){--inputs;headers.clear();return 0;}
MMRESULT openOut(LPHWAVEOUT handle,UINT,LPCWAVEFORMATEX,DWORD_PTR,DWORD_PTR,DWORD){*handle=reinterpret_cast<HWAVEOUT>(2);++outputs;return 0;}
MMRESULT prepareOut(HWAVEOUT,WAVEHDR* header,UINT){header->dwFlags=WHDR_PREPARED;return 0;}
MMRESULT write(HWAVEOUT,WAVEHDR* header,UINT){header->dwFlags=WHDR_PREPARED|WHDR_INQUEUE;return 0;}
MMRESULT resetOut(HWAVEOUT){return 0;}
MMRESULT unprepareOut(HWAVEOUT,WAVEHDR* header,UINT){header->dwFlags=0;return 0;}
MMRESULT closeOut(HWAVEOUT){--outputs;return 0;}
}
#define waveInGetNumDevs fake::count
#define waveInGetDevCapsW fake::caps
#define waveInMessage fake::message
#define waveInOpen fake::openIn
#define waveInPrepareHeader fake::prepareIn
#define waveInAddBuffer fake::add
#define waveInStart fake::start
#define waveInReset fake::resetIn
#define waveInUnprepareHeader fake::unprepareIn
#define waveInClose fake::closeIn
#define waveOutOpen fake::openOut
#define waveOutPrepareHeader fake::prepareOut
#define waveOutWrite fake::write
#define waveOutReset fake::resetOut
#define waveOutUnprepareHeader fake::unprepareOut
#define waveOutClose fake::closeOut
#include "audio.cpp"

void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    peerchat::Audio audio;std::wstring error;std::atomic<int> delivered{0};
    audio.setDevices(0,0);
    audio.setCaptureHandler([&](const std::vector<unsigned char>&){std::wstring ignored;audio.setMuted(true,ignored);++delivered;});
    for(int cycle=0;cycle<3;++cycle){
        require(audio.start(error)&&error.empty(),"Capture did not start");
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(delivered<=cycle&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(5));
        require(delivered==cycle+1,"Capture callback did not run");
        audio.stop();audio.stop();
        require(fake::inputs==0&&fake::outputs==0,"Device handle leaked across calls");
        require(fake::invalidQueues==0,"Callback mute requeued a header on a closed microphone");
    }
    peerchat::Audio metered;metered.setDevices(0,0);metered.setCaptureHandler([](const std::vector<unsigned char>&){});
    require(metered.start(error)&&error.empty(),"Metered capture did not start");
    auto meterDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);peerchat::MediaTelemetry levels;
    do{levels=metered.telemetry();std::this_thread::sleep_for(std::chrono::milliseconds(5));}while(levels.inputLevel<0.1f&&std::chrono::steady_clock::now()<meterDeadline);
    metered.play(1,std::vector<unsigned char>(640,0x40));levels=metered.telemetry();
    require(levels.active&&levels.inputAvailable&&levels.outputAvailable&&levels.inputLevel>0.1f&&levels.outputLevel>0,
            "Existing capture/playback PCM did not update generic telemetry");
    metered.stop();levels=metered.telemetry();require(!levels.active&&levels.inputLevel==0&&levels.outputLevel==0,
            "Media telemetry did not reset after device teardown");
    fake::failOpen=true;
    require(audio.start(error)&&!error.empty()&&fake::outputs==1,"Capture-open failure lost receive-only playback");audio.stop();
    fake::failOpen=false;fake::failStart=true;
    require(audio.start(error)&&!error.empty()&&fake::inputs==0&&fake::outputs==1,"Capture-start failure leaked input or lost playback");audio.stop();
    require(fake::inputs==0&&fake::outputs==0,"Failure teardown leaked devices");
    std::cout<<"PASS: three device sessions, callback mute, buffer generation, open/start failures, receive-only playback and handle cleanup.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <map>
#include <vector>
namespace peerchat::android_audio {
constexpr std::size_t Samples=320, Bytes=640, QueueFrames=5, MaxSources=32;
inline float gain(float value){return std::isfinite(value)?std::clamp(value,0.0f,4.0f):1.0f;}
inline std::int16_t scale(int value,float volume){return static_cast<std::int16_t>(std::clamp(value*gain(volume),-32768.0f,32767.0f));}
// Compatibility queue for Audio::play; VoiceReceiveEngine uses device-paced pull.
// Caller serializes access. Queue age cannot grow beyond five 20 ms frames.
struct Buffers {
    std::map<int,std::deque<std::vector<unsigned char>>> playback;
    std::deque<std::vector<unsigned char>> captured;
    std::uint64_t dropped=0;
    bool play(int source,const std::vector<unsigned char>& bytes){
        if(bytes.size()!=Bytes)return false;
        if(!playback.count(source)&&playback.size()>=MaxSources)return false;
        auto& queue=playback[source];if(queue.size()==QueueFrames){queue.pop_front();++dropped;}
        queue.push_back(bytes);return true;
    }
    void capture(std::vector<unsigned char> bytes){
        if(bytes.size()!=Bytes)return;
        if(captured.size()==QueueFrames){captured.pop_front();++dropped;}
        captured.push_back(std::move(bytes));
    }
    void pull(std::int16_t* output,float volume){
        std::array<int,Samples> mix{};
        for(auto it=playback.begin();it!=playback.end();){
            auto& queue=it->second;
            if(!queue.empty()){
                const auto& bytes=queue.front();
                for(std::size_t i=0;i<Samples;++i)mix[i]+=static_cast<std::int16_t>(unsigned(bytes[2*i])|(unsigned(bytes[2*i+1])<<8));
                queue.pop_front();
            }
            if(queue.empty())it=playback.erase(it);else ++it;
        }
        for(std::size_t i=0;i<Samples;++i)output[i]=scale(mix[i],volume);
    }
    void clear(){playback.clear();captured.clear();}
};
}

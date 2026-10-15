#include "../portable/audio_android_buffer.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace peerchat::android_audio;
std::vector<unsigned char> frame(std::int16_t value){
    std::vector<unsigned char> result(Bytes);
    for(std::size_t i=0;i<Samples;++i){result[2*i]=static_cast<unsigned char>(value);result[2*i+1]=static_cast<unsigned char>(static_cast<std::uint16_t>(value)>>8);}return result;
}
int main(){
    Buffers b;std::array<std::int16_t,Samples> out{};
    assert(!b.play(1,std::vector<unsigned char>(639)));
    for(int i=0;i<100;++i)assert(b.play(1,frame(static_cast<std::int16_t>(i))));
    assert(b.playback.at(1).size()==QueueFrames&&b.dropped==95);
    b.pull(out.data(),1);assert(out.front()==95&&out.back()==95);
    b.clear();b.play(1,frame(30000));b.play(2,frame(30000));b.pull(out.data(),1);assert(out.front()==32767);
    b.play(1,frame(-30000));b.play(2,frame(-30000));b.pull(out.data(),1);assert(out.front()==-32768);
    b.pull(out.data(),1);assert(out.front()==0&&b.playback.empty());
    for(int i=0;i<32;++i)assert(b.play(i,frame(1)));
    assert(!b.play(32,frame(1)));b.pull(out.data(),1);assert(out.front()==32);
    assert(b.play(32,frame(1000)));b.pull(out.data(),0);assert(out.front()==0);
    for(int i=0;i<50;++i)b.capture(frame(static_cast<std::int16_t>(i)));
    assert(b.captured.size()==QueueFrames&&b.captured.front()[0]==45);
    b.clear();assert(b.captured.empty()&&b.playback.empty());
    assert(scale(20000,4)==32767&&scale(-20000,4)==-32768);
    assert(gain(std::numeric_limits<float>::quiet_NaN())==1);
    assert(gain(std::numeric_limits<float>::infinity())==1);
    assert(gain(-1)==0&&gain(100)==4);
    std::cout<<"PASS Android PCM bounds, stale-drop, mixing, clipping, mute-clear and invalid gain\n";
}

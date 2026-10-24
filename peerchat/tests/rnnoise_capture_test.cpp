#include "rnnoise_capture.hpp"
#include "voice_apm_capture.hpp"
#include <iostream>
#include <chrono>
#include <fstream>
#include <vector>
#include <cassert>
#include <ctime>
int main(int argc,char** argv){
    for(unsigned level=0;level<3;++level){
        peerchat::AutomaticNoiseGate tuned;tuned.configure(level);
        const float minimum=level==0?.06f:(level==1?.02f:.008f);
        for(int b=0;b<1200;++b){bool open=tuned.decide(.7f,1000,200);for(int i=0;i<480;++i)tuned.next(open);}
        assert(tuned.background()&&std::abs(tuned.gain()-minimum)<.0001f&&tuned.noiseFloor()>900);
        for(int b=0;b<100;++b)assert(!tuned.decide(.7f,1000,400));
        const float noiseFloor=tuned.noiseFloor();
        assert(tuned.decide(.6f,100,90));
        for(int i=0;i<480;++i)tuned.next(true);
        assert(!tuned.background()&&tuned.gain()>.99f&&tuned.noiseFloor()==noiseFloor);
        for(int i=0;i<24;++i)assert(tuned.decide(.01f,90,10));
        assert(!tuned.decide(.01f,90,10));
        const float before=tuned.gain();tuned.next(false);assert(tuned.gain()<before&&tuned.gain()>before-.001f);
        tuned.reset();assert(tuned.gain()==1&&!tuned.background()&&tuned.noiseFloor()==80);
        peerchat::RnNoiseCapture profile;assert(profile.open(level));
        assert(std::abs(profile.trace().dryMix-(level==0?.20f:(level==1?.15f:.10f)))<.0001f);
    }
    peerchat::AutomaticNoiseGate gate;
    for(int b=0;b<350;++b){bool open=gate.decide(0.01f,100,8);for(int i=0;i<480;++i)gate.next(open);}
    assert(gate.gain()<0.13f&&gate.noiseFloor()>80);
    float floor=gate.noiseFloor();
    assert(gate.decide(.6f,120,105));for(int i=0;i<480;++i)gate.next(true);
    assert(gate.gain()>.99f);
    for(int i=0;i<200;++i)gate.decide(.7f,800,700);
    assert(gate.noiseFloor()==floor); // voiced material cannot train the floor
    for(int i=0;i<20;++i)assert(gate.decide(.01f,90,10));
    peerchat::RnNoiseCapture rn;assert(rn.open());assert(!rn.processFrame(nullptr));
    struct Guard{int32_t before=0x12345678;std::array<int16_t,320> pcm{};int32_t after=0x23456789;} frame;
    uint32_t rng=1;double in=0,out=0;double worst=0,total=0;
    const auto cpuBegin=std::clock();
    for(int b=0;b<1000;++b){for(auto& x:frame.pcm){rng=rng*1664525+1013904223;x=static_cast<int16_t>(int(rng>>20)-2048);if(b>200)in+=double(x)*x;}
        auto begin=std::chrono::steady_clock::now();assert(rn.processFrame(frame.pcm.data()));
        double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();total+=us;worst=std::max(worst,us);
        for(auto x:frame.pcm)if(b>200)out+=double(x)*x;
        assert(frame.before==0x12345678&&frame.after==0x23456789);
    }
    std::cout<<"CPU seconds per 20 seconds of audio="<<double(std::clock()-cpuBegin)/CLOCKS_PER_SEC<<"\n";
    assert(out<in*.2);std::cout<<"Noise-only attenuation dB="<<10*log10(in/out)<<"; mean processing us/20ms="<<total/1000<<"; max us="<<worst<<"\n";
    rn.reset();frame.pcm.fill(0);assert(rn.processFrame(frame.pcm.data()));for(auto x:frame.pcm)assert(x==0);
    for(int i=0;i<30;++i){assert(rn.open());frame.pcm.fill(i%2?32767:-32768);assert(rn.processFrame(frame.pcm.data()));rn.close();assert(!rn.processFrame(frame.pcm.data()));}
    // Same input through existing AEC/AGC path, and through RNNoise + soft gate.
    if(argc==3){std::ifstream file(argv[1],std::ios::binary);std::vector<int16_t> pcm;int16_t value;while(file.read(reinterpret_cast<char*>(&value),2))pcm.push_back(value);
        for(int mode=0;mode<2;++mode){peerchat::VoiceApmCapture apm;assert(apm.open(true,true,peerchat::NoiseSuppressionStrength::Medium,peerchat::AutomaticGainStrength::Balanced,false,mode==1));
            std::ofstream output(std::string(argv[2])+(mode?"-automatic.pcm":"-original.pcm"),std::ios::binary);
            for(size_t i=0;i+320<=pcm.size();i+=320){std::copy_n(pcm.data()+i,320,frame.pcm.data());assert(apm.processFrame(frame.pcm.data()));output.write(reinterpret_cast<const char*>(frame.pcm.data()),640);}
        }
    }
    std::cout<<"PASS: real RNNoise, adaptive gate, speech-floor freeze, soft hangover/attack, bounded frames, reset and 30 lifetimes. Signal delay: 480+62 samples at 48kHz = 11.292 ms.\n";
}

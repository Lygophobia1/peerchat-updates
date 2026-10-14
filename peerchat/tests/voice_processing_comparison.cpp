#include "../voice_apm_capture.hpp"
#include <iostream>
#include <cmath>
int main(){
    for(unsigned ns=0;ns<3;++ns)for(unsigned agc=0;agc<3;++agc){
        peerchat::VoiceApmCapture apm;if(!apm.open(true,true,static_cast<peerchat::NoiseSuppressionStrength>(ns),static_cast<peerchat::AutomaticGainStrength>(agc),false))return 1;
        double raw=0,after=0;unsigned full=0;std::uint32_t seed=12345;
        for(unsigned frame=0;frame<1000;++frame){std::array<std::int16_t,320> pcm{};
            for(unsigned i=0;i<320;++i){double t=(frame*320+i)/16000.0;seed=1664525*seed+1013904223;double fan=800*std::sin(t*2*3.141592653589793*150)+400*std::sin(t*2*3.141592653589793*300)+(int(seed>>16)-32768)*.012;double speech=(frame>=250&&frame<750)?2000*std::sin(t*2*3.141592653589793*185)*(0.6+0.4*std::sin(t*2*3.141592653589793*3)):0;pcm[i]=static_cast<std::int16_t>(fan+speech);if(frame>=800)raw+=double(pcm[i])*pcm[i];}
            if(!apm.processFrame(pcm.data()))return 2;
            if(frame>=800)for(auto sample:pcm){after+=double(sample)*sample;if(sample==32767||sample==-32768)++full;}
        }
        std::cout<<"SYNTHETIC_FAN ns="<<ns<<" agc="<<agc<<" final_fan_raw_rms="<<std::sqrt(raw/64000)<<" processed_rms="<<std::sqrt(after/64000)<<" full_scale_samples="<<full<<" (not perceptual speech quality or physical fan)\n";
    }
}

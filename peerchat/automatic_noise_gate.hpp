#pragma once
#include <algorithm>
#include <cmath>
namespace peerchat {
// 10 ms decisions, sample-smoothed gain. The deliberately soft floor preserves
// uncertain/quiet speech instead of hard muting it. No voice identity inference.
class AutomaticNoiseGate {
    float floor_=80, gain_=1;
    unsigned hold_=30, evidence_=0, rejected_=0;
    bool background_=false;
    float closedTarget_=.02f, minimum_=.02f;
    unsigned rejectionFrames_=160;
public:
    void configure(unsigned level){minimum_=level==0?.06f:(level==2?.008f:.02f);rejectionFrames_=level==0?200u:(level==2?120u:160u);reset();}
    float target()const{return closedTarget_;}
    bool background()const{return background_;}
    void reset(){floor_=80;gain_=1;hold_=30;evidence_=0;rejected_=0;background_=false;closedTarget_=minimum_;}
    float noiseFloor()const{return floor_;}
    float gain()const{return gain_;}
    bool decide(float probability,float rms,float retainedRms){
        const float ratio=rms/std::max(20.0f,floor_);
        const bool levelEvidence=ratio>2.5f && probability>0.05f;
        evidence_=levelEvidence?std::min(3u,evidence_+1):0;
        // High VAD alone can latch on harmonic fan noise. Require sustained
        // denoiser rejection before overriding it; transient weak consonants
        // retain the original speech path and hangover.
        rejected_=retainedRms < .35f*rms && rms>20 ? std::min(400u,rejected_+1) : (rejected_>2?rejected_-2:0);
        if(retainedRms>.5f*rms && probability>.12f){background_=false;rejected_=0;}
        if(rejected_>=rejectionFrames_)background_=true;
        const bool rejectedBackground=background_;
        closedTarget_=rejectedBackground?minimum_:(minimum_+(.12f-minimum_)*std::clamp(probability/.15f,0.0f,1.0f));
        const bool speech=!rejectedBackground && (probability>=0.35f ||
            (probability>=0.15f && retainedRms>0.45f*rms && ratio>1.25f) || evidence_>=2);
        if(speech)hold_=25;else if(hold_)--hold_;
        // Freeze during speech and its hangover. Learn persistent low-confidence
        // backgrounds slowly, with a bounded upward step, to protect whispers.
        if(!speech&&!hold_&&(probability<0.12f||rejectedBackground)){
            const float target=std::clamp(rms,10.0f,std::max(40.0f,floor_*1.5f));
            floor_+=(target-floor_)*(target<floor_?0.03f:0.01f);
        }
        return speech||hold_>0;
    }
    float next(bool open){
        const float target=open?1.0f:closedTarget_;
        // 2 ms attack, 180 ms release at RNNoise's 48 kHz rate.
        gain_+=(target-gain_)*(open?0.0103626f:0.000115734f);
        return gain_;
    }
};
}

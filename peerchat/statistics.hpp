#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>

namespace peerchat::statistics {
// Passive bounded meter. The existing worker owns clock, lifetime and publication;
// this has no transport, persistence, timer thread or event log.
struct RateSample {std::uint64_t confirmedBytes=0,bitsPerSecond=0,elapsedMilliseconds=0;bool sampled=false,etaReliable=false;};
class RateMeter {
    std::uint64_t highWater_=0,at_=0,sampleBytes_=0,progressAt_=0,rate_=0;unsigned steady_=0;
public:
    void baseline(std::uint64_t bytes,std::uint64_t now){highWater_=sampleBytes_=bytes;at_=progressAt_=now;rate_=0;steady_=0;}
    RateSample observe(std::uint64_t confirmed,std::uint64_t now,bool active,bool finish=false,bool paused=false){
        if(confirmed>highWater_){highWater_=confirmed;progressAt_=now;}
        if(now<at_||paused){baseline(highWater_,now);return {highWater_,0,0,false,false};}
        const auto elapsed=now-at_;if(elapsed<1000&&!finish)return {highWater_,current(now,active),0,false,steady_>=2};
        const auto delta=highWater_-sampleBytes_;
        const auto scaled=delta>std::numeric_limits<std::uint64_t>::max()/8000?std::numeric_limits<std::uint64_t>::max():delta*8000;
        const auto rate=active&&elapsed&&delta&&now-progressAt_<1000?scaled/elapsed:0;
        const auto low=std::min(rate,rate_),high=std::max(rate,rate_);
        const bool steady=low&&high/low<=4;
        steady_=rate?(steady?std::min(2u,steady_+1):1u):0;rate_=rate;at_=now;sampleBytes_=highWater_;
        return {highWater_,rate_,elapsed,true,steady_>=2};
    }
    std::uint64_t current(std::uint64_t now,bool active)const{return active&&now>=progressAt_&&now-progressAt_<1000?rate_:0;}
};
}

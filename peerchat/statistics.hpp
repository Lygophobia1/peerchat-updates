#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <string>

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
// Session-local counter views reuse the same meter; no second collection owner.
class CounterRates {
    struct Counter {RateMeter meter;std::uint64_t previous=0;};
    std::map<std::string,Counter> rows_;
public:
    void clear(){rows_.clear();}
    std::uint64_t sample(const std::string& key,std::uint64_t value,std::uint64_t now,bool active){
        auto i=rows_.find(key);if(i==rows_.end()){if(rows_.size()>=256)return 0;i=rows_.emplace(key,Counter{}).first;i->second.meter.baseline(value,now);}
        if(value<i->second.previous)i->second.meter.baseline(value,now);
        i->second.previous=value;i->second.meter.observe(value,now,active);return i->second.meter.current(now,active);
    }
};
}

#pragma once
#include <chrono>
#include <algorithm>
#include <cstdint>
namespace peerchat::screen {
// Capture callbacks are display-paced, not exact multiples of the target rate.
// Keep target phase instead of accumulating callback lateness. Admit at most
// one frame per callback; a stall resets phase rather than creating a burst.
class CaptureCadence {
public:
    using Clock=std::chrono::steady_clock;
    explicit CaptureCadence(std::uint32_t fps):period_(1000000000ull/std::max<std::uint32_t>(1,fps)){}
    bool admit(Clock::time_point now){
        if(!started_){started_=true;next_=now+period_;return true;}
        if(now<next_)return false;
        next_+=period_;
        if(next_<=now)next_=now+period_;
        return true;
    }
private:
    std::chrono::nanoseconds period_;
    Clock::time_point next_{};
    bool started_=false;
};
}

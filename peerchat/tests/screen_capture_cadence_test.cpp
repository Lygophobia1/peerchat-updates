#include "screen_capture_cadence.hpp"
#include <iostream>
#include <stdexcept>
using Clock=peerchat::screen::CaptureCadence::Clock;
static void check(bool v){if(!v)throw std::runtime_error("Capture cadence regression");}
int main(){
    auto start=Clock::time_point{};peerchat::screen::CaptureCadence pacing(30);
    unsigned accepted=0,legacy=0;Clock::time_point last{};bool have=false;
    for(unsigned i=0;i<600;++i){auto now=start+std::chrono::nanoseconds(16666667ull*i+(i%3)*50000);
        accepted+=pacing.admit(now);
        if(!have||now-last>=std::chrono::nanoseconds(33333333)){++legacy;last=now;have=true;}
    }
    check(accepted>=299&&accepted<=301&&legacy<270);
    auto late=start+std::chrono::seconds(30);check(pacing.admit(late));
    for(unsigned i=0;i<100;++i)check(!pacing.admit(late+std::chrono::microseconds(i)));
    check(!pacing.admit(late+std::chrono::milliseconds(32)));
    check(pacing.admit(late+std::chrono::milliseconds(34)));
    for(unsigned fps:{15u,30u,60u}){peerchat::screen::CaptureCadence p(fps);unsigned n=0;for(unsigned i=0;i<10000;++i)n+=p.admit(start+std::chrono::milliseconds(i));check(n>=fps*10&&n<=fps*10+1);}
    std::cout<<"PASS: jittered 60-Hz callbacks/30-FPS target legacy="<<legacy<<" phase_preserved="<<accepted<<"; stall/no bursts; 15/30/60-FPS limits\n";
}

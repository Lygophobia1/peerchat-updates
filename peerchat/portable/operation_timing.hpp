#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>

namespace peerchat::portable {
// Session-local observations only. Caller holds the existing native owner lock.
// No timers, payload retention, scheduling, database writes or authorization.
struct OperationTiming {
    std::uint64_t samples=0,waitUs=0,waitMaxUs=0,workUs=0,workMaxUs=0;
    static std::uint64_t now(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
    void observe(std::uint64_t wait,std::uint64_t work){++samples;waitUs+=wait;waitMaxUs=std::max(waitMaxUs,wait);workUs+=work;workMaxUs=std::max(workMaxUs,work);}
    std::string line(const char* prefix)const{
        const std::string p=prefix;
        return " "+p+"_samples="+std::to_string(samples)+" "+p+"_gate_wait_us="+std::to_string(waitUs)+
            " "+p+"_gate_wait_max_us="+std::to_string(waitMaxUs)+" "+p+"_processing_us="+std::to_string(workUs)+
            " "+p+"_processing_max_us="+std::to_string(workMaxUs);
    }
    struct Scope {
        OperationTiming* target;std::uint64_t waiting,entered=now();
        Scope(OperationTiming* value,std::uint64_t began):target(value),waiting(began){}
        ~Scope(){if(target)target->observe(entered>=waiting?entered-waiting:0,now()-entered);}
    };
};
}

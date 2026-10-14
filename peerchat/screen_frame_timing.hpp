#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <sstream>
namespace peerchat::screen {
// Owner-serialized, fixed memory. Local completion/arrival gaps are not scanout
// timestamps or end-to-end latency. Idle and network stalls are not inferred.
class FrameTiming {
    std::array<std::uint64_t,120> gaps_{};
    std::uint64_t previous_=0,count_=0,total_=0,maximum_=0,longGaps_=0;
public:
    void observe(std::uint64_t us,std::uint64_t period){
        if(previous_&&us>=previous_){auto gap=us-previous_;gaps_[count_%gaps_.size()]=gap;++count_;total_+=gap;maximum_=std::max(maximum_,gap);if(gap>period+period/2)++longGaps_;}
        previous_=us;
    }
    std::wstring text(const wchar_t* prefix)const{
        auto copy=gaps_;const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(count_,copy.size()));
        std::sort(copy.begin(),copy.begin()+n);
        std::wostringstream out;out<<L"\n"<<prefix<<L"=samples:"<<count_<<L";mean_us:"<<(count_?total_/count_:0)<<L";max_us:"<<maximum_<<L";recent_p95_us:"<<(n?copy[(n-1)*95/100]:0)<<L";over_1_5_period:"<<longGaps_;
        return out.str();
    }
};
}

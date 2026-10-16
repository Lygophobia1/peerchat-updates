#include "../portable/operation_timing.hpp"
#include <stdexcept>
#include <iostream>
int main(){
    peerchat::portable::OperationTiming timing;
    timing.observe(100,300);timing.observe(200,40);
    if(timing.samples!=2||timing.waitUs!=300||timing.waitMaxUs!=200||timing.workUs!=340||timing.workMaxUs!=300)throw std::runtime_error("Counter semantics changed");
    const auto value=timing.line("receive");
    if(value.find("receive_gate_wait_us=300 ")==std::string::npos||value.find("receive_processing_us=340 ")==std::string::npos)throw std::runtime_error("Diagnostic units changed");
    {peerchat::portable::OperationTiming::Scope scope(&timing,peerchat::portable::OperationTiming::now());}
    if(timing.samples!=3)throw std::runtime_error("Scope observation missing");
    std::cout<<"PASS fixed native timing counters separate lock wait and processing without changing operation ownership.\n";
}

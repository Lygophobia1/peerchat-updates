#include "windows_transfer_timing.hpp"
#include <iostream>
using namespace peerchat;
int main(){
    transfer::windows::TransferTiming timing;
    auto id=std::string(32,'a');auto frame=[&](transfer::Kind kind,std::uint64_t offset){protocol::Writer w(protocol::FileTransfer);w.number(1);w.number(static_cast<unsigned>(kind));w.string(id);w.number(offset);if(kind==transfer::Kind::Chunk)w.bytes(Blob(32768,7));return w.data;};
    auto check=[](bool value){if(!value)throw std::runtime_error("Timing regression");};
    timing.sent(frame(transfer::Kind::Chunk,0),4,100,110);
    timing.received(frame(transfer::Kind::Resume,32768),3,1000,1100); // stale route
    timing.received(frame(transfer::Kind::Resume,0),4,1000,1100); // not payload confirmation
    check(timing.diagnostics(id).find("ack_samples=0 ")!=std::string::npos);
    timing.received(frame(transfer::Kind::Resume,32768),4,1110,1200);
    timing.received(frame(transfer::Kind::Resume,32768),4,1400,1500); // duplicate
    timing.sent(frame(transfer::Kind::Chunk,32768),4,1610,1620);
    auto result=timing.diagnostics(id);check(result.find("ack_samples=1 ")!=std::string::npos&&result.find("ack_turnaround_us=1000 ")!=std::string::npos&&result.find("sender_ack_to_send_us=500 ")!=std::string::npos);
    timing.clear();timing.received(frame(transfer::Kind::Chunk,0),4,100,300);timing.sent(frame(transfer::Kind::Resume,32768),4,400,410);
    result=timing.diagnostics(id);check(result.find("receiver_dispatch_us=200 ")!=std::string::npos&&result.find("receiver_dispatch_to_ack_us=310 ")!=std::string::npos);
    timing.clear();check(timing.diagnostics(id).empty());
    timing.sent({},4,100,110);timing.received({},4,100,110);check(timing.diagnostics(id).empty());
    for(size_t n=0;n<65;++n){id=std::string(30,'0')+"0123456789abcdef"[n/16]+"0123456789abcdef"[n%16];timing.sent(frame(transfer::Kind::Chunk,0),4,100,110);}
    check(timing.diagnostics(id).empty()); // Supplemental observations are bounded, not transfer authority.
    std::cout<<"PASS: bounded session-only timing, exact ACK offset/route, duplicate/stale rejection, receiver processing, reply scheduling, profile reset.\n";
}

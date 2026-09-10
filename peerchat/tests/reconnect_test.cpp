#include "node.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <thread>

using namespace peerchat;
using Clock=std::chrono::steady_clock;
struct Seen {NodeEvent event;Clock::time_point at;};
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void drain(Node& node,std::vector<Seen>& seen){for(auto& event:node.poll())seen.push_back({std::move(event),Clock::now()});}
template<class Check> static void until(Node& node,std::vector<Seen>& seen,Check check,const char* message,int milliseconds=5000){auto deadline=Clock::now()+std::chrono::milliseconds(milliseconds);while(Clock::now()<deadline){drain(node,seen);if(check())return;std::this_thread::sleep_for(std::chrono::milliseconds(10));}throw std::runtime_error(message);}
static std::vector<Seen> attempts(const std::vector<Seen>& seen,const std::string& peer){std::vector<Seen> result;for(const auto& value:seen)if(value.event.kind==NodeEvent::Kind::ConnectionAttempt&&value.event.expectedPeerId==peer)result.push_back(value);return result;}
static size_t schedules(const std::vector<Seen>& seen,const std::string& peer){size_t result=0;for(const auto& value:seen)if(value.event.kind==NodeEvent::Kind::ReconnectScheduled&&value.event.expectedPeerId==peer)result++;return result;}

int main(){
    try{
        auto root=std::filesystem::current_path()/L"build"/L"tests"/L"runtime"/std::filesystem::path(L"reconnect-"+std::to_wstring(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        const std::string peerA(64,'a'),peerB(64,'b');Node node;std::vector<Seen> seen;node.start((root/L"main").wstring(),29761,L"Reconnect test");until(node,seen,[&]{for(const auto& value:seen)if(value.event.kind==NodeEvent::Kind::Ready)return true;return false;},"node did not start");
        node.saveContact({peerA,L"Peer A",{},{{L"127.0.0.1",29881}},true});node.saveContact({peerB,L"Peer B",{},{{L"127.0.0.1",29882}},true});
        until(node,seen,[&]{return attempts(seen,peerA).size()>=3&&attempts(seen,peerB).size()>=3;},"two peers did not retry with backoff");
        auto a=attempts(seen,peerA),b=attempts(seen,peerB);require(a[0].event.candidate==L"127.0.0.1:29881"&&b[0].event.candidate==L"127.0.0.1:29882","simultaneous peer diagnostics crossed candidates");require(a[0].event.candidateSource==L"Saved contact"&&a[0].event.route==L"automatic reconnect","automatic retry metadata was not propagated");
        auto gapOne=std::chrono::duration_cast<std::chrono::milliseconds>(a[1].at-a[0].at).count(),gapTwo=std::chrono::duration_cast<std::chrono::milliseconds>(a[2].at-a[1].at).count();require(gapOne>=50&&gapTwo>=110&&gapTwo>gapOne,"reconnect delay did not grow exponentially");
        for(const auto& value:seen)if(value.event.kind==NodeEvent::Kind::ReconnectScheduled)require(value.event.retryDelayMilliseconds>0&&value.event.retryDelayMilliseconds<=800,"reconnect jitter exceeded the configured cap");
        until(node,seen,[&]{return schedules(seen,peerA)>=3&&schedules(seen,peerB)>=3;},"failed attempts did not enter backoff");size_t before=attempts(seen,peerA).size();node.connect(L"127.0.0.1",29881,peerA,L"Saved contact",L"manual retry",true);until(node,seen,[&]{return attempts(seen,peerA).size()>before;},"manual retry did not force an immediate attempt",300);auto manual=attempts(seen,peerA).back();require(manual.event.route==L"manual retry","manual retry metadata was lost");
        until(node,seen,[&]{return schedules(seen,peerB)>=3;},"peer B was not waiting in backoff");before=attempts(seen,peerB).size();node.connect(L"127.0.0.1",29883,peerB,L"Discovery LAN candidate",L"candidate update",true);until(node,seen,[&]{return attempts(seen,peerB).size()>before;},"candidate update did not force an immediate attempt",300);auto presence=attempts(seen,peerB).back();require(presence.event.candidate==L"127.0.0.1:29883"&&presence.event.route==L"candidate update","presence candidate retry was not correlated");node.stop();

        Node target,verifier;std::vector<Seen> targetEvents,verifierEvents;target.start((root/L"target").wstring(),29762,L"Actual peer");verifier.start((root/L"verifier").wstring(),29763,L"Verifier");std::string targetId;until(target,targetEvents,[&]{for(const auto& value:targetEvents)if(value.event.kind==NodeEvent::Kind::Ready){targetId=value.event.peerId;return true;}return false;},"target did not start");until(verifier,verifierEvents,[&]{for(const auto& value:verifierEvents)if(value.event.kind==NodeEvent::Kind::Ready)return true;return false;},"verifier did not start");const std::string expected(64,'e');verifier.saveContact({expected,L"Expected peer",{},{{L"127.0.0.1",29762}},true});
        NodeEvent mismatch;until(verifier,verifierEvents,[&]{drain(target,targetEvents);for(const auto& value:verifierEvents)if(value.event.kind==NodeEvent::Kind::TrustRequired&&value.event.expectedPeerId==expected){mismatch=value.event;return true;}return false;},"wrong identity did not reach verification");require(mismatch.peerId==targetId&&mismatch.candidate==L"127.0.0.1:29762"&&mismatch.route==L"automatic reconnect","wrong-identity attempt lost correlation");verifier.approve(targetId,false);size_t mismatchAttempts=attempts(verifierEvents,expected).size();auto quietUntil=Clock::now()+std::chrono::milliseconds(1100);while(Clock::now()<quietUntil){drain(verifier,verifierEvents);drain(target,targetEvents);std::this_thread::sleep_for(std::chrono::milliseconds(10));}require(attempts(verifierEvents,expected).size()==mismatchAttempts,"wrong-identity endpoint retried without a manual/presence trigger");target.stop();verifier.stop();
        std::cout<<"PASS: two-peer exponential reconnect backoff, manual/presence immediate retry, endpoint diagnostics, and wrong-identity suspension.\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}

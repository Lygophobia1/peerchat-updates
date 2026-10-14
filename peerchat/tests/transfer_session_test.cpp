#include "node.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <atomic>
#include <cstdlib>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
using namespace peerchat;
void require(bool b,const char* text){if(!b)throw std::runtime_error(text);}
std::uint64_t processCpuUs(){FILETIME created{},exited{},kernel{},user{};require(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)!=0,"process CPU measurement failed");return (((std::uint64_t(kernel.dwHighDateTime)<<32)|kernel.dwLowDateTime)+((std::uint64_t(user.dwHighDateTime)<<32)|user.dwLowDateTime))/10;}
std::uint64_t processPeakBytes(){PROCESS_MEMORY_COUNTERS counters{};counters.cb=sizeof(counters);require(GetProcessMemoryInfo(GetCurrentProcess(),&counters,sizeof(counters))!=0,"process memory measurement failed");return counters.PeakWorkingSetSize;}
int main(int argc,char** argv){
    Node left,right;std::string a,b;std::uint64_t la=0,ra=0;bool readyLeft=false,readyRight=false;
    auto root=std::filesystem::current_path()/L"build"/L"tests"/L"runtime"/("file-session-"+Storage::newMessageId());
    try{
        left.start((root/L"left").wstring(),0,L"File left",false);right.start((root/L"right").wstring(),0,L"File right",false);
        auto poll=[&]{
            for(auto& e:left.poll()){
                if(e.kind==NodeEvent::Kind::Ready)a=e.peerId;
                if(e.kind==NodeEvent::Kind::RelayRequested)la=e.attemptId;
                if(e.kind==NodeEvent::Kind::TrustRequired)require(left.pinInitialAuthenticatedFriendIdentity(e.peerId,e.attemptId,1),"left constrained identity pin rejected");
                if(e.kind==NodeEvent::Kind::Connected)readyLeft=true;
                if(e.kind==NodeEvent::Kind::Disconnected)readyLeft=false;
                if(e.kind==NodeEvent::Kind::Error)std::wcerr<<L"left: "<<e.text<<L"\n";
            }
            for(auto& e:right.poll()){
                if(e.kind==NodeEvent::Kind::Ready)b=e.peerId;
                if(e.kind==NodeEvent::Kind::RelayRequested)ra=e.attemptId;
                if(e.kind==NodeEvent::Kind::TrustRequired)require(right.pinInitialAuthenticatedFriendIdentity(e.peerId,e.attemptId,1),"right constrained identity pin rejected");
                if(e.kind==NodeEvent::Kind::Connected)readyRight=true;
                if(e.kind==NodeEvent::Kind::Disconnected)readyRight=false;
                if(e.kind==NodeEvent::Kind::Error)std::wcerr<<L"right: "<<e.text<<L"\n";
            }
        };
        auto pump=[&]{unsigned char bytes[65536];if(la&&ra){
            for(int n=0;n<8;++n){int count=left.takeRelayOutput(la,bytes,sizeof(bytes),0);if(count<=0)break;require(right.relayReceive(ra,bytes,count),"right relay input rejected");}
            for(int n=0;n<8;++n){int count=right.takeRelayOutput(ra,bytes,sizeof(bytes),0);if(count<=0)break;require(left.relayReceive(la,bytes,count),"left relay input rejected");}
        }poll();};
        auto wait=[&](auto condition,const char* failure,int seconds=12){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);while(std::chrono::steady_clock::now()<end){pump();if(condition())return;std::this_thread::sleep_for(std::chrono::milliseconds(5));}throw std::runtime_error(failure);};
        wait([&]{return !a.empty()&&!b.empty();},"nodes did not open");
        bool denied=false;try{left.fileTransfer(1,b,"",L"not-a-file");}catch(...){denied=true;}require(denied,"unverified device accepted file send");
        left.replaceAuthenticatedFriends(71,1,{{72,b,{b}}});right.replaceAuthenticatedFriends(72,1,{{71,a,{a}}});
        left.setNetworkContext(1,L"file-relay-test");right.setNetworkContext(1,L"file-relay-test");
        left.updateCandidates(b,L"",0,L"",0,true,1,true);right.updateCandidates(a,L"",0,L"",0,true,1,true);
        wait([&]{return la&&ra;},"no relay demands");
        require(left.relayReady(la,a>b)&&right.relayReady(ra,b>a),"relay binding failed");
        wait([&]{return readyLeft&&readyRight;},"mutual TLS and device trust failed");
        if(argc==2&&std::string(argv[1])=="--benchmark"){
            const auto delayText=std::getenv("PEERCHAT_TEST_RELAY_DELAY_MS");
            const int relayDelay=delayText?std::stoi(delayText):0;require(relayDelay>=0&&relayDelay<=100,"invalid fixture delay");
            const bool observeDiagnostics=std::getenv("PEERCHAT_TEST_TRANSFER_DIAGNOSTICS")!=nullptr;
            // Blocking relay writers model production independently of UI polling.
            // Do not quantize every payload/ACK through the test observer's sleep.
            std::atomic<bool> forwarding{true},forwardFailed{false};
            auto forward=[&](Node& from,Node& to,std::uint64_t outgoing,std::uint64_t incoming){
                try{unsigned char bytes[65536];while(forwarding){
                    int count=from.takeRelayOutput(outgoing,bytes,sizeof(bytes),50);
                    if(count>0){if(relayDelay)std::this_thread::sleep_for(std::chrono::milliseconds(relayDelay));if(!to.relayReceive(incoming,bytes,count)){forwardFailed=true;break;}}
                }}catch(...){forwardFailed=true;}
            };
            std::thread leftWriter(forward,std::ref(left),std::ref(right),la,ra);
            std::thread rightWriter(forward,std::ref(right),std::ref(left),ra,la);
            struct ForwardingGuard{
                std::atomic<bool>& running;std::thread& left;std::thread& right;
                ~ForwardingGuard(){running=false;left.join();right.join();}
            } forwardingGuard{forwarding,leftWriter,rightWriter};
            auto clock=[] {return std::chrono::steady_clock::now();};
            std::vector<size_t> sizes{1048576,3145728,10485760};if(std::getenv("PEERCHAT_TEST_EXTENDED_SIZE"))sizes.push_back(33554432);
            for(size_t size:sizes){
                auto source=root/("benchmark-"+std::to_string(size)+".bin");Blob expected(size);
                for(size_t i=0;i<size;++i)expected[i]=static_cast<unsigned char>(i*19);
                {std::ofstream f(source,std::ios::binary);f.write(reinterpret_cast<const char*>(expected.data()),size);require(bool(f),"benchmark source write failed");}
                auto destination=root/("received-"+std::to_string(size));std::filesystem::create_directory(destination);
                auto start=clock();auto id=left.fileTransfer(1,b,"",source.wstring());std::string token(id.begin(),id.end());
                auto next=clock(),nextDiagnostics=clock();bool accepted=false,finished=false;auto payload=start;auto cpuStart=processCpuUs();
                while(clock()-start<std::chrono::seconds(120)){
                    poll();require(!forwardFailed,"benchmark relay writer failed");
                    if(observeDiagnostics&&clock()>=nextDiagnostics){nextDiagnostics=clock()+std::chrono::seconds(1);left.fileTransfer(11);right.fileTransfer(11);}
                    if(clock()>=next){next=clock()+std::chrono::milliseconds(250);
                        auto row=right.fileTransfer(0);auto at=row.find(L"\"id\":\""+id+L"\"");
                        if(at!=std::wstring::npos){auto end=row.find(L"}",at);auto item=row.substr(at,end-at);
                            if(!accepted&&item.find(L"\"state\":2")!=std::wstring::npos){right.fileTransfer(2,"",token,destination.wstring());accepted=true;payload=clock();cpuStart=processCpuUs();}
                            if(item.find(L"\"state\":4")!=std::wstring::npos){
                                auto sent=left.fileTransfer(0);auto senderAt=sent.find(L"\"id\":\""+id+L"\"");
                                if(senderAt!=std::wstring::npos&&sent.substr(senderAt,sent.find(L"}",senderAt)-senderAt).find(L"\"state\":4")!=std::wstring::npos){finished=true;break;}
                            }
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                require(finished,"benchmark did not complete");const auto cpuUs=processCpuUs()-cpuStart;
                auto final=destination/(id+L"-"+source.filename().wstring());std::ifstream f(final,std::ios::binary);Blob actual(std::istreambuf_iterator<char>(f),{});require(actual==expected,"benchmark final integrity failed");
                auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(clock()-start).count();auto payloadMs=std::chrono::duration_cast<std::chrono::milliseconds>(clock()-payload).count();
                std::cout<<"HOST_NATIVE_TLS_RELAY fixture_one_way_delay_ms="<<relayDelay<<" diagnostics="<<observeDiagnostics<<" bytes="<<size<<" total_ms="<<elapsed<<" accepted_to_complete_ms="<<payloadMs<<" payload_MiB_per_s="<<(size/1048576.0)/(payloadMs/1000.0)<<" process_cpu_us="<<cpuUs<<" process_peak_working_set_bytes="<<processPeakBytes()<<"\n";
                std::cout<<unicode::encode(left.fileTransfer(11))<<unicode::encode(right.fileTransfer(11));
            }
            // Both files share the real TLS winner; each remains one ACK-gated
            // chunk, with bounded manager fairness rather than independent pipes.
            auto concurrentSource=root/"concurrent.bin";Blob concurrentBytes(1048576,19);
            {std::ofstream file(concurrentSource,std::ios::binary);file.write(reinterpret_cast<const char*>(concurrentBytes.data()),concurrentBytes.size());require(bool(file),"concurrent source write failed");}
            auto concurrentDestination=root/"concurrent-received";std::filesystem::create_directory(concurrentDestination);
            std::vector<std::wstring> ids{left.fileTransfer(1,b,"",concurrentSource.wstring()),left.fileTransfer(1,b,"",concurrentSource.wstring())};
            std::vector<bool> accepted(2,false);auto concurrentStart=clock();bool allComplete=false;
            while(clock()-concurrentStart<std::chrono::seconds(45)){
                poll();auto receiverRows=right.fileTransfer(0);allComplete=true;
                for(size_t n=0;n<ids.size();++n){auto at=receiverRows.find(L"\"id\":\""+ids[n]+L"\"");if(at==std::wstring::npos){allComplete=false;continue;}
                    auto row=receiverRows.substr(at,receiverRows.find(L"}",at)-at);
                    if(!accepted[n]&&row.find(L"\"state\":2")!=std::wstring::npos){right.fileTransfer(2,"",unicode::encode(ids[n]),concurrentDestination.wstring());accepted[n]=true;}
                    if(row.find(L"\"state\":4")==std::wstring::npos)allComplete=false;
                }
                if(allComplete){auto senderRows=left.fileTransfer(0);for(const auto& id:ids){auto at=senderRows.find(L"\"id\":\""+id+L"\"");if(at==std::wstring::npos||senderRows.substr(at,senderRows.find(L"}",at)-at).find(L"\"state\":4")==std::wstring::npos)allComplete=false;}}
                if(allComplete)break;
                require(!forwardFailed,"concurrent relay failure");std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            require(allComplete,"concurrent transfers did not finish fairly");
            for(const auto& id:ids){std::ifstream file(concurrentDestination/(id+L"-concurrent.bin"),std::ios::binary);Blob actual(std::istreambuf_iterator<char>(file),{});require(actual==concurrentBytes,"concurrent integrity failure");}
            std::cout<<"HOST_CONCURRENT_TLS_RELAY files=2 bytes=2097152 total_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(clock()-concurrentStart).count()<<" exact_integrity=1\n";
            return 0; // Join writers before Node destruction, including exceptions.
        }
        auto name=std::wstring(L"binary-") + L"\u65e5\u672c.bin";auto source=root/name;std::vector<char> bytes(2*1024*1024);
        for(size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<char>(i*19);
        {std::ofstream file(source,std::ios::binary);file.write(bytes.data(),bytes.size());require(file.good(),"test source write failed");}
        auto destination=root/L"received";std::filesystem::create_directory(destination);
        auto id=left.fileTransfer(1,b,"",source.wstring());std::string narrow(id.begin(),id.end());
        wait([&]{return right.fileTransfer(0).find(id)!=std::wstring::npos;},"offer not delivered over peer TLS");
        right.fileTransfer(2,"",narrow,destination.wstring());
        wait([&]{return right.fileTransfer(0).find(L"\"done\":32768")!=std::wstring::npos||right.fileTransfer(0).find(L"\"done\":65536")!=std::wstring::npos;},"first bounded chunks did not arrive");
        auto originalA=a,originalB=b;
        left.stop();right.stop();la=ra=0;readyLeft=readyRight=false;a.clear();b.clear();
        require(std::filesystem::exists(destination/(id+L".peerchat-part")),"shutdown deleted resumable partial");
        left.start((root/L"left").wstring(),0,L"File left",false);right.start((root/L"right").wstring(),0,L"File right",false);
        wait([&]{return !a.empty()&&!b.empty();},"nodes did not reopen");
        require(a==originalA&&b==originalB,"restart changed device identity");
        require(left.fileTransfer(0).find(id)!=std::wstring::npos&&right.fileTransfer(0).find(id)!=std::wstring::npos,"restart lost transfer identity");
        left.replaceAuthenticatedFriends(71,2,{{72,b,{b}}});right.replaceAuthenticatedFriends(72,2,{{71,a,{a}}});
        left.setNetworkContext(2,L"file-restart-test");right.setNetworkContext(2,L"file-restart-test");
        left.updateCandidates(b,L"",0,L"",0,true,2,true);right.updateCandidates(a,L"",0,L"",0,true,2,true);
        wait([&]{return la&&ra;},"restart did not demand authenticated route");
        require(left.relayReady(la,a>b)&&right.relayReady(ra,b>a),"restart relay rejected");
        wait([&]{return readyLeft&&readyRight;},"restart peer TLS did not establish");
        auto oldLeft=la,oldRight=ra;left.relayClosed(la,L"file test reconnect");right.relayClosed(ra,L"file test reconnect");
        la=ra=0;readyLeft=readyRight=false;
        wait([&]{return la&&ra&&la!=oldLeft&&ra!=oldRight;},"existing manager failed to replace relay",15);
        require(left.relayReady(la,a>b)&&right.relayReady(ra,b>a),"replacement relay rejected");
        wait([&]{return readyLeft&&readyRight;},"replacement TLS did not establish");
        try{wait([&]{return left.fileTransfer(0).find(L"\"state\":4")!=std::wstring::npos&&right.fileTransfer(0).find(L"\"state\":4")!=std::wstring::npos;},"file did not resume and complete",30);}
        catch(...){std::cerr<<"sender "<<unicode::encode(left.fileTransfer(0))<<"\nreceiver "<<unicode::encode(right.fileTransfer(0))<<"\n";throw;}
        auto final=destination/(id+L"-"+name);std::ifstream file(final,std::ios::binary);std::vector<char> actual((std::istreambuf_iterator<char>(file)),{});
        require(actual==bytes,"completed file bytes mismatch");
        require(!std::filesystem::exists(destination/(id+L".peerchat-part")),"partial leaked after completion");
        auto message=left.send(b,L"Messaging survives file completion");require(!message.empty(),"text enqueue failed");
        bool delivered=false;
        auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(std::chrono::steady_clock::now()<end&&!delivered){
            unsigned char data[65536];int count=left.takeRelayOutput(la,data,sizeof(data),0);if(count>0)right.relayReceive(ra,data,count);
            count=right.takeRelayOutput(ra,data,sizeof(data),0);if(count>0)left.relayReceive(la,data,count);
            right.poll();for(auto& e:left.poll())if(e.kind==NodeEvent::Kind::Delivered)for(auto& m:e.messages)if(m.id==message)delivered=true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        require(delivered,"file transfer disturbed DeliveryManager message/ACK");
        std::wstring snippet=L"Unicode \u65e5\u672c \U0001f642\r\n<script>not executed</script>\n"+std::wstring(5000,L'x');
        require(!left.send(b,std::wstring(protocol::MaxChatCharacters,L'x')).empty(),"at-limit text rejected");
        require(left.send(b,std::wstring(protocol::MaxChatCharacters+1,L'x')).empty(),"oversized inline text accepted");
        auto snippetId=left.fileTransfer(7,b,"",snippet);std::string snippetNarrow(snippetId.begin(),snippetId.end());
        wait([&]{return right.fileTransfer(0).find(snippetId)!=std::wstring::npos;},"snippet offer missing");
        right.fileTransfer(2,"",snippetNarrow,destination.wstring());
        auto snippetPath=destination/(snippetId+L"-message.peerchat-text");
        wait([&]{return std::filesystem::exists(snippetPath);},"snippet not completed");
        std::ifstream snippetFile(snippetPath,std::ios::binary);std::string snippetBytes((std::istreambuf_iterator<char>(snippetFile)),{});
        require(snippetBytes==unicode::encode(snippet),"snippet content normalized or truncated");
        wait([&]{return left.fileTransfer(0).find(L"\"protectedSource\":true")!=std::wstring::npos;},"protected snippet sender did not complete");
        require(left.fileTransfer(9,"",snippetNarrow,L"")==snippet,"protected full preview mismatch");
        auto copy=root/L"explicit-snippet-copy.txt";left.fileTransfer(10,"",snippetNarrow,copy.wstring());
        std::ifstream copyFile(copy,std::ios::binary);std::string copyText((std::istreambuf_iterator<char>(copyFile)),{});require(copyText==snippetBytes,"explicit protected snippet export mismatch");
        auto multibyte=std::wstring(11000,L'\u65e5');auto multiId=left.fileTransfer(7,b,"",multibyte);std::string multiNarrow(multiId.begin(),multiId.end());
        wait([&]{return right.fileTransfer(0).find(multiId)!=std::wstring::npos;},"multibyte snippet offer missing");right.fileTransfer(2,"",multiNarrow,destination.wstring());
        wait([&]{try{return left.fileTransfer(9,"",multiNarrow,L"")==multibyte;}catch(...){return false;}},"multibyte protected snippet did not complete");
        require(left.fileTransfer(8,"",multiNarrow,L"")==std::wstring(10922,L'\u65e5'),"preview broke UTF-8 at 32 KiB boundary");
        bool missingDenied=false;try{left.fileTransfer(8,"",Storage::newMessageId(),L"");}catch(...){missingDenied=true;}require(missingDenied,"missing protected source did not fail closed");pump();require(readyLeft&&readyRight,"local preview failure retired the peer session");
        left.stop();right.stop();
        left.start((root/L"left").wstring(),0,L"File left",false);right.start((root/L"right").wstring(),0,L"File right",false);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        require(left.fileTransfer(0).find(snippetId)!=std::wstring::npos&&right.fileTransfer(0).find(snippetId)!=std::wstring::npos,"completed snippet lost after reopen");
        require(right.fileTransfer(0).find(snippetPath.filename().wstring())!=std::wstring::npos,"completed local path lost after reopen");
        require(left.fileTransfer(9,"",snippetNarrow,L"")==snippet,"protected snippet could not reopen after profile restart");
        left.stop();right.stop();
        std::cout<<"Native file session: unverified rejection / mutual peer TLS / opaque relay / Unicode binary / profile close-reopen resume / reconnect resume / final hash / message ACK coexistence PASS\n";
    }catch(const std::exception& error){left.stop();right.stop();std::cerr<<error.what()<<"\n";return 1;}
}

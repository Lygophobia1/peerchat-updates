#define NOMINMAX
#include "../windows_transfer_io.hpp"
#include <fstream>
#include <iostream>

using namespace peerchat;
using namespace peerchat::transfer;

static void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F> static void rejected(F action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed,"unsafe or conflicting receive accepted");}
static Blob read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return Blob(std::istreambuf_iterator<char>(file),{});}
static void write(const std::filesystem::path& path,const Blob& data){std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(data.data()),data.size());check(bool(file),"fixture write failed");}

int main(int argc,char** argv){try{
    check(argc==2,"fixture root required");auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);
    auto first=Metadata{std::string(32,'a'),"PeerChat.rar",4,{}};
    auto second=Metadata{std::string(32,'b'),"PeerChat.rar",4,{}};
    auto firstFolder=root/(first.id+".peerchat-received"),secondFolder=root/(second.id+".peerchat-received");
    Blob payload{'r','a','r','!'};
    DurableState state;state.metadata=first;state.done=4;
    {
        windows::FileSink sink(root.wstring(),first,nullptr,true,true);
        check(std::filesystem::path(sink.path())==firstFolder/L"PeerChat.rar","original name was altered");
        sink.append(payload);sink.flush();state.location=sink.resumeToken();
    }
    check(std::filesystem::exists(firstFolder/(first.id+".peerchat-part")),"durable partial was lost");
    {
        auto resumed=windows::FileSink::reopen(state);resumed->commit();
        check(read(firstFolder/L"PeerChat.rar")==payload,"resumed file changed");
    }
    {
        auto completed=windows::FileSink::reopen(state,true);
        check(completed->path()==(firstFolder/L"PeerChat.rar").wstring(),"completed history path changed");
    }
    rejected([&]{windows::FileSink duplicate(root.wstring(),first,nullptr,true,true);});
    check(read(firstFolder/L"PeerChat.rar")==payload,"duplicate touched completed file");
    {
        windows::FileSink sink(root.wstring(),second,nullptr,true,true);sink.append(payload);sink.commit();
        check(std::filesystem::path(sink.path())==secondFolder/L"PeerChat.rar"&&read(secondFolder/L"PeerChat.rar")==payload,"same basename collided");
    }
    auto legacy=Metadata{std::string(32,'c'),"old.bin",4,{}};
    DurableState oldState;oldState.metadata=legacy;oldState.done=4;
    {
        windows::FileSink sink(root.wstring(),legacy,nullptr,true);sink.append(payload);oldState.location=sink.resumeToken();sink.commit();
        check(std::filesystem::path(sink.path())==root/(legacy.id+"-old.bin"),"legacy cache path changed");
    }
    check(windows::FileSink::reopen(oldState,true)->path()==(root/(legacy.id+"-old.bin")).wstring(),"old token failed to reopen");
    for(auto name:{"../escape.rar","CON.rar","folder\\escape.rar","trailing. "}){
        Metadata invalid{std::string(32,'d'),name,4,{}};rejected([&]{windows::FileSink sink(root.wstring(),invalid,nullptr,true,true);});
    }
    auto tampered=state;tampered.metadata.name="changed.rar";rejected([&]{windows::FileSink::reopen(tampered,true);});
    auto raced=Metadata{std::string(32,'f'),"race.rar",4,{}};
    auto racedFolder=root/(raced.id+".peerchat-received");
    {
        windows::FileSink sink(root.wstring(),raced,nullptr,true,true);sink.append(payload);
        write(racedFolder/L"race.rar",Blob{'o','l','d'});
        rejected([&]{sink.commit();});
        check(read(racedFolder/L"race.rar")==Blob({'o','l','d'}),"commit overwrote a competing file");
    }
    auto other=Metadata{std::string(32,'e'),"existing.rar",4,{}};auto conflict=root/(other.id+".peerchat-received");std::filesystem::create_directory(conflict);
    write(conflict/L"existing.rar",payload);rejected([&]{windows::FileSink sink(root.wstring(),other,nullptr,true,true);});
    check(read(conflict/L"existing.rar")==payload,"conflict overwrote existing content");
    std::cout<<"PASS DM original basename, duplicate isolation, durable reopen, legacy token, traversal/reserved-name rejection and no overwrite\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

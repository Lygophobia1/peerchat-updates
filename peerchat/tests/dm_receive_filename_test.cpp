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
    auto selected=Metadata{std::string(32,'1'),"cat.png",4,{}};
    auto selectedPath=root/L"cat2.png";
    DurableState chosen;chosen.metadata=selected;chosen.done=4;
    {
        windows::FileSink sink(root.wstring(),selected,nullptr,true,false,"cat2.png");
        check(sink.path()==selectedPath.wstring(),"chooser basename was not honored");
        sink.append(payload);sink.flush();chosen.location=sink.resumeToken();
    }
    check(std::filesystem::exists(root/(selected.id+".peerchat-part"))&&!std::filesystem::exists(selectedPath),"selected partial lost or prematurely committed");
    {auto resumed=windows::FileSink::reopen(chosen);check(resumed->path()==selectedPath.wstring(),"selected partial reopened at wrong path");resumed->commit();}
    check(read(selectedPath)==payload&&windows::FileSink::reopen(chosen,true)->path()==selectedPath.wstring(),"selected completed file did not survive restart");
    rejected([&]{windows::FileSink sink(root.wstring(),Metadata{std::string(32,'2'),"other.png",4,{}},nullptr,true,false,"cat2.png");});
    check(read(selectedPath)==payload,"existing selected file overwritten");
    auto changedOriginal=chosen;changedOriginal.metadata.name="other.png";rejected([&]{windows::FileSink::reopen(changedOriginal,true);});
    auto changedId=chosen;changedId.metadata.id=std::string(32,'2');rejected([&]{windows::FileSink::reopen(changedId,true);});
    for(auto name:{"../cat2.png","CON.png","folder\\cat2.png","trailing. ","NUL","cat2.png/"})
        rejected([&]{windows::FileSink sink(root.wstring(),Metadata{std::string(32,'3'),"cat.png",4,{}},nullptr,true,false,name);});
    rejected([&]{windows::FileSink sink(root.wstring(),Metadata{std::string(32,'3'),"cat.png",4,{}},nullptr,true,false,(std::string(32,'3')+".PEERCHAT-PART"));});
    auto selectedRace=Metadata{std::string(32,'4'),"cat.png",4,{}};
    {windows::FileSink sink(root.wstring(),selectedRace,nullptr,true,false,"late.png");sink.append(payload);write(root/L"late.png",Blob{'o','l','d'});rejected([&]{sink.commit();});check(read(root/L"late.png")==Blob({'o','l','d'}),"selected commit overwrote race winner");}
    auto partialCollision=Metadata{std::string(32,'5'),"cat.png",4,{}};
    write(root/(partialCollision.id+".peerchat-part"),payload);rejected([&]{windows::FileSink sink(root.wstring(),partialCollision,nullptr,true,false,"fresh.png");});
    check(read(root/(partialCollision.id+".peerchat-part"))==payload,"partial collision altered existing file");
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
    std::cout<<"PASS DM original basename and selected exact basename, duplicate isolation, durable reopen, legacy token, unsafe-name rejection and no overwrite\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

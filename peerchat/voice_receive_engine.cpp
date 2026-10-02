#include "voice_receive_engine.hpp"
#include <algorithm>
#include <cstring>

namespace peerchat {
namespace {
std::uint32_t be32(const unsigned char* b){return (std::uint32_t(b[0])<<24)|(std::uint32_t(b[1])<<16)|(std::uint32_t(b[2])<<8)|b[3];}
template<class T> T symbol(HMODULE module,const char* name){
    auto address=GetProcAddress(module,name);T target=nullptr;
    static_assert(sizeof(target)==sizeof(address),"Windows function pointer width");
    std::memcpy(&target,&address,sizeof(target));return target;
}
}
VoiceReceiveEngine::~VoiceReceiveEngine(){close();}
bool VoiceReceiveEngine::open(const std::wstring& path){
    std::lock_guard<std::mutex> guard(mutex_);closeLocked();counters_={};busyPullBlocks_=0;
    if(path.size()<3||!(path[1]==L':'||(path[0]==L'\\'&&path[1]==L'\\')))return false;
    module_=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module_){counters_.lastError=PC_RX_FAILED;return false;}
#define BIND(member,type,name) member=symbol<type>(module_,name);if(!member){counters_.lastError=PC_RX_FAILED;closeLocked();return false;}
    BIND(create_,Create,"pc_rx_create") BIND(destroy_,Destroy,"pc_rx_destroy")
    BIND(add_,Add,"pc_rx_source_add") BIND(remove_,Remove,"pc_rx_source_remove")
    BIND(insert_,Insert,"pc_rx_insert") BIND(pull_,Pull,"pc_rx_pull_10ms") BIND(stats_,Stats,"pc_rx_get_source_stats")
#undef BIND
    pc_rx_config config{sizeof(config),PC_RX_DEVICE_PACED,32,100,200};
    counters_.lastError=create_(&config,&engine_);
    if(counters_.lastError!=PC_RX_OK){closeLocked();return false;}return true;
}
void VoiceReceiveEngine::closeLocked(){
    if(engine_&&destroy_)destroy_(engine_);
    engine_=0;sources_.clear();if(module_)FreeLibrary(module_);module_=nullptr;
}
void VoiceReceiveEngine::close(){std::lock_guard<std::mutex> guard(mutex_);closeLocked();}
bool VoiceReceiveEngine::authorize(const std::string& key,std::uint64_t generation){
    std::lock_guard<std::mutex> guard(mutex_);
    if(!engine_||key.empty()||key.size()>256||!generation)return false;
    auto found=sources_.find(key);
    if(found!=sources_.end()){
        if(generation==found->second.generation)return true;
        if(generation<found->second.generation)return false;
        if(found->second.handle){int status=remove_(engine_,found->second.handle);if(status!=PC_RX_OK){counters_.lastError=status;return false;}}
        found->second={generation,0,0};return true;
    }
    if(sources_.size()>=32)return false;
    sources_.emplace(key,Source{generation,0,0});return true;
}
void VoiceReceiveEngine::retire(const std::string& key){
    std::lock_guard<std::mutex> guard(mutex_);auto found=sources_.find(key);if(found==sources_.end())return;
    if(found->second.handle){int status=remove_(engine_,found->second.handle);if(status!=PC_RX_OK){counters_.lastError=status;return;}}
    sources_.erase(found);
}
int VoiceReceiveEngine::insertRtp(const std::string& key,std::uint64_t generation,const unsigned char* bytes,std::size_t count){
    std::unique_lock<std::mutex> guard(mutex_,std::try_to_lock);if(!guard.owns_lock())return PC_RX_BUSY;
    auto reject=[this](int status){++counters_.rejectedPackets;counters_.lastError=status;return status;};
    auto found=sources_.find(key);
    if(!engine_||found==sources_.end()||generation!=found->second.generation||!bytes||count<12||count>8192||(bytes[0]>>6)!=2)return reject(PC_RX_INVALID);
    std::size_t offset=12+4*(bytes[0]&15);if(offset>count)return reject(PC_RX_INVALID);
    if(bytes[0]&16){if(count-offset<4)return reject(PC_RX_INVALID);std::size_t extension=4+4*((unsigned(bytes[offset+2])<<8)|bytes[offset+3]);if(extension>count-offset)return reject(PC_RX_INVALID);offset+=extension;}
    std::size_t end=count;if(bytes[0]&32){std::size_t padding=bytes[count-1];if(!padding||padding>count-offset)return reject(PC_RX_INVALID);end-=padding;}
    const auto ssrc=be32(bytes+8);const unsigned pt=bytes[1]&127;
    if(end<=offset||end-offset>4096||!ssrc||pt!=111)return reject(PC_RX_INVALID);
    auto& source=found->second;
    if(!source.handle){
        pc_rx_source_config config{sizeof(config),1,ssrc,pt,generation};
        int status=add_(engine_,&config,&source.handle);if(status!=PC_RX_OK)return reject(status);source.ssrc=ssrc;
    }else if(source.ssrc!=ssrc)return reject(PC_RX_INVALID);
    pc_rx_packet packet{sizeof(packet),1,ssrc,be32(bytes+4),std::uint32_t((unsigned(bytes[2])<<8)|bytes[3]),pt,generation,0};
    int status=insert_(engine_,source.handle,&packet,bytes+offset,static_cast<std::uint32_t>(end-offset));
    if(status!=PC_RX_OK)return reject(status);
    ++counters_.acceptedPackets;return PC_RX_OK;
}
bool VoiceReceiveEngine::pull20ms(std::int16_t* pcm,std::size_t samples,bool deafened){
    if(!pcm||samples!=320)return false;
    std::fill_n(pcm,320,0);
    std::unique_lock<std::mutex> guard(mutex_,std::try_to_lock);if(!guard.owns_lock()){busyPullBlocks_+=2;return false;}
    if(!engine_){counters_.underrunBlocks+=2;return false;}
    bool success=true;
    for(unsigned block=0;block<2;++block){int status=pull_(engine_,0,pcm+block*160,160);
        if(status==PC_RX_OK)++counters_.mixedBlocks;else{++counters_.underrunBlocks;counters_.lastError=status;std::fill_n(pcm+block*160,160,0);success=false;}
    }
    // Keep NetEq advancing while deafened; do not replay queued audio on undeafen.
    if(deafened)std::fill_n(pcm,320,0);
    return success;
}
VoiceReceiveEngine::Snapshot VoiceReceiveEngine::snapshot(){
    std::lock_guard<std::mutex> guard(mutex_);auto result=counters_;
    result.underrunBlocks+=busyPullBlocks_.load();
    result.authorizedSources=static_cast<unsigned>(sources_.size());
    for(const auto& item:sources_)if(item.second.handle){++result.activeSources;pc_rx_source_stats value{};value.size=sizeof(value);value.version=1;
        if(stats_(engine_,item.second.handle,&value)==PC_RX_OK)result.sources.emplace(item.first,value);
    }
    return result;
}
}

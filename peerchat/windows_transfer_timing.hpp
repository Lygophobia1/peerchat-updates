#pragma once
#include "transfer_manager.hpp"

namespace peerchat::transfer::windows {
// Windows adapter observations only. No journal fields, wire changes, remote
// clock assumptions or Android lifecycle changes. One bounded row per transfer.
class TransferTiming {
    struct Row {
        std::uint64_t route=0,expected=0,sentAt=0,ackAt=0,receivedAt=0,replyOffset=0;
        std::uint64_t ackCount=0,ackUs=0,ackMaxUs=0,idleCount=0,idleUs=0;
        std::uint64_t receiveCount=0,receiveUs=0,replyCount=0,replyUs=0,sendUs=0;
        bool pending=false,replyPending=false;
    };
    std::map<std::string,Row> rows_;
public:
    static std::uint64_t now(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
    void clear(){rows_.clear();}
    void sent(const Blob& bytes,std::uint64_t route,std::uint64_t began,std::uint64_t ended)noexcept{try{
        protocol::Reader r(bytes);if(r.type()!=protocol::FileTransfer||r.number()!=1)return;
        auto kind=static_cast<Kind>(r.number());auto id=r.string();
        if(!rows_.count(id)&&rows_.size()>=MaxRecords)return;
        auto& row=rows_[id];
        if(row.route!=route||kind==Kind::Offer){row.pending=false;row.ackAt=0;row.replyPending=false;row.route=route;}
        row.sendUs+=ended-began;
        if(kind==Kind::Chunk){auto offset=r.number();auto data=r.bytes();
            if(row.ackAt&&began>=row.ackAt){row.idleUs+=began-row.ackAt;++row.idleCount;}
            row.ackAt=0;row.expected=offset+data.size();row.sentAt=ended;row.pending=true;
        }else if(kind==Kind::Resume){auto offset=r.number();
            if(row.replyPending&&offset==row.replyOffset&&ended>=row.receivedAt){row.replyUs+=ended-row.receivedAt;++row.replyCount;row.replyPending=false;}
        }
    }catch(...){} }
    // Called only after authenticated winning-route dispatch and manager validation.
    void received(const Blob& bytes,std::uint64_t route,std::uint64_t began,std::uint64_t ended)noexcept{try{
        protocol::Reader r(bytes);if(r.type()!=protocol::FileTransfer||r.number()!=1)return;
        auto kind=static_cast<Kind>(r.number());auto id=r.string();
        if(!rows_.count(id)&&rows_.size()>=MaxRecords)return;
        auto& row=rows_[id];
        if(kind==Kind::Resume){auto offset=r.number();
            if(row.pending&&row.route==route&&offset==row.expected&&began>=row.sentAt){auto wait=began-row.sentAt;row.ackUs+=wait;row.ackMaxUs=std::max(row.ackMaxUs,wait);++row.ackCount;row.pending=false;row.ackAt=began;}
        }else if(kind==Kind::Chunk){auto offset=r.number();auto data=r.bytes();
            row.receiveUs+=ended-began;++row.receiveCount;row.receivedAt=began;row.replyOffset=offset+data.size();row.replyPending=true;row.route=route;
        }
    }catch(...){} }
    std::string diagnostics(const std::string& id)const{
        auto i=rows_.find(id);if(i==rows_.end())return {};const auto& r=i->second;
        return " ack_samples="+std::to_string(r.ackCount)+" ack_turnaround_us="+std::to_string(r.ackUs)+" ack_turnaround_max_us="+std::to_string(r.ackMaxUs)+
            " sender_idle_samples="+std::to_string(r.idleCount)+" sender_ack_to_send_us="+std::to_string(r.idleUs)+
            " receiver_samples="+std::to_string(r.receiveCount)+" receiver_dispatch_us="+std::to_string(r.receiveUs)+
            " receiver_ack_samples="+std::to_string(r.replyCount)+" receiver_dispatch_to_ack_us="+std::to_string(r.replyUs)+
            " tls_submit_us="+std::to_string(r.sendUs);
    }
};
}

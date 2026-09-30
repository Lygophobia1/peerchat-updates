#pragma once
#include "protocol.hpp"
#include <utility>

namespace peerchat::group_keys {
inline Blob offerPacket(const GroupKeyHandoff& offer){
    protocol::check(protocol::id(offer.id,32)&&protocol::id(offer.groupId,32)&&
        protocol::id(offer.serverGroupId,32)&&protocol::id(offer.distributorPeerId)&&
        protocol::id(offer.recipientPeerId)&&offer.rosterGeneration>0&&
        offer.keyGeneration==offer.rosterGeneration&&offer.contentKey.size()==32);
    protocol::Writer writer(protocol::GroupKeyOffer);
    writer.string(offer.id);writer.string(offer.groupId);writer.string(offer.serverGroupId);
    writer.number(offer.rosterGeneration);writer.number(offer.keyGeneration);
    writer.string(offer.distributorPeerId);writer.string(offer.recipientPeerId);
    writer.bytes(offer.contentKey);
    Blob frame=std::move(writer.data);
    return frame;
}
inline GroupKeyHandoff readOffer(const Blob& frame){
    protocol::Reader reader(frame);protocol::check(reader.type()==protocol::GroupKeyOffer);
    GroupKeyHandoff offer;offer.id=reader.string();offer.groupId=reader.string();
    offer.serverGroupId=reader.string();offer.rosterGeneration=reader.number();
    offer.keyGeneration=reader.number();offer.distributorPeerId=reader.string();
    offer.recipientPeerId=reader.string();offer.contentKey=reader.bytes();reader.end();
    protocol::check(protocol::id(offer.id,32)&&protocol::id(offer.groupId,32)&&
        protocol::id(offer.serverGroupId,32)&&protocol::id(offer.distributorPeerId)&&
        protocol::id(offer.recipientPeerId)&&offer.rosterGeneration>0&&
        offer.keyGeneration==offer.rosterGeneration&&offer.contentKey.size()==32);
    return offer;
}
struct Ack {std::string handoffId,serverGroupId;std::uint64_t rosterGeneration=0,keyGeneration=0;};
inline Blob ackPacket(const Ack& ack){
    protocol::check(protocol::id(ack.handoffId,32)&&protocol::id(ack.serverGroupId,32)&&
        ack.rosterGeneration>0&&ack.keyGeneration==ack.rosterGeneration);
    protocol::Writer writer(protocol::GroupKeyAck);writer.string(ack.handoffId);
    writer.string(ack.serverGroupId);writer.number(ack.rosterGeneration);
    writer.number(ack.keyGeneration);return std::move(writer.data);
}
inline Ack readAck(const Blob& frame){
    protocol::Reader reader(frame);protocol::check(reader.type()==protocol::GroupKeyAck);
    Ack ack;ack.handoffId=reader.string();ack.serverGroupId=reader.string();
    ack.rosterGeneration=reader.number();ack.keyGeneration=reader.number();reader.end();
    protocol::check(protocol::id(ack.handoffId,32)&&protocol::id(ack.serverGroupId,32)&&
        ack.rosterGeneration>0&&ack.keyGeneration==ack.rosterGeneration);
    return ack;
}
}

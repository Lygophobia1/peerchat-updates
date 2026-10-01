#define RTC_STATIC
#include <rtc/rtc.hpp>
#include <rtc/h264rtppacketizer.hpp>
#include <rtc/rtcpreceivingsession.hpp>
#include <rtc/plihandler.hpp>
#include "h264_rtp_receiver.hpp"
#include <iostream>
#include <stdexcept>

void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        auto config = std::make_shared<rtc::RtpPacketizationConfig>(1234, "screen-test", 102, 90000);
        config->sequenceNumber = 65530; config->timestamp = 9000;
        rtc::H264RtpPacketizer packetizer(rtc::NalUnit::Separator::StartSequence, config, 1200);
        rtc::RtcpReceivingSession rtcp;
        unsigned pli = 0;
        rtc::PliHandler senderPli([&] { ++pli; });
        peerchat::H264RtpReceiver receiver;
        std::vector<peerchat::H264RtpReceiver::AccessUnit> accepted;
        const auto submit = [&](const peerchat::H264RtpReceiver::AccessUnit& au) { accepted.push_back(au); return true; };
        const rtc::message_callback noSend = [](rtc::message_ptr) {};
        auto packetize = [&](std::uint8_t nal, bool bootstrap) {
            std::vector<std::uint8_t> bytes;
            if (bootstrap) bytes = {0,0,0,1,0x67,0x11,0,0,0,1,0x68,0x22};
            bytes.insert(bytes.end(), {0,0,0,1,nal});
            // No Annex B emulation sequences in this synthetic payload.
            for (unsigned i=0; i<6000; ++i) bytes.push_back(static_cast<std::uint8_t>(1+i%250));
            rtc::message_vector messages{rtc::make_message(reinterpret_cast<const std::byte*>(bytes.data()),
                reinterpret_cast<const std::byte*>(bytes.data()+bytes.size()))};
            packetizer.outgoing(messages, noSend);
            config->timestamp += 3000;
            return std::make_pair(std::move(messages), std::move(bytes));
        };
        auto receive = [&](rtc::message_ptr packet, std::uint64_t now) {
            rtc::message_vector messages{packet}; rtcp.incoming(messages, noSend);
            require(messages.size()==1 && packet->size()>12, "receiving session lost valid RTP");
            const auto* b = reinterpret_cast<const std::uint8_t*>(packet->data());
            require((b[0]&31)==0 && (b[1]&127)==102, "unexpected RTP header in test");
            peerchat::H264RtpReceiver::Packet p;
            p.sequence=static_cast<std::uint16_t>((unsigned(b[2])<<8)|b[3]);
            p.timestamp=(unsigned(b[4])<<24)|(unsigned(b[5])<<16)|(unsigned(b[6])<<8)|b[7];
            p.marker=(b[1]&128)!=0; p.payload.assign(b+12,b+packet->size());
            receiver.receive(std::move(p),now,submit);
        };
        auto first=packetize(0x65,true);
        require(first.first.size()>5, "pinned packetizer did not fragment IDR");
        std::swap(first.first[3],first.first[4]);
        for(auto& p:first.first) receive(p,0);
        require(accepted.size()==1 && !receiver.needsKeyframe(), "pinned RTP reorder/bootstrap failed");
        // Bootstrap may prefix cached SPS/PPS. The original SPS/PPS/IDR must
        // still survive packetization/reassembly byte-for-byte at the tail.
        require(accepted[0].bytes.size()>=first.second.size() &&
            std::equal(first.second.rbegin(),first.second.rend(),accepted[0].bytes.rbegin()), "pinned packetizer changed NAL bytes");
        auto lost=packetize(0x61,false);
        for(std::size_t i=0;i<lost.first.size();++i) if(i!=2) receive(lost.first[i],1);
        receiver.tick(22,submit);
        require(accepted.size()==1 && receiver.needsKeyframe(), "pinned fragmented damaged AU reached decoder");
        rtc::message_vector feedback;
        require(rtcp.requestKeyframe([&](rtc::message_ptr m){feedback.push_back(m);}), "PLI request failed");
        senderPli.incoming(feedback,noSend);
        require(pli==1, "pinned RTCP PLI did not reach sender handler");
        auto recovery=packetize(0x65,true);
        for(auto& p:recovery.first) receive(p,23);
        require(accepted.size()==2 && !receiver.needsKeyframe(), "pinned packetizer recovery IDR rejected");
        std::cout << "PASS: pinned libdatachannel H.264 byte-exact FU-A reorder/wrap/loss/IDR recovery and RTCP PLI handler.\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}

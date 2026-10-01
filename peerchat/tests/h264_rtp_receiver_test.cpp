#include "h264_rtp_receiver.hpp"
#include <iostream>
#include <stdexcept>

using Receiver = peerchat::H264RtpReceiver;
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
struct Test {
    Receiver receiver;
    std::vector<Receiver::AccessUnit> delivered;
    bool accept = true;
    Receiver::Submit submit = [this](const Receiver::AccessUnit& au) {
        if (accept) delivered.push_back(au);
        return accept;
    };
    void packet(unsigned seq, unsigned ts, bool marker, std::vector<std::uint8_t> payload,
                std::uint64_t now = 0) {
        receiver.receive({static_cast<std::uint16_t>(seq), ts, marker, std::move(payload), 0, false}, now, submit);
    }
    void bootstrap(unsigned seq = 0, unsigned ts = 100) {
        // STAP-A with SPS/PPS/IDR, each containing safe synthetic payload bytes.
        packet(seq, ts, true, {24,0,2,0x67,0x11,0,2,0x68,0x22,0,2,0x65,0x33});
        require(delivered.size() == 1 && delivered.back().recovery, "initial clean bootstrap not delivered");
    }
    void expire() { receiver.tick(21, submit); }
};
int main() {
    try {
        { Test t; t.bootstrap();
          t.packet(1,200,false,{0x7c,0x81,0x10});
          t.packet(3,200,true,{0x7c,0x41,0x30},1);
          t.packet(2,200,false,{0x7c,0x01,0x20},2);
          require(t.delivered.size()==2, "modest reorder lost complete FU-A");
          require(t.delivered.back().bytes == std::vector<std::uint8_t>({0,0,0,1,0x61,0x10,0x20,0x30}), "FU-A reconstruction not byte exact");
          require(t.receiver.stats().sequenceLoss==0 && t.receiver.stats().reorderedRecovered==1, "reorder miscounted as loss"); }
        { Test t; t.bootstrap();
          t.packet(1,200,false,{0x61,0x10}); // missing seq 2, an entire NAL
          t.packet(3,200,true,{0x61,0x30}); t.expire();
          require(t.delivered.size()==1 && t.receiver.needsKeyframe(), "incomplete single-NAL AU reached decoder");
          t.packet(4,300,true,{0x61,0x40},22);
          require(t.delivered.size()==1, "dependent delta frame delivered after loss");
          t.packet(5,400,true,{0x65,0x50},23);
          require(t.delivered.size()==2 && !t.receiver.needsKeyframe(), "IDR did not restore decoding");
          require(t.receiver.stats().sequenceLoss==1 && t.receiver.stats().recoveryAccepted==2, "loss/recovery counters wrong"); }
        { Test t; t.bootstrap();
          t.packet(1,200,false,{0x7c,0x81,0x10});
          t.packet(3,200,true,{0x7c,0x41,0x30}); t.expire();
          require(t.delivered.size()==1 && t.receiver.stats().incompleteFu==1, "missing middle fragment accepted");
          t.packet(2,200,false,{0x7c,0x01,0x20},22);
          t.packet(4,300,true,{0x65,0x40},23);
          require(t.delivered.size()==2 && t.delivered.back().timestamp==300, "late abandoned fragment contaminated recovery");
          require(t.receiver.stats().late==1, "late packet not counted"); }
        { Test t; t.bootstrap();
          t.packet(1,200,false,{0x7c,0x81,0x10});
          t.packet(2,200,false,{0x7c,0x01,0x20});
          t.packet(4,300,true,{0x65,0x40}); t.expire();
          require(t.delivered.size()==1 && t.receiver.stats().incompleteFu==1, "missing FU-A end or uncertain next AU accepted");
          t.packet(5,400,true,{0x65,0x50},22);
          require(t.delivered.size()==2, "fresh IDR after missing FU-A end did not recover"); }
        { Test t; t.bootstrap();
          t.packet(1,200,false,{0x7c,0x81,0x10});
          t.packet(2,200,true,{0x7c,0x01,0x20});
          require(t.delivered.size()==1 && t.receiver.stats().incompleteFu==1, "marker with unfinished FU-A accepted");
          t.packet(3,200,true,{0x61,0x30});
          require(t.delivered.size()==1, "tail of abandoned timestamp was revived"); }
        { Test t; t.bootstrap(65533);
          t.packet(65534,200,false,{0x7c,0x81,0x10});
          t.packet(0,200,true,{0x7c,0x41,0x30});
          t.packet(65535,200,false,{0x7c,0x01,0x20});
          require(t.delivered.size()==2 && t.receiver.stats().sequenceLoss==0, "wrap reorder broken"); }
        { Test t; t.bootstrap(65533);
          t.packet(65534,200,false,{0x7c,0x81,0x10});
          t.packet(0,200,true,{0x7c,0x41,0x30});
          t.packet(1,300,true,{0x65,0x40}); t.expire();
          require(t.delivered.size()==2 && t.receiver.stats().sequenceLoss==1, "wrap loss selected wrong buffered sequence"); }
        { Test t; t.bootstrap();
          t.packet(1,200,false,{0x61,0x10});
          t.packet(2,300,true,{0x65,0x20});
          require(t.delivered.size()==2 && t.delivered.back().timestamp==300, "timestamps mixed in one sample");
          require(t.receiver.stats().timestampBeforeMarker==1, "missing marker unrecorded"); }
        { Test t; t.bootstrap();
          t.packet(2,200,true,{0x61,0x20}); t.receiver.tick(19,t.submit);
          require(t.receiver.stats().sequenceLoss==0, "loss declared before reorder deadline");
          t.expire(); require(t.delivered.size()==1 && t.receiver.stats().sequenceLoss==1, "missing prefix admitted");
          t.packet(3,300,true,{0x65,0x30},22);
          require(t.delivered.size()==2, "loss before IDR prevents later clean recovery"); }
        { Test t; t.bootstrap();
          t.packet(1,200,false,{0x7c,0x81,0x10});
          t.packet(34,200,true,{0x7c,0x41,0x30});
          require(t.receiver.stats().sequenceLoss==32 && t.delivered.size()==1, "packet window not bounded"); }
        { Test t; t.bootstrap(); t.accept=false;
          t.packet(1,200,true,{0x61,0x10}); t.accept=true;
          t.packet(2,300,true,{0x61,0x20});
          require(t.delivered.size()==1 && t.receiver.stats().decoderQueueRefused==1, "decoder queue drop failed to damage references");
          t.packet(3,400,true,{0x65,0x30}); require(t.delivered.size()==2, "queue refusal recovery failed"); }
        { Test t;
          t.packet(0,100,true,{0x65,0x10}); require(t.delivered.empty(), "IDR without parameter sets bootstrapped");
          t.packet(1,200,true,{24,0,2,0x67,0x11,0,2,0x68,0x22});
          t.packet(2,300,true,{0x65,0x30}); require(t.delivered.size()==1, "valid standalone SPS/PPS not retained"); }
        { Test t; t.bootstrap();
          t.packet(1,200,true,{24,0,2,0x65,0x11,0,2,0x61,0x22});
          require(t.delivered.size()==1 && t.receiver.needsKeyframe(), "mixed IDR/delta AU accepted");
          t.packet(2,300,true,{0x7c,0xc5,0x30});
          require(t.delivered.size()==1, "illegal FU-A start+end accepted");
          t.receiver.reset(); require(t.receiver.stats().sequenceLoss==0 && t.receiver.needsKeyframe(), "generation reset retained old continuity"); }
        { Test t; t.bootstrap();
          t.packet(1,200,false,{0x7c,0x81,0x10});
          t.packet(3,200,true,{0x7c,0x41,0x30});
          t.packet(3,200,true,{0x7c,0x41,0x30});
          t.packet(2,200,false,{0x7c,0x01,0x20});
          t.packet(4,100,true,{0x65,0x40});
          require(t.delivered.size()==2 && t.receiver.stats().duplicate==1 && t.receiver.stats().late==1, "duplicate/old timestamp polluted newer AU"); }
        { Test t; t.bootstrap();
          t.packet(1,200,false,{0x7c,0x81,0x10});
          t.packet(2,200,true,{0x7c,0x45,0x20});
          require(t.delivered.size()==1 && t.receiver.needsKeyframe(), "FU-A header mismatch accepted"); }
        { peerchat::H264SenderContinuity sender;
          require(!sender.allow(1,false) && sender.allow(2,true) && sender.allow(3,false), "initial sender bootstrap broken");
          require(!sender.allow(5,false) && !sender.allow(6,false), "encoded ordinal gap did not guard references");
          require(sender.allow(7,true) && sender.allow(8,false), "fresh bootstrap did not restore send");
          sender.sendFailed(); require(!sender.allow(9,false), "send failure reference recovery failed");
          require(sender.allow(10,false,true) && sender.waiting() && !sender.allow(11,false), "codec config mistaken for IDR");
          require(sender.allow(12,true), "IDR after separate codec configuration rejected"); }
        std::cout << "PASS: 16 deterministic RTP/H.264 reorder, loss, FU-A, wrap, bootstrap, queue-pressure and IDR recovery cases.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}

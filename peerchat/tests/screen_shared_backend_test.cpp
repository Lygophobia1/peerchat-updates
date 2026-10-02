#include "screen_media_backend.hpp"
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
struct Mechanics {
    inline static unsigned captureOpens=0, captureCloses=0, pops=0, keyframes=0;
    inline static unsigned decoderOpens=0, decoderCloses=0, pushes=0;
    inline static bool refuse=false;
    bool open() { return true; }
    void close() {}
    pc_screen_encoder* encoderOpen(const pc_screen_encoder_options*) {
        ++captureOpens; return reinterpret_cast<pc_screen_encoder*>(1);
    }
    void encoderClose(pc_screen_encoder*) { ++captureCloses; }
    int encoderPop(pc_screen_encoder*, std::uint8_t* out, std::uint32_t size,
                   pc_screen_encoded_frame* info) {
        ++pops;
        const std::uint8_t au[]={0,0,0,1,0x65,0x33};
        info->length=sizeof(au); info->idr=1; info->encoded_sequence=pops;
        if(size<sizeof(au)) return -1;
        std::memcpy(out,au,sizeof(au)); return 1;
    }
    int encoderRequestKeyframe(pc_screen_encoder*) { ++keyframes; return 1; }
    int encoderStatus(pc_screen_encoder*,pc_screen_encoder_status*) const { return 1; }
    pc_screen_decoder* decoderOpen(const pc_screen_decoder_options*) {
        ++decoderOpens; return reinterpret_cast<pc_screen_decoder*>(1);
    }
    void decoderClose(pc_screen_decoder*) { ++decoderCloses; }
    int decoderStatus(pc_screen_decoder*,pc_screen_decoder_status*) const { return 1; }
    int decoderPush(pc_screen_decoder*,const std::uint8_t* au,std::uint32_t size,std::uint64_t) {
        require(size>=6 && au[0]==0 && au[3]==1,"decoder input is not Annex B");
        ++pushes; return refuse?0:1;
    }
};
}

int main() {
    try {
        using Source=peerchat::BasicScreenCaptureEncodeBackend<Mechanics>;
        using Receiver=peerchat::BasicScreenReceiveDecodeBackend<Mechanics>;
        static_assert(!std::is_copy_constructible_v<Source>);
        static_assert(!std::is_copy_constructible_v<Receiver>);
        {
            Source source;
            pc_screen_encoder_options options{}; options.structure_size=sizeof(options);
            require(source.open(options),"source open failed");
            pc_screen_encoded_frame frame{}; frame.structure_size=sizeof(frame);
            require(source.pop(frame)==1,"source pop failed");
            // A single borrowed AU feeds independent destinations; neither
            // destination creates another source or consumes its queue.
            const std::vector<std::uint8_t> a(source.data(),source.data()+frame.length);
            const std::vector<std::uint8_t> b(source.data(),source.data()+frame.length);
            require(a==b && Mechanics::captureOpens==1 && Mechanics::pops==1,
                    "fanout repeated capture/encode/pop");
            require(source.requestKeyframe(),"keyframe request failed");
            source.close(); source.close();
        }
        require(Mechanics::captureCloses==1,"source close not idempotent");
        for(unsigned iteration=0;iteration<20;++iteration) {
            Receiver receiver;
            pc_screen_decoder_options options{}; options.structure_size=sizeof(options);
            require(receiver.open(options),"receiver open failed");
            auto packet=[&](unsigned seq,unsigned ts,bool marker,std::vector<std::uint8_t> bytes,unsigned now=0) {
                receiver.receive({static_cast<std::uint16_t>(seq),ts,marker,std::move(bytes),0,false},now);
            };
            const auto before=Mechanics::pushes;
            packet(0,100,true,{24,0,2,0x67,0x11,0,2,0x68,0x22,0,2,0x65,0x33});
            require(Mechanics::pushes==before+1,"bootstrap never reached decoder");
            packet(1,200,false,{0x7c,0x81,0x10});
            packet(3,200,true,{0x7c,0x41,0x30}); receiver.tick(21);
            require(Mechanics::pushes==before+1 && receiver.rtpReceiver_.needsKeyframe(),
                    "damaged AU reached extracted decoder");
            packet(2,200,false,{0x7c,0x01,0x20},22);
            packet(4,300,true,{0x65,0x40},23);
            require(Mechanics::pushes==before+2,"IDR recovery failed");
            Mechanics::refuse=true;
            packet(5,400,true,{0x61,0x55},24);
            require(receiver.rtpReceiver_.needsKeyframe(),"queue refusal lost recovery requirement");
            Mechanics::refuse=false;
            receiver.close(); receiver.close();
            require(receiver.accessUnitsDelivered_==2,"teardown lost retained counters");
            require(receiver.open(options),"receiver reopen failed");
            require(receiver.accessUnitsDelivered_==0 && receiver.firstAccessUnitNals_.empty(),
                    "new receiver retained prior lifetime state");
        }
        require(Mechanics::decoderOpens==40 && Mechanics::decoderCloses==40,"decoder lifetime mismatch");
        std::cout<<"PASS: single source/AU reuse; extracted receive bootstrap/loss/late/IDR/queue recovery; 40 decoder lifetimes\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n'; return 1;
    }
}

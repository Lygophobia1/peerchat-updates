#include <opus/opus.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{int error=OPUS_OK;OpusEncoder* encoder=opus_encoder_create(16000,1,OPUS_APPLICATION_VOIP,&error);OpusDecoder* decoder=opus_decoder_create(16000,1,&error);require(encoder&&decoder&&error==OPUS_OK,"Opus initialization failed");opus_encoder_ctl(encoder,OPUS_SET_BITRATE(24000));opus_encoder_ctl(encoder,OPUS_SET_INBAND_FEC(1));std::vector<opus_int16> input(320),output(960);unsigned char packet[4000];long long energy=0;size_t bytes=0;for(int frame=0;frame<100;++frame){for(int i=0;i<320;++i)input[i]=static_cast<opus_int16>(9000*std::sin(2.0*3.141592653589793*440.0*(frame*320+i)/16000.0));int encoded=opus_encode(encoder,input.data(),320,packet,sizeof(packet));require(encoded>0,"Opus encoding failed");bytes+=encoded;int decoded=opus_decode(decoder,packet,encoded,output.data(),static_cast<int>(output.size()),0);require(decoded==320,"Opus changed the 20 ms frame size");for(int i=0;i<decoded;++i)energy+=std::abs(static_cast<int>(output[i]));}require(energy>1000000,"Decoded loopback was silent");require(bytes<100000,"Encoded packets exceeded the test budget");opus_encoder_destroy(encoder);opus_decoder_destroy(decoder);std::cout<<"PASS: device-free 16 kHz mono Opus loopback.\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}

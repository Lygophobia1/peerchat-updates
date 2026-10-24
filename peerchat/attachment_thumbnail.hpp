#pragma once
#include "protocol.hpp"

namespace peerchat::attachment_thumbnails {
constexpr size_t MaxBytes=2048;
constexpr unsigned MaxEdge=128;
// Only small baseline JPEGs are offered to platform decoders. The image is a
// sender-authenticated presentation hint, never proof of original file bytes.
inline bool valid(const Blob& bytes){
 if(bytes.empty())return true;
 if(bytes.size()<16||bytes.size()>MaxBytes||bytes[0]!=255||bytes[1]!=216||bytes[bytes.size()-2]!=255||bytes.back()!=217)return false;
 size_t at=2;bool dimensions=false;
 while(at+4<=bytes.size()){
  if(bytes[at++]!=255)return false;
  while(at<bytes.size()&&bytes[at]==255)++at;
  if(at>=bytes.size())return false;
  auto marker=bytes[at++];if(marker==0||marker==216||marker==217)return false;
  if(marker>=208&&marker<=215)return false;
  if(at+2>bytes.size())return false;
  size_t length=(size_t(bytes[at])<<8)|bytes[at+1];
  if(length<2||length>bytes.size()-at)return false;
  if(marker==192){
   if(dimensions||length<8||bytes[at+2]!=8)return false;
   unsigned height=(unsigned(bytes[at+3])<<8)|bytes[at+4],width=(unsigned(bytes[at+5])<<8)|bytes[at+6],components=bytes[at+7];
   if(!width||width>MaxEdge||!height||height>MaxEdge||(components!=1&&components!=3)||length!=8+3*components)return false;
   dimensions=true;
  }else if(marker>=193&&marker<=207&&marker!=196&&marker!=200&&marker!=204)return false;
  if(marker==218)return dimensions;
  at+=length;
 }
 return false;
}
}

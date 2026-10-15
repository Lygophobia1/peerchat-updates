#define main legacy_transfer_manager_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "transfer_manager_test.cpp"
#pragma GCC diagnostic pop
#undef main
#include "group_attachment_blob.hpp"
struct PreparationSink:MemorySink {bool discarded=false;unsigned flushed=0;using MemorySink::MemorySink;void flush()override{++flushed;}void discard()override{discarded=true;result.bytes.clear();result.committed=false;}};
int main(){try{
 for(size_t length:{0u,1u,32768u,32769u,1048577u}){
  MemorySource source(data(length),u8"original-📷.png");Result cipher;PreparationSink sink(cipher);windows::Sha256 ph,ch;group_attachments::Sealer prepare(source,sink,{std::string(32,'1'),std::string(32,'2'),std::string(64,'a'),4,7},ph,ch);
  rejected([&]{prepare.takeManifest();});bool ready=false;unsigned steps=0;std::uint64_t prior=0;
  while(!ready){auto before=cipher.bytes.size();ready=prepare.step();check(cipher.bytes.size()-before<=group_attachments::BlockBytes+group_attachments::TagBytes);check(prepare.processedBytes()>=prior&&prepare.processedBytes()-prior<=group_attachments::BlockBytes);prior=prepare.processedBytes();++steps;}
  check(prepare.size()==length&&prior==length&&!cipher.committed&&sink.flushed==1&&!sink.discarded);check(steps==std::max<size_t>(1,(length+32767)/32768)+2);
  auto manifest=prepare.takeManifest();rejected([&]{prepare.step();});rejected([&]{prepare.takeManifest();});sink.commit();MemorySource encrypted(cipher.bytes);Result result;PreparationSink output(result);windows::Sha256 p2,c2;group_attachments::open(encrypted,output,manifest,p2,c2);check(result.committed&&result.bytes==data(length));
 }
 {MemorySource source(data(1048576));Result result;PreparationSink sink(result);windows::Sha256 ph,ch;{group_attachments::Sealer preparation(source,sink,{std::string(32,'1'),std::string(32,'2'),std::string(64,'a'),4,7},ph,ch);preparation.step();preparation.step();preparation.cancel();rejected([&]{preparation.step();});}check(sink.discarded&&!result.committed&&result.bytes.empty());}
 {MemorySource source(data(1));Result result;PreparationSink sink(result);windows::Sha256 ph,ch;{group_attachments::Sealer preparation(source,sink,{std::string(32,'1'),std::string(32,'2'),std::string(64,'a'),4,7},ph,ch);while(!preparation.step()){} }check(sink.discarded&&!result.committed);}
 std::cout<<"PASS bounded incremental encryption, exact empty/Unicode/boundary content, no commit before key handoff, cancellation/abandonment and single-use manifest; no physical pacing claim\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

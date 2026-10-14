#define main legacy_transfer_manager_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "transfer_manager_test.cpp"
#pragma GCC diagnostic pop
#undef main
#include "group_attachment_blob.hpp"
struct BlobSink:MemorySink {using MemorySink::MemorySink;void discard()override{result.bytes.clear();result.committed=false;}};
int main(){try{
 for(size_t size:{0u,1u,32768u,32769u,1048576u,3145728u}){
  auto original=data(size);MemorySource source(original,u8"résumé-日本.png");Result encrypted;BlobSink sink(encrypted);windows::Sha256 ph,ch;
  auto manifest=group_attachments::seal(source,sink,{std::string(32,'1'),std::string(32,'2'),std::string(64,'a'),3,7},ph,ch);
  check(encrypted.committed&&encrypted.bytes.size()==manifest.cipherSize());MemorySource wire(encrypted.bytes);Result decoded;BlobSink plain(decoded);windows::Sha256 p,c;group_attachments::open(wire,plain,manifest,p,c);check(decoded.committed&&decoded.bytes==original);
  if(size){auto corrupted=encrypted.bytes;corrupted[std::min<size_t>(30,corrupted.size()-1)]^=1;MemorySource bad(corrupted);Result failed;BlobSink target(failed);windows::Sha256 p2,c2;rejected([&]{group_attachments::open(bad,target,manifest,p2,c2);});check(!failed.committed&&failed.bytes.empty());}
  manifest.binding.epoch++;Result wrong;BlobSink destination(wrong);windows::Sha256 p3,c3;rejected([&]{group_attachments::open(wire,destination,manifest,p3,c3);});check(!wrong.committed);manifest.binding.epoch--;
  auto rejectOpen=[&]{Result failed;BlobSink output(failed);windows::Sha256 p5,c5;rejected([&]{group_attachments::open(wire,output,manifest,p5,c5);});check(!failed.committed&&failed.bytes.empty());};
  manifest.binding.account++;rejectOpen();manifest.binding.account--;
  auto group=manifest.binding.group;manifest.binding.group=std::string(32,'3');rejectOpen();manifest.binding.group=group;
  auto sender=manifest.binding.sender;manifest.binding.sender=std::string(64,'b');rejectOpen();manifest.binding.sender=sender;
  manifest.key[0]^=1;rejectOpen();manifest.key[0]^=1;
  manifest.plainDigest[0]^=1;rejectOpen();manifest.plainDigest[0]^=1;
  manifest.cipherDigest[0]^=1;rejectOpen();manifest.cipherDigest[0]^=1;
  auto truncated=encrypted.bytes;truncated.pop_back();MemorySource shortWire(truncated);Result incomplete;BlobSink partial(incomplete);windows::Sha256 p6,c6;rejected([&]{group_attachments::open(shortWire,partial,manifest,p6,c6);});check(!incomplete.committed&&incomplete.bytes.empty());
  check(group_attachments::nonce(manifest,0)!=group_attachments::nonce(manifest,1));
  manifest.name="different.png";Result renamed;BlobSink changed(renamed);windows::Sha256 p4,c4;rejected([&]{group_attachments::open(wire,changed,manifest,p4,c4);});check(!renamed.committed);
  std::cout<<"PASS bounded encrypted blob size="<<size<<" exact plaintext; epoch/account/group/sender/name/key/digest/tamper/truncation rejection; no failed plaintext commit\n";
 }
 std::cout<<"PASS codec only: no server, no group admission claim, no production exposure\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

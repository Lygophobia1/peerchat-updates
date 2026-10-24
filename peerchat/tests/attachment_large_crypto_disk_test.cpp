#define main retained_transfer_fixture_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "transfer_manager_test.cpp"
#pragma GCC diagnostic pop
#undef main
#include "../group_attachment_finalizer.hpp"
#include <psapi.h>
struct PatternSource final:Source {
 std::uint64_t length;explicit PatternSource(std::uint64_t n):length(n){}
 std::uint64_t size()const override{return length;}std::string name()const override{return u8"cached-日本.bin";}
 Blob read(std::uint64_t at,size_t n)override{check(n<=ChunkBytes&&at<=length&&n<=length-at);Blob b(n);for(size_t i=0;i<n;++i)b[i]=static_cast<unsigned char>((at+i)*29+71);return b;}
};
int main(int argc,char** argv){try{
 check(argc==2);auto root=std::filesystem::absolute(argv[1]);check(!std::filesystem::exists(root));std::filesystem::create_directories(root);
 using namespace group_attachments;PatternSource source(MaxCachedFileBytes);const std::string id(32,'3');
 Metadata metadata{id,"ciphertext.peerchat",HeaderBytes+source.size()+((source.size()+BlockBytes-1)/BlockBytes)*TagBytes,{}};
 std::string cipherPath;std::optional<Manifest> manifest;
 {windows::FileSink sink(root.wstring(),metadata);windows::Sha256 p,c;Sealer sealer(source,sink,{id,std::string(32,'2'),std::string(64,'a'),7,3},p,c);std::uint64_t steps=0;while(!sealer.step())++steps;check(steps<50000);manifest.emplace(sealer.takeManifest());sink.commit();cipherPath=unicode::encode(sink.path());}
 check(std::filesystem::file_size(unicode::decode(cipherPath))==manifest->cipherSize());
 Metadata plain{std::string(32,'4'),source.name(),source.size(),manifest->plainDigest};std::wstring output;
 {windows::FileSource cipher(unicode::decode(cipherPath),"ciphertext.peerchat",Purpose::GroupCacheCiphertext);windows::FileSink sink(root.wstring(),plain);windows::Sha256 p,c;BlockFinalizer<false> verify(cipher,sink,*manifest,p,c);while(!verify.step()){}check(verify.completion()&&verify.processedBytes()==MaxCachedFileBytes);output=sink.path();}
 {windows::FileSource file(output);windows::Sha256 hash;for(std::uint64_t at=0;at<source.size();at+=ChunkBytes){auto n=static_cast<size_t>(std::min<std::uint64_t>(ChunkBytes,source.size()-at));auto bytes=file.read(at,n);check(bytes==source.read(at,n));hash.update(bytes.data(),bytes.size());}check(hash.finish()==manifest->plainDigest);}
 // Exceeding the hard limit is rejected before reading or encrypting a block.
 {PatternSource tooLarge(MaxCachedFileBytes+1);Metadata rejectedMetadata{std::string(32,'5'),"ciphertext.peerchat",metadata.size+17,{}};windows::FileSink sink(root.wstring(),rejectedMetadata);windows::Sha256 p,c;rejected([&]{Sealer blocked(tooLarge,sink,{rejectedMetadata.id,std::string(32,'2'),std::string(64,'a'),7,3},p,c);});}
 PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof(memory);check(GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory))&&memory.PeakWorkingSetSize<128ull*1024*1024);
 std::cout<<"PASS actual 1.5-GiB cached ciphertext and plaintext files; AES-GCM every block, full SHA-256, exact output, +1-byte early rejection, bounded 32-KiB I/O; peak working set="<<memory.PeakWorkingSetSize<<". Local disk codec test, not server upload. Fixture: "<<root.string()<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
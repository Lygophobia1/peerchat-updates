#include "../portable/attachment_file_scope.hpp"
#include <iostream>
using namespace peerchat;
using namespace peerchat::transfer;
int main(){try{
 auto check=[](bool value){if(!value)throw std::runtime_error("File scope test failed");};
 auto rejects=[&](auto action){bool rejected=false;try{action();}catch(...){rejected=true;}check(rejected);};
 protocol::Writer ordinary(protocol::FileTransfer);auto before=ordinary.data;
 posix::tokenPurpose(ordinary,Purpose::PeerFile);check(before==ordinary.data);
 protocol::Reader old(ordinary.data);check(old.type()==protocol::FileTransfer);posix::tokenPurpose(old,Purpose::PeerFile);
 protocol::Writer cache(protocol::FileTransfer);posix::tokenPurpose(cache,Purpose::DirectCacheCiphertext);
 protocol::Reader restored(cache.data);check(restored.type()==protocol::FileTransfer);posix::tokenPurpose(restored,Purpose::DirectCacheCiphertext);
 rejects([&]{protocol::Reader r(cache.data);r.type();posix::tokenPurpose(r,Purpose::PeerFile);});
 rejects([&]{protocol::Reader r(ordinary.data);r.type();posix::tokenPurpose(r,Purpose::DirectCacheCiphertext);});
 rejects([&]{protocol::Writer w(protocol::FileTransfer);posix::tokenPurpose(w,Purpose::GroupCacheCiphertext);});
 auto tampered=cache.data;tampered.back()^=1;
 rejects([&]{protocol::Reader r(tampered);r.type();posix::tokenPurpose(r,Purpose::DirectCacheCiphertext);});
 cache.number(9);rejects([&]{protocol::Reader r(cache.data);r.type();posix::tokenPurpose(r,Purpose::DirectCacheCiphertext);});
 check(posix::maximum(Purpose::PeerFile)==256ull*1024*1024&&posix::maximum(Purpose::DirectCacheCiphertext)==MaxCacheCipherBytes&&MaxCacheCipherBytes>MaxFileBytes);
 std::cout<<"PASS production Android file-scope helpers: legacy token bytes, purpose binding, substitutions, tampering, trailing data and distinct ciphertext bound. HOST ONLY; no POSIX filesystem execution.\n";
 return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

#define _WIN32_WINNT 0x0602
#define NOMINMAX
#include "../archive_attachment_catalog.hpp"
#include "../windows_transfer_io.hpp"
#include <fstream>
#include <iostream>
int main(int argc,char** argv){try{
    using namespace peerchat;protocol::check(argc==2);auto root=std::filesystem::path(argv[1]);
    Storage source((root/"pair-source").wstring()),reader((root/"pair-reader").wstring());auto a=source.profile(),b=reader.profile();const std::string contact(64,'a');
    for(auto store:{&source,&reader}){store->rememberAuthenticatedDeviceAccount(a.peerId,8);store->rememberAuthenticatedDeviceAccount(b.peerId,8);store->rememberAuthenticatedDeviceAccount(contact,9);}
    for(unsigned n=0;n<25;++n){
        StoredMessage message{Storage::newMessageId(),n%2?a.peerId:contact,n%2?contact:a.peerId,L"Packaged archive text",100+n,Delivery::Delivered,0,n+1};
        protocol::check(source.append(contact,message));
    }
    auto path=root/L"source-file.bin";Blob bytes(100000);for(size_t n=0;n<bytes.size();++n)bytes[n]=static_cast<unsigned char>(n*17);
    {std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());protocol::check(bool(file));}
    transfer::windows::Sha256 hash;for(size_t n=0;n<bytes.size();n+=transfer::ChunkBytes)hash.update(bytes.data()+n,std::min(transfer::ChunkBytes,bytes.size()-n));
    transfer::windows::FileSource file(path.wstring(),u8"résumé-日本.bin");
    archive::Attachment attachment;attachment.id=Storage::newMessageId();attachment.conversation=contact;attachment.sender=a.peerId;attachment.recipient=contact;attachment.senderAccount=8;attachment.recipientAccount=9;
    attachment.name=file.name();attachment.size=file.size();attachment.digest=hash.finish();attachment.utc=100;attachment.location=file.resumeToken();archive::AttachmentCatalog(source).retain(attachment);
    std::filesystem::create_directory(root/"downloads");std::cout<<attachment.id;return 0;
}catch(const std::exception& e){std::cerr<<e.what();return 1;}}

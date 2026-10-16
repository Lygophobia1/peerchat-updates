#pragma once

// TEST HARNESS ONLY. MinGW cannot compile the Android POSIX file adapter.
// This leaves production messaging/JNI/storage source intact while making
// every attempted attachment file or attachment hash operation fail closed.
#include "transfer_manager.hpp"

namespace peerchat::transfer::posix {
[[noreturn]] inline void attachmentUnavailable(){
    throw std::runtime_error("Android attachment file I/O is unavailable in the Windows JNI messaging proof");
}
class Cache {
public:
    explicit Cache(const std::string&) {} // Live messaging constructs this owner; no file is opened here.
    void reserve(std::uint64_t){attachmentUnavailable();}
};
class Sha256 final:public Hash {
public:
    Sha256(){attachmentUnavailable();}
    void update(const unsigned char*,size_t)override{attachmentUnavailable();}
    Digest finish()override{attachmentUnavailable();}
};
class FileSource final:public Source {
public:
    FileSource(std::shared_ptr<Cache>,std::string,std::string){attachmentUnavailable();}
    std::uint64_t size()const override{attachmentUnavailable();}
    std::string name()const override{attachmentUnavailable();}
    Blob read(std::uint64_t,size_t)override{attachmentUnavailable();}
    Blob resumeToken()const override{attachmentUnavailable();}
    const std::string& cacheName()const{attachmentUnavailable();}
    static std::unique_ptr<FileSource> create(std::shared_ptr<Cache>,const std::string&,const std::string&,const Blob&){attachmentUnavailable();}
    static std::unique_ptr<FileSource> reopen(std::shared_ptr<Cache>,const DurableState&){attachmentUnavailable();}
};
class FileSink final:public Sink {
public:
    FileSink(std::shared_ptr<Cache>,const Metadata&){attachmentUnavailable();}
    void append(const Blob&)override{attachmentUnavailable();}
    void flush()override{attachmentUnavailable();}
    Blob read(std::uint64_t,size_t)override{attachmentUnavailable();}
    void commit()override{attachmentUnavailable();}
    void discard()override{attachmentUnavailable();}
    Blob resumeToken()const override{attachmentUnavailable();}
    Blob sourceToken()const{attachmentUnavailable();}
    static std::unique_ptr<FileSink> reopen(std::shared_ptr<Cache>,const DurableState&){attachmentUnavailable();}
};
}

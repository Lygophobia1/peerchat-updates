#pragma once
#include "../device_key_manager.hpp"
#include "record_protection.hpp"

namespace peerchat::platform {
// Platform supplies profile-scoped cross-process locking and atomic replace.
// read returns false ONLY for genuine absence, never I/O failure/corruption.
class KeyRegistryStore {
public:
    virtual ~KeyRegistryStore() = default;
    virtual void locked(const std::function<void()>& operation) = 0;
    virtual bool read(std::vector<unsigned char>& protectedBytes) = 0;
    virtual void replace(const std::vector<unsigned char>& protectedBytes) = 0;
};

class ProtectedKeyRegistry final : public DeviceKeyRegistry {
    KeyRegistryStore& store_;
    RecordProtector& protection_;
    std::string context_;
    static constexpr std::size_t limit = 512 * 1024;
    static void require(bool value) { if (!value) throw std::runtime_error("Invalid device key registry"); }
    struct Writer {
        std::vector<unsigned char> data{'P','K','R',1};
        void number(std::uint64_t value, unsigned bytes) {
            for (unsigned i=0;i<bytes;++i) data.push_back(static_cast<unsigned char>(value >> (i*8)));
        }
        void blob(const std::vector<unsigned char>& value) {
            require(value.size()<=8192); number(value.size(),4); data.insert(data.end(),value.begin(),value.end());
        }
        void text(const std::string& value) { blob({value.begin(),value.end()}); }
    };
    struct Reader {
        const std::vector<unsigned char>& data;
        std::size_t pos=4;
        std::uint64_t number(unsigned bytes) {
            require(bytes<=8 && pos<=data.size() && bytes<=data.size()-pos);
            std::uint64_t value=0;
            for(unsigned i=0;i<bytes;++i) value|=std::uint64_t(data[pos++]) << (i*8);
            return value;
        }
        std::vector<unsigned char> blob(std::size_t maximum) {
            const auto size=number(4); require(size<=maximum && size<=data.size()-pos);
            std::vector<unsigned char> value(data.begin()+pos,data.begin()+pos+static_cast<std::size_t>(size));
            pos+=static_cast<std::size_t>(size); return value;
        }
        std::string text() { const auto value=blob(256); return {value.begin(),value.end()}; }
    };
public:
    ProtectedKeyRegistry(KeyRegistryStore& store, RecordProtector& protection, const std::string& profileId)
        : store_(store), protection_(protection), context_("PeerChat/device-key-registry/v1/"+profileId) {
        require(!profileId.empty() && profileId.size()<=256);
    }
    void locked(const std::function<void()>& operation) override { store_.locked(operation); }
    static std::vector<unsigned char> encode(const std::vector<DeviceKeyMetadata>& entries) {
        DeviceKeyManager::validate(entries); Writer writer; writer.number(entries.size(),4);
        for(const auto& e:entries) {
            writer.text(e.id); writer.number(static_cast<unsigned>(e.role),1); writer.text(e.algorithm);
            writer.number(e.generation,4); writer.text(e.providerAlias); writer.blob(e.publicKey);
            writer.number(e.createdUtcMilliseconds,8); writer.number(static_cast<unsigned>(e.state),1);
            writer.number(static_cast<unsigned>(e.registration),1); writer.text(e.predecessorId);
            writer.blob(e.signedBinding);
        }
        require(writer.data.size()<=limit); return writer.data;
    }
    static std::vector<DeviceKeyMetadata> decode(const std::vector<unsigned char>& bytes) {
        require(bytes.size()>=8 && bytes.size()<=limit && bytes[0]=='P' && bytes[1]=='K' && bytes[2]=='R' && bytes[3]==1);
        Reader reader{bytes}; const auto count=reader.number(4); require(count<=32);
        std::vector<DeviceKeyMetadata> entries;
        for(std::uint64_t i=0;i<count;++i) {
            DeviceKeyMetadata e;
            e.id=reader.text(); e.role=static_cast<DeviceKeyRole>(reader.number(1)); e.algorithm=reader.text();
            e.generation=static_cast<std::uint32_t>(reader.number(4)); e.providerAlias=reader.text(); e.publicKey=reader.blob(65);
            e.createdUtcMilliseconds=reader.number(8); e.state=static_cast<DeviceKeyState>(reader.number(1));
            e.registration=static_cast<DeviceKeyRegistration>(reader.number(1)); e.predecessorId=reader.text();
            e.signedBinding=reader.blob(8192); entries.push_back(std::move(e));
        }
        require(reader.pos==bytes.size()); DeviceKeyManager::validate(entries); return entries;
    }
    bool read(std::vector<DeviceKeyMetadata>& entries) override {
        std::vector<unsigned char> bytes;
        if(!store_.read(bytes)) { entries.clear(); return false; }
        require(!bytes.empty() && bytes.size()<=1024*1024);
        entries=decode(protection_.unprotect(bytes,context_)); return true;
    }
    void replace(const std::vector<DeviceKeyMetadata>& entries) override {
        auto protectedBytes=protection_.protect(encode(entries),context_);
        require(!protectedBytes.empty() && protectedBytes.size()<=1024*1024);
        store_.replace(protectedBytes);
    }
};
}

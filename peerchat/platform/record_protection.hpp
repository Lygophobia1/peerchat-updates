#pragma once
#include "../storage.hpp"

namespace peerchat::platform {
// Profile-bound encryption provider. Failure throws; there is no plaintext
// fallback. Context is authenticated record binding, not secret key material.
class RecordProtector {
public:
    virtual ~RecordProtector()=default;
    virtual Blob protect(const Blob&,const std::string& context)=0;
    virtual Blob unprotect(const Blob&,const std::string& context)=0;
};
}

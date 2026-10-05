#pragma once
#include "protected_key_registry.hpp"
#include "storage_backend.hpp"
#include <mutex>

namespace peerchat::platform {
// Reuses the platform SQL execution/protected-record adapters. No private keys
// enter SQLite. The connection belongs exclusively to this registry adapter;
// use a DISTINCT DATABASE FILE from message Storage: a registry authorization
// lease spans crypto and durable receive, which must not contend with itself
// for a second write transaction on the same SQLite file.
// BEGIN IMMEDIATE serializes registry updates across processes as well as
// adapter instances. Busy/I/O/corruption is an error, never a missing registry.
class SqlKeyRegistryStore final : public KeyRegistryStore {
    std::shared_ptr<SqlDatabase> database_;
    std::recursive_mutex mutex_;
    bool inside_=false;
    void requireLocked() const {
        if(!inside_)throw std::runtime_error("Key registry operation requires a transaction");
    }
public:
    explicit SqlKeyRegistryStore(std::shared_ptr<SqlDatabase> database):database_(std::move(database)) {
        if(!database_)throw std::runtime_error("Key registry database missing");
        database_->execute("PRAGMA synchronous=FULL; PRAGMA secure_delete=ON;"
            "CREATE TABLE IF NOT EXISTS device_key_registry(singleton INTEGER PRIMARY KEY CHECK(singleton=1),b BLOB NOT NULL);");
    }
    void locked(const std::function<void()>& operation) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        SqlTransaction transaction(*database_);
        inside_=true;
        try {operation();inside_=false;transaction.commit();}
        catch(...) {inside_=false;throw;}
    }
    bool read(Blob& bytes) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        requireLocked();CheckedSqlStatement statement(*database_,"SELECT b FROM device_key_registry WHERE singleton=1");
        if(!statement.row()){bytes.clear();return false;}
        bytes=statement.bytes(0);return true;
    }
    void replace(const Blob& bytes) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        requireLocked();
        if(bytes.empty()||bytes.size()>1048576)throw std::runtime_error("Invalid protected key registry size");
        CheckedSqlStatement statement(*database_,"INSERT INTO device_key_registry(singleton,b) VALUES(1,?) ON CONFLICT(singleton) DO UPDATE SET b=excluded.b");
        statement.blob(1,bytes);statement.row();
    }
};
}

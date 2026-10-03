#pragma once
#include "../storage.hpp"
#include "identity_provider.hpp"
#include "record_protection.hpp"
#include <stdexcept>

namespace peerchat::platform {
class SqlStatement {
public:
    virtual ~SqlStatement()=default;
    virtual void bindText(int,const std::string&)=0;
    virtual void bindBlob(int,const Blob&)=0;
    virtual void bindNumber(int,std::int64_t)=0;
    virtual bool row()=0;
    virtual Blob bytes(int)=0;
    virtual std::string text(int)=0;
    virtual std::int64_t number(int)=0;
};
class SqlDatabase {
public:
    virtual ~SqlDatabase()=default;
    virtual void execute(const std::string&)=0;
    virtual std::unique_ptr<SqlStatement> prepare(const std::string&)=0;
};
// Common checked cursor: an adapter must not return unbounded encrypted records.
// SQLite adapters perform execution only; callers retain record policy.
class CheckedSqlStatement {
    std::unique_ptr<SqlStatement> statement_;
public:
    CheckedSqlStatement(SqlDatabase& database,const std::string& sql):statement_(database.prepare(sql)){
        if(!statement_)throw std::runtime_error("Database statement unavailable");
    }
    void text(int i,const std::string& value){statement_->bindText(i,value);}
    void blob(int i,const Blob& value){statement_->bindBlob(i,value);}
    void number(int i,std::int64_t value){statement_->bindNumber(i,value);}
    bool row(){return statement_->row();}
    Blob bytes(int i){auto value=statement_->bytes(i);if(value.empty()||value.size()>1024*1024)throw std::runtime_error("Invalid encrypted record size");return value;}
    std::string text(int i){return statement_->text(i);}
    std::int64_t number(int i){return statement_->number(i);}
};
class SqlTransaction {
    SqlDatabase& database_;
    bool committed_=false;
public:
    explicit SqlTransaction(SqlDatabase& database):database_(database){database_.execute("BEGIN IMMEDIATE");}
    SqlTransaction(const SqlTransaction&)=delete;
    SqlTransaction& operator=(const SqlTransaction&)=delete;
    void commit(){database_.execute("COMMIT");committed_=true;}
    ~SqlTransaction(){if(!committed_){try{database_.execute("ROLLBACK");}catch(...){}}}
};
// Storage owns schema, record serialization, transactions, history and outbox.
// The platform provides SQLite execution and protected blobs, never message policy.
class StoragePlatform {
public:
    std::shared_ptr<SqlDatabase> database;
    std::shared_ptr<RecordProtector> records;
    std::shared_ptr<IdentityProvider> identity;
};
}

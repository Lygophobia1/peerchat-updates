#include "../storage.cpp"
#include <iostream>

int main(){using namespace peerchat;using namespace storage_detail;try{
    auto directory=(std::filesystem::current_path()/L"build"/L"android-foundation"/L"profiles"/wide(Storage::newMessageId())).wstring();
    const Blob payload{0,1,255,0};const std::string context="PeerChat.v1/backend-proof";
    platform::WindowsRecordProtector protector;
    {
        WindowsSqlDatabase db(directory);
        db.execute("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA secure_delete=ON;");
        db.execute("CREATE TABLE proof(k TEXT PRIMARY KEY,b BLOB NOT NULL,n INTEGER NOT NULL)");
        platform::SqlTransaction transaction(db);
        auto insert=db.prepare("INSERT INTO proof(k,b,n) VALUES(?,?,?)");
        insert->bindText(1,"record");insert->bindBlob(2,protector.protect(payload,context));insert->bindNumber(3,INT64_MAX);
        require(!insert->row(),"Insert returned a row");insert.reset();transaction.commit();
        {platform::SqlTransaction rollback(db);db.execute("DELETE FROM proof");}
    }
    {
        WindowsSqlDatabase db(directory);
        auto read=db.prepare("SELECT k,b,n FROM proof WHERE k=?");read->bindText(1,"record");
        require(read->row()&&read->text(0)=="record"&&read->number(2)==INT64_MAX,"Provider reopen mismatch");
        require(protector.unprotect(read->bytes(1),context)==payload,"Protected provider blob mismatch");
        require(!read->row(),"Unexpected duplicate row");
        bool rejected=false;try{db.prepare("invalid SQL");}catch(const std::exception&){rejected=true;}
        require(rejected,"Invalid statement accepted");
    }
    std::cout<<"PASS: SQLite provider binding, protected blob, integer range, transaction rollback and database reopen\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}

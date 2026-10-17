// TEST ONLY. Not referenced by Android/Windows product build definitions.
// Real Windows SQLite/DPAPI provide host platform primitives; the production
// Android JNI/Profile/shared runtime and Java TLS implementation remain intact.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef NOMINMAX // storage.cpp defines it itself; MinGW's C++ runtime already defined it.
#include "../storage.cpp"
#include "../portable/peerchat_core.cpp"

namespace host_fixture {
struct Database {
    std::unique_ptr<peerchat::storage_detail::WindowsSqlDatabase> sql;
    std::map<std::uint64_t,std::unique_ptr<peerchat::platform::SqlStatement>> statements;
};
std::mutex gate;
std::uint64_t next=1;
std::map<std::uint64_t,Database> databases;
template<class F> jbyteArray call(JNIEnv* env,F body){
    try{std::lock_guard<std::mutex> lock(gate);return peerchat::platform::android::Context::input(env,body());}
    catch(...){if(env->ExceptionCheck())env->ExceptionClear();auto type=env->FindClass("java/lang/IllegalStateException");if(type)env->ThrowNew(type,"Host test SQL/protection provider failed");return nullptr;}
}
// Isolated fixture setup only: provision explicit, already verified test pins.
// This export is compiled into test DLLs, never a product library/build target.
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_platform_tls_NativeDmAttachmentSide_profileFixtureDiagnostic(JNIEnv* env,jclass,jobject facade){
    std::string result="ok";
    try{auto providers=peerchat::platform::android::platform(env,facade);peerchat::Storage storage(providers);auto profile=storage.profile();peerchat::delivery::DeliveryManager delivery(storage,profile.peerId);}
    catch(const std::exception& error){result=error.what();}catch(...){result="Unknown host profile failure";}
    if(env->ExceptionCheck())env->ExceptionClear();
    return peerchat::platform::android::Context::input(env,result);
}
extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_platform_tls_NativeDmAttachmentSide_trustFixturePeer(JNIEnv* env,jclass,jlong handle,jbyteArray input,jboolean verified){
    try{auto instance=lookup(static_cast<pc_core_handle>(handle));peerchat::protocol::check(bool(instance));std::lock_guard<std::mutex> lock(instance->gate);
        auto key=peerchat::platform::android::Context::output(env,input);auto device=peerchat::Storage::publicKeyId(key);peerchat::protocol::check(key.size()==72&&instance->friends.accountFor(device)>0);
        instance->storage->saveContact({device,L"Explicit isolated JNI fixture pin",key,{},verified==JNI_TRUE});return 0;
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return 1;}
}
// Test-only protected archive classification: deny ordinary traffic for an ID.
extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_platform_tls_DirectFileCompatibilityProof_verifiedFixturePeer(JNIEnv* env,jclass,jlong handle,jbyteArray input,jboolean verified){
    return Java_com_peerchat_android_platform_tls_NativeDmAttachmentSide_trustFixturePeer(env,nullptr,handle,input,verified);
}
// Does not create any archive authorization/grant or bypass production dispatch.
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_platform_tls_NativeTransferProof_markArchive(JNIEnv* env,jclass,jlong handle,jbyteArray input){
    try{
        auto instance=lookup(static_cast<pc_core_handle>(handle));peerchat::protocol::check(bool(instance));std::lock_guard<std::mutex> lock(instance->gate);
        auto bytes=peerchat::platform::android::Context::output(env,input);std::string id(bytes.begin(),bytes.end());peerchat::protocol::check(peerchat::protocol::id(id,32)&&bool(instance->archiveTransfers));
        auto w=peerchat::archive::attachmentHeader(10);w.number(instance->friends.localAccount());w.string(instance->profile.peerId);w.string(id);
        static const char hex[]="0123456789abcdef";std::string context;for(auto b:peerchat::digestDevicePayload(w.data)){context+=hex[b>>4];context+=hex[b&15];}
        instance->storage->saveArchiveSyncState(context,{},peerchat::Blob{1});return peerchat::platform::android::Context::input(env,peerchat::Blob{});
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return nullptr;}
}
peerchat::Blob number(std::uint64_t value){peerchat::Blob bytes;for(int i=0;i<8;++i)bytes.push_back(static_cast<unsigned char>(value>>(8*i)));return bytes;}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_platform_tls_ReliableProfileProof_database(JNIEnv* env,jclass,jbyteArray directory){
    return host_fixture::call(env,[&]{
        auto input=peerchat::platform::android::Context::output(env,directory);
        auto path=peerchat::unicode::decode(std::string(input.begin(),input.end()));
        auto id=host_fixture::next++;host_fixture::Database database;
        database.sql=std::make_unique<peerchat::storage_detail::WindowsSqlDatabase>(path);
        host_fixture::databases.emplace(id,std::move(database));return host_fixture::number(id);
    });
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_platform_tls_ReliableProfileProof_sql(JNIEnv* env,jclass,jlong db,jlong stmt,jint op,jint index,jbyteArray bytes,jlong number,jboolean blob){
    return host_fixture::call(env,[&]()->peerchat::Blob{
        auto found=host_fixture::databases.find(static_cast<std::uint64_t>(db));peerchat::protocol::check(found!=host_fixture::databases.end());
        auto& database=found->second;
        auto input=peerchat::platform::android::Context::output(env,bytes);std::string text(input.begin(),input.end());
        if(op==0){database.sql->execute(text);return {};}
        if(op==1){auto id=host_fixture::next++;database.statements.emplace(id,database.sql->prepare(text));return host_fixture::number(id);}
        auto statement=database.statements.find(static_cast<std::uint64_t>(stmt));peerchat::protocol::check(statement!=database.statements.end());
        auto& value=*statement->second;
        switch(op){
        case 2:if(blob==JNI_TRUE)value.bindBlob(index,input);else value.bindText(index,text);return {};
        case 3:value.bindNumber(index,number);return {};
        case 4:return {static_cast<unsigned char>(value.row())};
        case 5:return value.bytes(index);
        case 6:{auto result=value.text(index);return {result.begin(),result.end()};}
        case 7:return host_fixture::number(static_cast<std::uint64_t>(value.number(index)));
        case 8:database.statements.erase(statement);return {};
        default:throw std::runtime_error("Invalid test SQL operation");
        }
    });
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_platform_tls_ReliableProfileProof_protection(JNIEnv* env,jclass,jboolean encrypt,jbyteArray input,jbyteArray label){
    return host_fixture::call(env,[&]{
        auto bytes=peerchat::platform::android::Context::output(env,input),context=peerchat::platform::android::Context::output(env,label);
        return peerchat::storage_detail::protect(bytes,std::string(context.begin(),context.end()),encrypt==JNI_TRUE);
    });
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_platform_tls_ReliableProfileProof_closeDatabase(JNIEnv* env,jclass,jlong db){
    return host_fixture::call(env,[&]()->peerchat::Blob{
        peerchat::protocol::check(host_fixture::databases.erase(static_cast<std::uint64_t>(db))==1);return {};
    });
}

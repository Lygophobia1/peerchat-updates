/* Read-only Android diagnostic. No BLOBs, messages, credentials or keys read.
 * Uses the device's existing SQLite, not an application dependency. */
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;
int main(int argc,char** argv){
    if(argc!=2||strstr(argv[1],"../")||!strstr(argv[1],"no_backup/profiles/")||!strstr(argv[1],"/peerchat.sqlite"))return 2;
    void* library=dlopen("/system/lib64/libsqlite.so",RTLD_NOW|RTLD_LOCAL);
    if(!library){puts("probe_sqlite_unavailable");return 3;}
    int (*open_db)(const char*,sqlite3**,int,const char*)=dlsym(library,"sqlite3_open_v2");
    int (*close_db)(sqlite3*)=dlsym(library,"sqlite3_close");
    int (*prepare)(sqlite3*,const char*,int,sqlite3_stmt**,const char**)=dlsym(library,"sqlite3_prepare_v2");
    int (*step)(sqlite3_stmt*)=dlsym(library,"sqlite3_step");
    int (*finish)(sqlite3_stmt*)=dlsym(library,"sqlite3_finalize");
    const unsigned char* (*text)(sqlite3_stmt*,int)=dlsym(library,"sqlite3_column_text");
    long long (*number)(sqlite3_stmt*,int)=dlsym(library,"sqlite3_column_int64");
    if(!open_db||!close_db||!prepare||!step||!finish||!text||!number)return 4;
    sqlite3* db=0;sqlite3_stmt* query=0;
    if(open_db(argv[1],&db,1,0)){puts("probe_readonly_open_failed");if(db)close_db(db);return 5;}
    const char* sql="SELECT m.peer,COUNT(*),EXISTS(SELECT 1 FROM objects WHERE k='authenticated-device-account/v1/'||m.peer) FROM messages m GROUP BY m.peer LIMIT 32";
    if(prepare(db,sql,-1,&query,0)){puts("probe_metadata_query_failed");close_db(db);return 6;}
    int result;
    while((result=step(query))==100){
        const char* peer=(const char*)text(query,0);int valid=peer&&strlen(peer)==64;
        if(valid)for(int n=0;n<64;n++)if(!((peer[n]>='0'&&peer[n]<='9')||(peer[n]>='a'&&peer[n]<='f')))valid=0;
        if(valid)printf("routing_device=%s record_count=%lld historical_mapping_present=%lld\n",peer,number(query,1),number(query,2));
        else puts("noncanonical_routing_metadata_present");
    }
    finish(query);close_db(db);dlclose(library);
    return result==101?0:7;
}

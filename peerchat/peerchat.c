#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIMIT 4096
typedef struct { SOCKET socket; BCRYPT_KEY_HANDLE tx, rx; uint64_t sent, received; } Chat;
static void die(const char *message) { fprintf(stderr, "%s\n", message); exit(1); }
static void check(NTSTATUS status) { if (status < 0) die("Windows cryptography operation failed."); }
static void random_bytes(unsigned char *p, ULONG n) { check(BCryptGenRandom(NULL,p,n,BCRYPT_USE_SYSTEM_PREFERRED_RNG)); }
static int transfer(SOCKET s, unsigned char *p, int n, int writing) {
    while (n) {
        int k = writing ? send(s,(char *)p,n,0) : recv(s,(char *)p,n,0);
        if (k <= 0) return 0;
        p += k; n -= k;
    }
    return 1;
}
/* Domain-separated HMAC over both fresh challenges yields independent directional keys. */
static BCRYPT_KEY_HANDLE derive(unsigned char master[32], unsigned char challenges[64], unsigned char direction) {
    BCRYPT_ALG_HANDLE hash_alg, aes;
    BCRYPT_HASH_HANDLE hash;
    BCRYPT_KEY_HANDLE key;
    unsigned char material[32];
    const unsigned char domain[] = "PeerChat-v1-session";
    check(BCryptOpenAlgorithmProvider(&hash_alg,BCRYPT_SHA256_ALGORITHM,NULL,BCRYPT_ALG_HANDLE_HMAC_FLAG));
    check(BCryptCreateHash(hash_alg,&hash,NULL,0,master,32,0));
    check(BCryptHashData(hash,(PUCHAR)domain,sizeof(domain)-1,0));
    check(BCryptHashData(hash,challenges,64,0));
    check(BCryptHashData(hash,&direction,1,0));
    check(BCryptFinishHash(hash,material,32,0));
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(hash_alg,0);
    check(BCryptOpenAlgorithmProvider(&aes,BCRYPT_AES_ALGORITHM,NULL,0));
    check(BCryptSetProperty(aes,BCRYPT_CHAINING_MODE,(PUCHAR)BCRYPT_CHAIN_MODE_GCM,sizeof(BCRYPT_CHAIN_MODE_GCM),0));
    check(BCryptGenerateSymmetricKey(aes,&key,NULL,0,material,32,0));
    BCryptCloseAlgorithmProvider(aes,0);
    SecureZeroMemory(material,sizeof(material));
    return key;
}
static int crypt_frame(BCRYPT_KEY_HANDLE key, uint64_t counter, unsigned char header[4],
                       unsigned char *input, ULONG size, unsigned char *output, unsigned char tag[16], int encrypt) {
    unsigned char nonce[12] = {0};
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth;
    ULONG written = 0;
    for (int i=0;i<8;i++) nonce[11-i]=(unsigned char)(counter >> (i*8));
    BCRYPT_INIT_AUTH_MODE_INFO(auth);
    auth.pbNonce=nonce; auth.cbNonce=sizeof(nonce);
    auth.pbAuthData=header; auth.cbAuthData=4;
    auth.pbTag=tag; auth.cbTag=16;
    NTSTATUS status = encrypt ? BCryptEncrypt(key,input,size,&auth,NULL,0,output,size,&written,0)
                              : BCryptDecrypt(key,input,size,&auth,NULL,0,output,size,&written,0);
    return status >= 0 && written == size;
}
static int send_frame(Chat *c, const unsigned char *message, unsigned int n) {
    unsigned char header[4], cipher[LIMIT], tag[16];
    if (!n || n > LIMIT || c->sent == UINT64_MAX) return 0;
    header[0]=0; header[1]=0; header[2]=(unsigned char)(n>>8); header[3]=(unsigned char)n;
    if (!crypt_frame(c->tx,c->sent++,header,(PUCHAR)message,n,cipher,tag,1)) return 0;
    return transfer(c->socket,header,4,1) && transfer(c->socket,cipher,(int)n,1) && transfer(c->socket,tag,16,1);
}
static int receive_frame(Chat *c, unsigned char *message) {
    unsigned char header[4], cipher[LIMIT], tag[16];
    if (!transfer(c->socket,header,4,0)) return -1;
    unsigned int n=((unsigned int)header[0]<<24)|((unsigned int)header[1]<<16)|((unsigned int)header[2]<<8)|header[3];
    if (!n || n>LIMIT || c->received==UINT64_MAX) return -1;
    if (!transfer(c->socket,cipher,(int)n,0) || !transfer(c->socket,tag,16,0)) return -1;
    if (!crypt_frame(c->rx,c->received++,header,cipher,n,message,tag,0)) return -1;
    return (int)n;
}
static DWORD WINAPI receiver(LPVOID argument) {
    Chat *c=argument;
    unsigned char message[LIMIT];
    for (;;) {
        int n=receive_frame(c,message);
        if (n<0) { fprintf(stderr,"\nConnection closed or authentication failed.\n"); ExitProcess(1); }
        if (message[0]==2 && n==1) { puts("\nFriend left the chat."); ExitProcess(0); }
        if (message[0]!=1) { fprintf(stderr,"Invalid message type.\n"); ExitProcess(1); }
        fputs("\nfriend> ",stdout);
        /* Suppress terminal escape/control sequences received from the network. */
        for(int i=1;i<n;i++) putchar(message[i]>=32 && message[i]!=127 ? message[i] : ' ');
        fputs("\nyou> ",stdout); fflush(stdout);
    }
}
static void read_key(const char *path, unsigned char key[32]) {
    FILE *f=fopen(path,"rb");
    if (!f) die("Cannot open key file.");
    size_t n=fread(key,1,32,f); int extra=fgetc(f); fclose(f);
    if(n!=32 || extra!=EOF) die("Key file must contain exactly 32 random bytes; use keygen.");
}
static SOCKET connect_peer(const char *host, const char *port, int listening) {
    struct addrinfo hints={0}, *list=NULL, *a;
    SOCKET s=INVALID_SOCKET;
    hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM; hints.ai_protocol=IPPROTO_TCP;
    if(getaddrinfo(host,port,&hints,&list)) die("Cannot resolve host/address.");
    for(a=list;a;a=a->ai_next) {
        s=socket(a->ai_family,a->ai_socktype,a->ai_protocol);
        if(s==INVALID_SOCKET) continue;
        if(listening) {
            BOOL exclusive=TRUE;
            setsockopt(s,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,(char *)&exclusive,sizeof(exclusive));
            if(bind(s,a->ai_addr,(int)a->ai_addrlen)==0 && listen(s,1)==0) break;
        } else if(connect(s,a->ai_addr,(int)a->ai_addrlen)==0) break;
        closesocket(s); s=INVALID_SOCKET;
    }
    freeaddrinfo(list);
    if(s==INVALID_SOCKET) die("Could not bind/connect. Check address, port, and firewall.");
    if(listening) {
        printf("Waiting on %s:%s ...\n",host,port); fflush(stdout);
        SOCKET peer=accept(s,NULL,NULL); closesocket(s); s=peer;
        if(s==INVALID_SOCKET) die("Accept failed.");
    }
    return s;
}
int main(int argc,char **argv) {
    unsigned char master[32];
    setvbuf(stdout,NULL,_IONBF,0);
    if(argc==3 && !strcmp(argv[1],"keygen")) {
        random_bytes(master,32);
        HANDLE f=CreateFileA(argv[2],GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        if(f==INVALID_HANDLE_VALUE) die("Cannot create key file (will not overwrite an existing file).");
        DWORD written=0;
        BOOL ok=WriteFile(f,master,32,&written,NULL); CloseHandle(f); SecureZeroMemory(master,32);
        if(!ok || written!=32) die("Key write failed.");
        puts("Key created. Share this file privately with your friend; keep it secret."); return 0;
    }
    if(argc!=5 || (strcmp(argv[1],"listen") && strcmp(argv[1],"connect"))) {
        puts("PeerChat - encrypted two-person C chat for Windows\n"
             "  peerchat keygen friend.key\n"
             "  peerchat listen 0.0.0.0 7777 friend.key\n"
             "  peerchat connect HOST 7777 friend.key\n"
             "Type /quit to leave. See README.md for security limitations."); return 1;
    }
    char *end; long port=strtol(argv[3],&end,10);
    if(!*argv[3] || *end || port<1 || port>65535) die("Port must be 1-65535.");
    read_key(argv[4],master);
    WSADATA wsa; if(WSAStartup(MAKEWORD(2,2),&wsa)) die("Winsock startup failed.");
    int listening=!strcmp(argv[1],"listen");
    Chat c={0}; c.socket=connect_peer(argv[2],argv[3],listening);
    DWORD timeout=15000;
    setsockopt(c.socket,SOL_SOCKET,SO_RCVTIMEO,(char *)&timeout,sizeof(timeout));
    setsockopt(c.socket,SOL_SOCKET,SO_SNDTIMEO,(char *)&timeout,sizeof(timeout));
    unsigned char local[36]={'P','C','0','1'}, remote[36], challenges[64];
    random_bytes(local+4,32);
    if(!transfer(c.socket,local,36,1) || !transfer(c.socket,remote,36,0) || memcmp(remote,"PC01",4)) die("Handshake failed.");
    memcpy(challenges,listening?remote+4:local+4,32);
    memcpy(challenges+32,listening?local+4:remote+4,32);
    c.tx=derive(master,challenges,(unsigned char)(listening?1:0));
    c.rx=derive(master,challenges,(unsigned char)(listening?0:1));
    SecureZeroMemory(master,32);
    unsigned char proof[LIMIT];
    if(!send_frame(&c,(const unsigned char *)"ready",5) || receive_frame(&c,proof)!=5 || memcmp(proof,"ready",5)) die("Authentication failed: wrong key or altered connection.");
    timeout=0; setsockopt(c.socket,SOL_SOCKET,SO_RCVTIMEO,(char *)&timeout,sizeof(timeout));
    puts("Encrypted connection authenticated with shared key. Type /quit to leave.");
    HANDLE thread=CreateThread(NULL,0,receiver,&c,0,NULL);
    if(!thread) die("Cannot start receiver.");
    unsigned char message[LIMIT]; message[0]=1;
    for(;;) {
        fputs("you> ",stdout);
        if(!fgets((char *)message+1,LIMIT-1,stdin)) break;
        size_t n=strlen((char *)message+1);
        if(n && message[n]!='\n' && !feof(stdin)) {
            int ch; while((ch=getchar())!='\n' && ch!=EOF) {}
            puts("Message too long (maximum 4093 bytes). Try a shorter message."); continue;
        }
        while(n && (message[n]=='\n' || message[n]=='\r')) message[n--]=0;
        if(!strcmp((char *)message+1,"/quit")) break;
        if(n && !send_frame(&c,message,(unsigned int)n+1)) die("Send failed.");
    }
    unsigned char goodbye=2; send_frame(&c,&goodbye,1);
    /* Process exit releases the socket and receiver, including a blocked receive. */
    CloseHandle(thread);
    return 0;
}

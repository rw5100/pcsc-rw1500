#include "ifdhandler.h"
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#define LOAD(name) __typeof__(&name) p_##name; do { void *symbol=dlsym(module,#name); if(!symbol) { fprintf(stderr,"missing %s\n",#name); return 1; } memcpy(&p_##name,&symbol,sizeof(symbol)); } while(0)
#define CALL(name,...) do { rc=p_##name(__VA_ARGS__); printf(#name ": %ld\n",(long)rc); if(rc!=IFD_SUCCESS) goto end; } while(0)
int main(int argc,char **argv) {
    if(argc!=2) { fprintf(stderr,"usage: %s BUNDLE_EXECUTABLE\n",argv[0]); return 2; }
    void *module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!module) { fprintf(stderr,"%s\n",dlerror()); return 1; }
    LOAD(IFDHCreateChannelByName); LOAD(IFDHCloseChannel); LOAD(IFDHICCPresence);
    LOAD(IFDHPowerICC); LOAD(IFDHGetCapabilities); LOAD(IFDHSetProtocolParameters); LOAD(IFDHTransmitToICC);
    int opened=0,failed=1; RESPONSECODE rc;
    CALL(IFDHCreateChannelByName,0,"usb:04dd/9259"); opened=1;
    rc=p_IFDHICCPresence(0); printf("IFDHICCPresence: %ld\n",(long)rc); if(rc!=IFD_ICC_PRESENT) goto end;
    unsigned char atr[MAX_ATR_SIZE]; DWORD length=sizeof(atr);
    CALL(IFDHPowerICC,0,IFD_POWER_UP,atr,&length);
    printf("ATR:"); for(DWORD i=0;i<length;i++) printf(" %02X",atr[i]); puts("");
    unsigned char cached[MAX_ATR_SIZE]; DWORD n=sizeof(cached);
    CALL(IFDHGetCapabilities,0,TAG_IFD_ATR,&n,cached);
    if(n!=length || memcmp(atr,cached,n)) goto end;
    CALL(IFDHSetProtocolParameters,0,SCARD_PROTOCOL_T1,0,0,0,0);
    unsigned char apdu[]={0,0xa4,0,0x0c,2,0x3f,0},reply[258];
    for(unsigned i=0;i<3;i++) {
        SCARD_IO_HEADER send={1,sizeof(send)},recv={0}; n=sizeof(reply);
        CALL(IFDHTransmitToICC,0,send,apdu,sizeof(apdu),reply,&n,&recv);
        if(n<2 || recv.Protocol!=1) goto end;
        printf("APDU:"); for(DWORD j=0;j<n;j++) printf(" %02X",reply[j]); puts("");
    }
    failed=0;
end:
    if(opened) { rc=p_IFDHCloseChannel(0); printf("IFDHCloseChannel: %ld\n",(long)rc); if(rc) failed=1; }
    dlclose(module); return failed;
}

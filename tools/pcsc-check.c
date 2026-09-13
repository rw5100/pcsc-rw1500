#ifdef __APPLE__
#include <PCSC/wintypes.h>
#include <PCSC/winscard.h>
#else
#include <winscard.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int result(const char *op,LONG rc) {
    printf("%s: 0x%08lX\n",op,(unsigned long)(unsigned int)rc);
    return rc!=SCARD_S_SUCCESS;
}
static void bytes(const char *name,const unsigned char *p,size_t n) {
    printf("%s:",name); for(size_t i=0;i<n;i++) printf(" %02X",p[i]); puts("");
}
int main(int argc,char **argv) {
    SCARDCONTEXT context; SCARDHANDLE card=0; LONG rc; int failed=1;
    char *names=NULL; DWORD size=0,protocol;
    if(argc>2) { fprintf(stderr,"usage: %s [exact reader name]\n",argv[0]); return 2; }
    rc=SCardEstablishContext(SCARD_SCOPE_SYSTEM,NULL,NULL,&context);
    if(result("SCardEstablishContext",rc)) return 1;
    rc=SCardListReaders(context,NULL,NULL,&size);
    if(result("SCardListReaders(size)",rc)) goto end;
    names=calloc(size?size:1,1); if(!names) goto end;
    rc=SCardListReaders(context,NULL,names,&size);
    if(result("SCardListReaders",rc)) goto end;
    char *selected=NULL; unsigned matches=0;
    for(char *p=names;*p;p+=strlen(p)+1) {
        printf("reader: %s\n",p);
        if((argc==2 && !strcmp(p,argv[1])) || (argc==1 && strstr(p,"RW5100"))) { selected=p; matches++; }
    }
    if(matches!=1) { fprintf(stderr,"Expected one selected RW5100 reader, found %u\n",matches); goto end; }
    rc=SCardConnect(context,selected,SCARD_SHARE_SHARED,SCARD_PROTOCOL_T0|SCARD_PROTOCOL_T1,&card,&protocol);
    if(result("SCardConnect",rc)) goto end;
    char reader[512]; DWORD readerlen=sizeof(reader),state,active,atrlen=64; unsigned char atr[64];
    rc=SCardStatus(card,reader,&readerlen,&state,&active,atr,&atrlen);
    if(result("SCardStatus",rc)) goto end;
    printf("protocol: %lu state: %lu\n",(unsigned long)active,(unsigned long)state); bytes("ATR",atr,atrlen);
    rc=SCardBeginTransaction(card); if(result("SCardBeginTransaction",rc)) goto end;
    const unsigned char apdu[]={0,0xa4,0,0x0c,2,0x3f,0};
    for(unsigned i=0;i<3;i++) {
        unsigned char reply[258]; DWORD length=sizeof(reply);
        SCARD_IO_REQUEST send={protocol,sizeof(send)},recv={0,sizeof(recv)};
        rc=SCardTransmit(card,&send,apdu,sizeof(apdu),&recv,reply,&length);
        if(result("SCardTransmit",rc)) break;
        bytes("APDU response",reply,length);
        if(length<2) { rc=SCARD_F_COMM_ERROR; break; }
    }
    LONG endrc=SCardEndTransaction(card,SCARD_LEAVE_CARD);
    if(result("SCardEndTransaction",endrc) || rc!=SCARD_S_SUCCESS) goto end;
    failed=0;
end:
    if(card && result("SCardDisconnect",SCardDisconnect(card,SCARD_UNPOWER_CARD))) failed=1;
    free(names);
    if(result("SCardReleaseContext",SCardReleaseContext(context))) failed=1;
    return failed;
}

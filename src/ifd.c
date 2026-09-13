#include "ifdhandler.h"
#include "rw5100.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

#define READERS 16
#define IO_TIMEOUT 5000
/* One lock also serializes the resource manager's presence polling with I/O. */
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static struct reader {
    rw_device *device;
    int allocated;
    uint64_t retry_at;
    DWORD lun;
    rw_device_info address;
    uint8_t atr[MAX_ATR_SIZE];
    size_t atr_len;
    int protocol;
} readers[READERS];
static struct reader *find(DWORD lun) {
    for(unsigned i=0;i<READERS;i++)
        if(readers[i].allocated && readers[i].lun==lun) return &readers[i];
    return NULL;
}
static void invalidate(struct reader *r) {
    memset(r->atr,0,sizeof(r->atr)); r->atr_len=0; r->protocol=-1;
}
static RESPONSECODE error(int e) {
    switch(e) {
    case RW_OK:return IFD_SUCCESS;
    case RW_ERROR_NO_CARD:return IFD_ICC_NOT_PRESENT;
    case RW_ERROR_NO_DEVICE:case RW_ERROR_DISCONNECTED:return IFD_NO_SUCH_DEVICE;
    case RW_ERROR_TIMEOUT:return IFD_RESPONSE_TIMEOUT;
    case RW_ERROR_BUFFER:return IFD_ERROR_INSUFFICIENT_BUFFER;
    case RW_ERROR_UNSUPPORTED:return IFD_NOT_SUPPORTED;
    default:return IFD_COMMUNICATION_ERROR;
    }
}
/* A transport failure requires a new handle, never an APDU retry. */
static uint64_t now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
static int presence_status(struct reader *r,int *state) {
    int result=r->device?rw_status(r->device,state,IO_TIMEOUT):RW_ERROR_STATE;
    if(result!=RW_ERROR_STATE) return result;
    invalidate(r);
    if(now_ms()<r->retry_at) return result;
    rw_close(r->device); r->device=NULL;
    result=rw_open(&r->device,&r->address);
    r->retry_at=now_ms()+1000;
    syslog(result?LOG_ERR:LOG_NOTICE,"rw5100 reopen lun=%u bus=%u address=%u result=%d",
           (unsigned)r->lun,r->address.bus,r->address.address,result);
    if(!result) result=rw_status(r->device,state,IO_TIMEOUT);
    return result;
}
/* Parse only known resource-manager USB names. Never ignore an unknown suffix. */
static int selector(const char *name,rw_device_info *out,int *exact) {
    unsigned vid,pid,bus,addr,iface; int n=0,end=0;
    *exact=0;
#ifdef __APPLE__
    /* macOS passes the matching plist friendly name, not a USB address. */
    if(name && !strcmp(name,"RW5100 USB Smart Card Reader")) return 1;
#endif
    if(!name || sscanf(name,"usb:%4x/%4x%n",&vid,&pid,&n)!=2 ||
       vid!=0x04dd || pid!=0x9259) return 0;
    if(!name[n]) return 1;
    if(sscanf(name+n,":libusb-1.0:%u:%u:%u%n",&bus,&addr,&iface,&end)!=3 || name[n+end]) {
        end=0;
        if(sscanf(name+n,":libudev:%u:/dev/bus/usb/%u/%u%n",&iface,&bus,&addr,&end)!=3 || name[n+end])
            return 0;
    }
    if(bus>255 || addr>255 || iface!=0) return 0;
    *out=(rw_device_info){(uint8_t)bus,(uint8_t)addr,0x04dd,0x9259};
    *exact=1; return 1;
}
static RESPONSECODE open_reader(DWORD lun,const char *name) {
    if((lun&0xffffu)!=0 || find(lun)) return IFD_COMMUNICATION_ERROR;
    rw_device_info address={0}; int exact;
    if(!selector(name,&address,&exact)) return IFD_NO_SUCH_DEVICE;
    struct reader *r=NULL;
    for(unsigned i=0;i<READERS;i++) if(!readers[i].allocated) { r=&readers[i]; break; }
    if(!r) return IFD_COMMUNICATION_ERROR;
    if(!exact) {
        size_t count=1;
        int e=rw_enumerate(&address,&count);
        if(e || count!=1) return e?error(e):IFD_NO_SUCH_DEVICE;
    }
    for(unsigned i=0;i<READERS;i++)
        if(readers[i].allocated && readers[i].address.bus==address.bus &&
           readers[i].address.address==address.address) return IFD_COMMUNICATION_ERROR;
    rw_device *device=NULL; int e=rw_open(&device,&address);
    if(e) return error(e);
    r->allocated=1; r->device=device; r->address=address; r->lun=lun; invalidate(r);
    return IFD_SUCCESS;
}
__attribute__((visibility("default"))) RESPONSECODE IFDHCreateChannelByName(DWORD lun,LPSTR name) {
    pthread_mutex_lock(&lock);
    RESPONSECODE e=open_reader(lun,name);
    pthread_mutex_unlock(&lock); return e;
}
__attribute__((visibility("default"))) RESPONSECODE IFDHCreateChannel(DWORD lun,DWORD channel) {
    (void)channel;
    return IFDHCreateChannelByName(lun,"usb:04dd/9259");
}
__attribute__((visibility("default"))) RESPONSECODE IFDHCloseChannel(DWORD lun) {
    pthread_mutex_lock(&lock);
    struct reader *r=find(lun); RESPONSECODE e=IFD_NO_SUCH_DEVICE;
    if(r) {
        int state; int result=r->device?rw_status(r->device,&state,IO_TIMEOUT):RW_OK;
        e=result==RW_ERROR_NO_CARD?IFD_SUCCESS:error(result);
        rw_close(r->device); memset(r,0,sizeof(*r));
    }
    pthread_mutex_unlock(&lock); return e;
}
__attribute__((visibility("default"))) RESPONSECODE IFDHICCPresence(DWORD lun) {
    pthread_mutex_lock(&lock);
    struct reader *r=find(lun); RESPONSECODE e=IFD_NO_SUCH_DEVICE;
    if(r) {
        int status=RW_CARD_ABSENT; int result=presence_status(r,&status);
        e=result?error(result):(status==RW_CARD_ABSENT?IFD_ICC_NOT_PRESENT:IFD_ICC_PRESENT);
        if(result || status!=RW_CARD_POWERED) invalidate(r);
    }
    pthread_mutex_unlock(&lock); return e;
}
__attribute__((visibility("default"))) RESPONSECODE IFDHGetCapabilities(DWORD lun,DWORD tag,PDWORD length,PUCHAR value) {
    if(!length) return IFD_COMMUNICATION_ERROR;
    DWORD capacity=*length; *length=0;
    pthread_mutex_lock(&lock);
    struct reader *r=find(lun); RESPONSECODE e=IFD_NO_SUCH_DEVICE;
    if(r) {
        uint8_t byte=0; const uint8_t *data=&byte; size_t n=1;
        e=IFD_SUCCESS;
        switch(tag) {
        case TAG_IFD_ATR:data=r->atr; n=r->atr_len; break;
        case TAG_IFD_SLOTS_NUMBER:case TAG_IFD_THREAD_SAFE:byte=1; break;
        case TAG_IFD_SIMULTANEOUS_ACCESS:byte=READERS; break;
        case TAG_IFD_SLOTNUM:case TAG_IFD_SLOT_THREAD_SAFE:byte=0; break;
        default:e=IFD_ERROR_TAG; break;
        }
        if(e==IFD_SUCCESS) {
            *length=(DWORD)n;
            if(capacity<n || (!value && n)) e=IFD_ERROR_INSUFFICIENT_BUFFER;
            else if(n) memcpy(value,data,n);
        }
    }
    pthread_mutex_unlock(&lock); return e;
}
__attribute__((visibility("default"))) RESPONSECODE IFDHSetCapabilities(DWORD lun,DWORD tag,DWORD length,PUCHAR value) {
    (void)tag; (void)length; (void)value;
    pthread_mutex_lock(&lock);
    RESPONSECODE e=find(lun)?IFD_ERROR_TAG:IFD_NO_SUCH_DEVICE;
    pthread_mutex_unlock(&lock); return e;
}
__attribute__((visibility("default"))) RESPONSECODE IFDHPowerICC(DWORD lun,DWORD action,PUCHAR atr,PDWORD length) {
    if(!length) return IFD_COMMUNICATION_ERROR;
    *length=0;
    pthread_mutex_lock(&lock);
    struct reader *r=find(lun); RESPONSECODE e=IFD_NO_SUCH_DEVICE;
    if(r) {
        if(action==IFD_POWER_DOWN) {
            invalidate(r);
            int state; int result=rw_status(r->device,&state,IO_TIMEOUT);
            e=result==RW_ERROR_NO_CARD?IFD_SUCCESS:error(result);
        } else if(action==IFD_POWER_UP || action==IFD_RESET) {
            /* The IFD caller supplies MAX_ATR_SIZE bytes; macOS initializes
             * AtrLength to zero and uses it only as an output. */
            if(!atr) e=IFD_COMMUNICATION_ERROR;
            else {
                int state=RW_CARD_ABSENT; invalidate(r);
                int result=rw_status(r->device,&state,IO_TIMEOUT);
                size_t n=sizeof(r->atr);
                if(!result) result=rw_reset(r->device,action==IFD_RESET,
                                           r->atr,&n,IO_TIMEOUT);
                if(!result) { r->atr_len=n; memcpy(atr,r->atr,n); *length=(DWORD)n; }
                else invalidate(r);
                syslog(result?LOG_ERR:LOG_NOTICE,
                       "rw5100 PowerICC lun=%u action=%u state=%d warm=%d result=%d atr_length=%zu",
                       (unsigned)lun,(unsigned)action,state,action==IFD_RESET,result,n);
                e=error(result);
            }
        } else e=IFD_NOT_SUPPORTED;
    }
    pthread_mutex_unlock(&lock); return e;
}
__attribute__((visibility("default"))) RESPONSECODE IFDHSetProtocolParameters(DWORD lun,DWORD protocol,UCHAR flags,
                                      UCHAR pts1,UCHAR pts2,UCHAR pts3) {
    (void)pts1; (void)pts2; (void)pts3;
    if(protocol!=SCARD_PROTOCOL_T0 && protocol!=SCARD_PROTOCOL_T1) return IFD_PROTOCOL_NOT_SUPPORTED;
    if(flags) return IFD_ERROR_PTS_FAILURE;
    pthread_mutex_lock(&lock);
    struct reader *r=find(lun); RESPONSECODE e=IFD_NO_SUCH_DEVICE;
    if(r) {
        int p=protocol==SCARD_PROTOCOL_T0?0:1;
        int result=rw_set_protocol(r->device,p,IO_TIMEOUT);
        if(!result) r->protocol=p;
        else invalidate(r);
        e=result==RW_ERROR_UNSUPPORTED?IFD_PROTOCOL_NOT_SUPPORTED:error(result);
    }
    pthread_mutex_unlock(&lock); return e;
}
__attribute__((visibility("default"))) RESPONSECODE IFDHTransmitToICC(DWORD lun,SCARD_IO_HEADER send,PUCHAR tx,DWORD txlen,
                              PUCHAR rx,PDWORD rxlen,PSCARD_IO_HEADER recv) {
    if(!rxlen) return IFD_COMMUNICATION_ERROR;
    size_t n=*rxlen; *rxlen=0;
    if(recv) memset(recv,0,sizeof(*recv));
    if(!tx || !txlen || !rx || !recv) return IFD_COMMUNICATION_ERROR;
    pthread_mutex_lock(&lock);
    struct reader *r=find(lun); RESPONSECODE e=IFD_NO_SUCH_DEVICE;
    if(r) {
        /* Transmit PCI uses 0/1, unlike SetProtocolParameters' 1/2 masks. */
        if(send.Protocol>1) e=IFD_PROTOCOL_NOT_SUPPORTED;
        else {
            int result=RW_OK;
            if(r->protocol!=(int)send.Protocol) {
                result=rw_set_protocol(r->device,(int)send.Protocol,IO_TIMEOUT);
                if(!result) r->protocol=(int)send.Protocol;
            }
            if(!result) result=rw_transmit(r->device,tx,(size_t)txlen,rx,&n,IO_TIMEOUT);
            if(!result) { *rxlen=(DWORD)n; recv->Protocol=send.Protocol; recv->Length=sizeof(*recv); }
            else if(result!=RW_ERROR_BUFFER) invalidate(r);
            e=error(result);
        }
    }
    pthread_mutex_unlock(&lock); return e;
}
__attribute__((visibility("default"))) RESPONSECODE IFDHControl(DWORD lun,DWORD code,PUCHAR tx,DWORD txlen,
                        PUCHAR rx,DWORD rxlen,LPDWORD returned) {
    (void)code; (void)tx; (void)txlen; (void)rx; (void)rxlen;
    if(!returned) return IFD_COMMUNICATION_ERROR;
    *returned=0;
    pthread_mutex_lock(&lock);
    RESPONSECODE e=find(lun)?IFD_NOT_SUPPORTED:IFD_NO_SUCH_DEVICE;
    pthread_mutex_unlock(&lock); return e;
}

#include "ifdhandler.h"
#include "rw5100.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
struct rw_device { int id; };
static struct rw_device device;
static int present=RW_CARD_PRESENT,tx_error,transmits,selected=-1,closed;
int rw_enumerate(rw_device_info *out,size_t *n) { CHECK(*n>=1); *n=1; *out=(rw_device_info){2,3,0x04dd,0x9259}; return 0; }
int rw_open(rw_device **out,const rw_device_info *s) { CHECK(s->bus==2 && s->address==3); *out=&device; return 0; }
void rw_close(rw_device *d) { CHECK(d==&device); closed++; }
int rw_status(rw_device *d,int *s,unsigned timeout) { (void)d; CHECK(timeout); *s=present; return 0; }
int rw_power_off(rw_device *d,unsigned timeout) { (void)d; (void)timeout; present=RW_CARD_PRESENT; return 0; }
int rw_reset(rw_device *d,int warm,uint8_t *atr,size_t *n,unsigned timeout) {
    (void)d; (void)warm; (void)timeout; CHECK(*n>=2); atr[0]=0x3b; atr[1]=0; *n=2; present=RW_CARD_POWERED; return 0;
}
int rw_set_protocol(rw_device *d,int p,unsigned timeout) { (void)d; (void)timeout; selected=p; return 0; }
int rw_transmit(rw_device *d,const uint8_t *tx,size_t tn,uint8_t *rx,size_t *rn,unsigned timeout) {
    (void)d; (void)tx; (void)tn; (void)timeout; transmits++;
    if(tx_error) return tx_error;
    CHECK(*rn>=2); rx[0]=0x6a; rx[1]=0x81; *rn=2; return 0;
}
int main(void) {
    CHECK(IFDHCreateChannelByName(1,"usb:04dd/9259")==IFD_COMMUNICATION_ERROR);
    CHECK(IFDHCreateChannelByName(0,"usb:1234/9259")==IFD_NO_SUCH_DEVICE);
    CHECK(IFDHCreateChannelByName(0,"usb:04dd/9259:unknown")==IFD_NO_SUCH_DEVICE);
    CHECK(IFDHCreateChannelByName(0,"usb:04dd/9259:libusb-1.0:2:3:0")==0);
    CHECK(IFDHCreateChannelByName(0x10000,"usb:04dd/9259")==IFD_COMMUNICATION_ERROR);
    CHECK(IFDHICCPresence(0)==IFD_ICC_PRESENT);
    UCHAR atr[MAX_ATR_SIZE],rx[8]; DWORD n=sizeof(atr);
    CHECK(IFDHPowerICC(0,IFD_POWER_UP,atr,&n)==0 && n==2);
    n=1; CHECK(IFDHGetCapabilities(0,TAG_IFD_ATR,&n,atr)==IFD_ERROR_INSUFFICIENT_BUFFER && n==2);
    CHECK(IFDHSetProtocolParameters(0,SCARD_PROTOCOL_T1,0,0,0,0)==0 && selected==1);
    CHECK(IFDHSetProtocolParameters(0,SCARD_PROTOCOL_T0,1,0,0,0)==IFD_ERROR_PTS_FAILURE);
    SCARD_IO_HEADER send={1,0},recv={0}; UCHAR tx[]={0,0xa4,0,0}; n=sizeof(rx);
    CHECK(IFDHTransmitToICC(0,send,tx,4,rx,&n,&recv)==0 && n==2 && rx[0]==0x6a && recv.Protocol==1);
    tx_error=RW_ERROR_BUFFER; n=sizeof(rx);
    CHECK(IFDHTransmitToICC(0,send,tx,4,rx,&n,&recv)==IFD_ERROR_INSUFFICIENT_BUFFER && n==0 && transmits==2);
    tx_error=RW_ERROR_TIMEOUT; n=sizeof(rx);
    CHECK(IFDHTransmitToICC(0,send,tx,4,rx,&n,&recv)==IFD_RESPONSE_TIMEOUT && n==0 && transmits==3);
    n=sizeof(atr); CHECK(IFDHGetCapabilities(0,TAG_IFD_ATR,&n,atr)==0 && n==0);
    present=RW_CARD_ABSENT; CHECK(IFDHICCPresence(0)==IFD_ICC_NOT_PRESENT);
    n=10; CHECK(IFDHControl(0,0,NULL,0,NULL,0,&n)==IFD_NOT_SUPPORTED && n==0);
    CHECK(IFDHCloseChannel(0)==0 && closed==1);
    CHECK(IFDHICCPresence(0)==IFD_NO_SUCH_DEVICE);
    CHECK(IFDHCreateChannelByName(0x10000,"usb:04dd/9259:libudev:0:/dev/bus/usb/002/003")==0);
    CHECK(IFDHCloseChannel(0x10000)==0);
    puts("IFD selection, ABI, lifecycle, buffer and no-replay tests passed"); return 0;
}

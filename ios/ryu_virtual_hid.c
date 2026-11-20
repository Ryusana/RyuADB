// RYU Virtual HID P1 -- standalone iOS/rootless research daemon.
// Purpose: kernel-registered VIRTUAL digitizer, never spoof the physical touchscreen senderID.
// Hardware validation on iOS 17.5 / arm64e is NOT complete. Run only on a test device/secure LAN.
#define _DARWIN_C_SOURCE 1
#include <CoreFoundation/CoreFoundation.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include "ryu_hid_report.h"

#define RYU_PORT 6017
static volatile sig_atomic_t running=1;
static pthread_mutex_t report_lock=PTHREAD_MUTEX_INITIALIZER;
static RyuReport current;
static void *user_device=NULL,*iokit_handle=NULL;
static void *(*pCreate)(CFAllocatorRef,CFDictionaryRef)=NULL;
static void (*pSchedule)(void*,CFRunLoopRef,CFStringRef)=NULL;
static int (*pHandle)(void*,uint8_t*,CFIndex)=NULL;
static char secret[33];

static void add_num(CFMutableDictionaryRef d, const char *key, int value) {
  CFStringRef k=CFStringCreateWithCString(kCFAllocatorDefault,key,kCFStringEncodingUTF8);
  CFNumberRef v=CFNumberCreate(kCFAllocatorDefault,kCFNumberIntType,&value);
  CFDictionarySetValue(d,k,v); CFRelease(k); CFRelease(v);
}
static void add_str(CFMutableDictionaryRef d,const char *key,const char *value) {
  CFStringRef k=CFStringCreateWithCString(kCFAllocatorDefault,key,kCFStringEncodingUTF8);
  CFStringRef v=CFStringCreateWithCString(kCFAllocatorDefault,value,kCFStringEncodingUTF8);
  CFDictionarySetValue(d,k,v); CFRelease(k); CFRelease(v);
}
static int load_hid(void) {
  iokit_handle=dlopen("/System/Library/Frameworks/IOKit.framework/IOKit",RTLD_NOW);
  if(!iokit_handle){fprintf(stderr,"ERR iokit_dl_open: %s\n",dlerror());return -1;}
  pCreate=(void*(*)(CFAllocatorRef,CFDictionaryRef))dlsym(iokit_handle,"IOHIDUserDeviceCreate");
  pSchedule=(void(*)(void*,CFRunLoopRef,CFStringRef))dlsym(iokit_handle,"IOHIDUserDeviceScheduleWithRunLoop");
  pHandle=(int(*)(void*,uint8_t*,CFIndex))dlsym(iokit_handle,"IOHIDUserDeviceHandleReport");
  if(!pCreate || !pSchedule || !pHandle){fprintf(stderr,"ERR missing IOHIDUserDevice symbol\n");return -2;}
  CFMutableDictionaryRef dict=CFDictionaryCreateMutable(kCFAllocatorDefault,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
  CFDataRef desc=CFDataCreate(kCFAllocatorDefault,RYU_HID_DESCRIPTOR,(CFIndex)sizeof(RYU_HID_DESCRIPTOR));
  CFDictionarySetValue(dict,CFSTR("ReportDescriptor"),desc);
  add_str(dict,"Product","RYU Virtual Touchscreen P1");
  add_str(dict,"Manufacturer","RYU Research");
  add_str(dict,"Transport","Virtual");
  add_num(dict,"VendorID",0x1212); add_num(dict,"ProductID",0x1717);
  add_num(dict,"PrimaryUsagePage",0x0d); add_num(dict,"PrimaryUsage",0x04);
  add_num(dict,"ReportInterval",8000); // request up to 125Hz; actual rate is input-driven
  user_device=pCreate(kCFAllocatorDefault,dict);
  CFRelease(desc); CFRelease(dict);
  if(!user_device){ fprintf(stderr,"ERR virtual HID creation rejected (check jailbreak entitlements/iOS support)\n"); return -3; }
  pSchedule(user_device,CFRunLoopGetMain(),kCFRunLoopDefaultMode);
  printf("HID_DEVICE_CREATED (kernel accepted registration; NOT proof of effective touch)\n");
  return 0;
}
static void all_virtual_up(void) {
  pthread_mutex_lock(&report_lock);
  ryu_report_reset(&current);
  if(user_device) { int rc=pHandle(user_device,(uint8_t*)&current,sizeof(current));
    printf("VIRTUAL_RELEASE result=0x%x\n",(unsigned)rc); }
  pthread_mutex_unlock(&report_lock);
}
static void *server(void *p) {
 (void)p;
 int s=socket(AF_INET,SOCK_STREAM,0); if(s<0) {perror("socket");return NULL;}
 int one=1; setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
 struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_port=htons(RYU_PORT);a.sin_addr.s_addr=INADDR_ANY;
 if(bind(s,(struct sockaddr*)&a,sizeof(a))!=0 || listen(s,1)!=0){perror("bind/listen");close(s);return NULL;}
 printf("LISTEN 0.0.0.0:%d ONLY ON A TRUSTED LAN. TOKEN=%s\n",RYU_PORT,secret);
 while(running){
   struct sockaddr_in peer; socklen_t plen=sizeof(peer); int c=accept(s,(struct sockaddr*)&peer,&plen);
   if(c<0){if(errno==EINTR)continue;break;}
   struct timeval timeout={3,0};setsockopt(c,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
   FILE *f=fdopen(c,"r+"); if(!f){close(c);continue;}
   setvbuf(f,NULL,_IONBF,0);
   char line[256]={0};
   // Authentication required; never accept raw commands before HELLO.
   if(!fgets(line,sizeof(line),f) || strncmp(line,"HELLO ",6)!=0 || strncmp(line+6,secret,32)!=0 || line[38]!='\n'){
      fputs("ERR AUTH\n",f); fclose(f); continue;
   }
   fputs("READY 414 896 5\n",f);
   while(running && fgets(line,sizeof(line),f)){
      int id,x,y,rc=-10;char cmd[12]={0};
      if(sscanf(line,"%11s",cmd)!=1)continue;
      if(strcmp(cmd,"PING")==0){ fputs("PONG\n",f);continue;}
      if(strcmp(cmd,"RESET")==0){all_virtual_up();fputs("OK RESET\n",f);continue;}
      if(strcmp(cmd,"DOWN") && strcmp(cmd,"MOVE") && strcmp(cmd,"UP")){fputs("ERR COMMAND\n",f);continue;}
      if(sscanf(line,"%11s %d %d %d",cmd,&id,&x,&y)!=4) {fputs("ERR FORMAT\n",f);continue;}
      pthread_mutex_lock(&report_lock);
      rc=ryu_report_set(&current,id,strcmp(cmd,"DOWN")==0?1:(strcmp(cmd,"MOVE")==0?2:0),x,y);
      if(rc==0){
        int hid_rc=pHandle(user_device,(uint8_t*)&current,(CFIndex)sizeof(current));
        if(hid_rc!=0) { fprintf(f,"ERR HID 0x%x\n",(unsigned)hid_rc); fprintf(stderr,"HID report failed: 0x%x\n",(unsigned)hid_rc); }
        else fputs("OK\n",f);
      }else fprintf(f,"ERR STATE %d\n",rc);
      pthread_mutex_unlock(&report_lock);
   }
   // Disconnect only affects VIRTUAL contacts; physical digitizer is never touched.
   all_virtual_up(); fclose(f);
 }
 close(s);return NULL;
}
int main(int argc, char **argv){
 setvbuf(stdout,NULL,_IONBF,0);
 printf("RYU Virtual HID P1 / iOS 17.5 experimental / foreground only\n");
 printf("WARNING: test on a non-critical screen; no claim of physical-finger independence yet.\n");
 signal(SIGPIPE,SIG_IGN);
 // SIGINT/SIGTERM default action terminates the user device and releases ONLY its contacts.
 uint8_t bytes[16]; arc4random_buf(bytes,sizeof(bytes));
 for(int i=0;i<16;i++)sprintf(secret+i*2,"%02x",bytes[i]);secret[32]=0;
 ryu_report_init(&current);
 if(argc>1 && strcmp(argv[1],"--report-test")==0){
   int rc=ryu_report_set(&current,0,1,200,300);
   printf("REPORT_TEST count=%u bytes=%zu rc=%d (no actual HID device created)\n",current.count,sizeof(current),rc);
   return rc==0?0:1;
 }
 if(load_hid()!=0)return 2;
 if(argc>1 && strcmp(argv[1],"--probe")==0){
   printf("PROBE_CREATED. Does not prove UIKit touch input.\n");
   if(user_device)CFRelease(user_device);
   return 0;
 }
 pthread_t th;
 if(pthread_create(&th,NULL,server,NULL)!=0){fprintf(stderr,"ERR server thread\n");return 3;}
 CFRunLoopRun(); // foreground service; stop with Ctrl+C/kill
 all_virtual_up();
 if(user_device)CFRelease(user_device);
 return 0;
}

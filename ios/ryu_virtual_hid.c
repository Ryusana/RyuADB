// RYU Virtual HID P1 -- standalone iOS/rootless research daemon.
// Purpose: kernel-registered VIRTUAL digitizer, never spoof the physical touchscreen senderID.
// Hardware validation on iOS 17.5 / arm64e is NOT complete. Run only on a test device/secure LAN.
#define _DARWIN_C_SOURCE 1
#include <CoreFoundation/CoreFoundation.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <netinet/in.h>
#include <mach/mach.h>
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
#define RYU_BUILD_STAMP "P1.4-ALT-HID-EVENT-PATH-20261121"
// Deliberately harmless, vendor-defined HID descriptor used to isolate
// a registration/permission problem from a touchscreen descriptor problem.
static const uint8_t RYU_BASIC_DESCRIPTOR[] = {
  0x06,0x00,0xff, 0x09,0x01, 0xa1,0x01,
  0x15,0x00, 0x26,0xff,0x00, 0x75,0x08,0x95,0x01,
  0x09,0x01,0x81,0x02,0xc0
};
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
// V08: no-touch registration-layer audit. No Apple service, registry property,
// jailbreak privilege setting, or screen event is modified. The only IOKit
// mutation is opening and immediately closing a user client if permitted.
typedef const struct __SecTask *RyuSecTaskRef;
static void audit_task_entitlements(void) {
  const char *path="/System/Library/Frameworks/Security.framework/Security";
  void *h=dlopen(path,RTLD_NOW|RTLD_LOCAL);
  if(!h){printf("SECTASK_LIBRARY=UNAVAILABLE reason=dlopen_failed\n");return;}
  RyuSecTaskRef (*task_create)(CFAllocatorRef)=(RyuSecTaskRef(*)(CFAllocatorRef))dlsym(h,"SecTaskCreateFromSelf");
  CFTypeRef (*copy_value)(RyuSecTaskRef,CFStringRef,CFErrorRef*)=
    (CFTypeRef(*)(RyuSecTaskRef,CFStringRef,CFErrorRef*))dlsym(h,"SecTaskCopyValueForEntitlement");
  if(!task_create || !copy_value){printf("SECTASK_API=UNAVAILABLE\n");dlclose(h);return;}
  RyuSecTaskRef task=task_create(kCFAllocatorDefault);
  if(!task){printf("SECTASK_CREATE=NULL\n");dlclose(h);return;}
  const char *keys[]={
    "platform-application",
    "com.apple.hid.manager.user-access-device",
    "com.apple.developer.hid.virtual.device",
    "com.apple.hid.system.user-access-service",
    "com.apple.hid.system.server-access"
  };
  for(size_t i=0;i<sizeof(keys)/sizeof(keys[0]);i++){
    CFStringRef key=CFStringCreateWithCString(kCFAllocatorDefault,keys[i],kCFStringEncodingUTF8);
    CFErrorRef err=NULL;
    CFTypeRef val=copy_value(task,key,&err);
    if(val){
      if(CFGetTypeID(val)==CFBooleanGetTypeID()){
        printf("SECTASK_RIGHT %s=%s\n",keys[i],CFBooleanGetValue((CFBooleanRef)val)?"TRUE":"FALSE");
      }else printf("SECTASK_RIGHT %s=NON_BOOLEAN\n",keys[i]);
      CFRelease(val);
    }else if(err){
      printf("SECTASK_RIGHT %s=ERROR code=%ld\n",keys[i],(long)CFErrorGetCode(err));
    }else printf("SECTASK_RIGHT %s=NOT_PRESENT\n",keys[i]);
    if(err)CFRelease(err);
    CFRelease(key);
  }
  CFRelease((CFTypeRef)task);
  dlclose(h);
}
// V09: query kernel code-signing state via csops(2). This does not alter flags.
// SecTask entitlement visibility alone does not tell us CS_PLATFORM_BINARY.
static void audit_kernel_codesign_state(void) {
  typedef int (*ryu_csops_fn)(pid_t,unsigned int,void*,size_t);
  ryu_csops_fn csops_fn=(ryu_csops_fn)dlsym(RTLD_DEFAULT,"csops");
  void *lib=NULL;
  if(!csops_fn){
    lib=dlopen("/usr/lib/system/libsystem_kernel.dylib",RTLD_NOW|RTLD_LOCAL);
    if(lib)csops_fn=(ryu_csops_fn)dlsym(lib,"csops");
  }
  if(!csops_fn){puts("CSOPS_STATUS=SYMBOL_UNAVAILABLE");if(lib)dlclose(lib);return;}
  uint32_t flags=0;
  errno=0;
  int rc=csops_fn(getpid(),0,&flags,sizeof(flags)); // CS_OPS_STATUS = 0
  if(rc!=0){printf("CSOPS_STATUS=FAILED errno=%d\n",errno);if(lib)dlclose(lib);return;}
  printf("CSOPS_FLAGS=0x%08x\n",flags);
  printf("CSOPS_CS_VALID=%s\n",(flags & 0x00000001u)?"TRUE":"FALSE");
  printf("CSOPS_CS_ADHOC=%s\n",(flags & 0x00000002u)?"TRUE":"FALSE");
  printf("CSOPS_CS_PLATFORM_BINARY=%s\n",(flags & 0x04000000u)?"TRUE":"FALSE");
  printf("CSOPS_CS_PLATFORM_PATH=%s\n",(flags & 0x08000000u)?"TRUE":"FALSE");
  printf("CSOPS_CS_DEBUGGED=%s\n",(flags & 0x10000000u)?"TRUE":"FALSE");
  printf("CSOPS_CS_SIGNED=%s\n",(flags & 0x20000000u)?"TRUE":"FALSE");
  if(lib)dlclose(lib);
}

// V09: print only a narrow whitelist of published IORegistry properties.
// No IORegistry property is changed; no user client is instantiated by this helper.
static void audit_hid_resource_registry(void *lib, uint32_t service) {
  CFTypeRef (*copyprop)(uint32_t,CFStringRef,CFAllocatorRef,uint32_t)=
    (CFTypeRef(*)(uint32_t,CFStringRef,CFAllocatorRef,uint32_t))dlsym(lib,"IORegistryEntryCreateCFProperty");
  if(!copyprop){puts("IOREG_PROPERTY_API=UNAVAILABLE");return;}
  const char *keys[]={"IOUserClientClass","IOUserClientEntitlements","IOClass","IOProviderClass"};
  for(size_t i=0;i<sizeof(keys)/sizeof(keys[0]);++i){
    CFStringRef key=CFStringCreateWithCString(kCFAllocatorDefault,keys[i],kCFStringEncodingUTF8);
    if(!key){continue;}
    CFTypeRef p=copyprop(service,key,kCFAllocatorDefault,0);
    if(!p){printf("IOREG_PROP %s=NOT_PRESENT\n",keys[i]);CFRelease(key);continue;}
    if(CFGetTypeID(p)==CFStringGetTypeID()){
      char buf[256]={0};
      if(CFStringGetCString((CFStringRef)p,buf,sizeof(buf),kCFStringEncodingUTF8))
        printf("IOREG_PROP %s=%s\n",keys[i],buf);
      else printf("IOREG_PROP %s=STRING_CONVERSION_FAILED\n",keys[i]);
    }else printf("IOREG_PROP %s=NON_STRING_TYPE\n",keys[i]);
    CFRelease(p); CFRelease(key);
  }
}

static void audit_iokit_user_client(void) {
  void *h=dlopen("/System/Library/Frameworks/IOKit.framework/IOKit",RTLD_NOW|RTLD_LOCAL);
  if(!h){printf("IOKIT_LIBRARY=UNAVAILABLE reason=dlopen_failed\n");return;}
  CFMutableDictionaryRef (*service_match)(const char *)=(CFMutableDictionaryRef(*)(const char *))dlsym(h,"IOServiceMatching");
  CFMutableDictionaryRef (*name_match)(const char *)=(CFMutableDictionaryRef(*)(const char *))dlsym(h,"IOServiceNameMatching");
  uint32_t (*get_service)(mach_port_t,CFDictionaryRef)=(uint32_t(*)(mach_port_t,CFDictionaryRef))dlsym(h,"IOServiceGetMatchingService");
  kern_return_t (*service_open)(uint32_t,mach_port_t,uint32_t,mach_port_t*)=
    (kern_return_t(*)(uint32_t,mach_port_t,uint32_t,mach_port_t*))dlsym(h,"IOServiceOpen");
  kern_return_t (*service_close)(mach_port_t)=(kern_return_t(*)(mach_port_t))dlsym(h,"IOServiceClose");
  kern_return_t (*obj_release)(uint32_t)=(kern_return_t(*)(uint32_t))dlsym(h,"IOObjectRelease");
  if(!service_match || !get_service || !service_open || !service_close || !obj_release){
    printf("IOKIT_REQUIRED_SYMBOLS=MISSING\n");dlclose(h);return;
  }
  // Published IOHIDFamily defines the only device user-client type as zero.
  // Matching is a read-only query of IORegistry.
  uint32_t service=get_service(MACH_PORT_NULL,service_match("IOHIDResource"));
  if(service)printf("IOHID_RESOURCE_MATCH=CLASS_FOUND\n");
  if(!service && name_match){
    service=get_service(MACH_PORT_NULL,name_match("IOHIDResource"));
    if(service)printf("IOHID_RESOURCE_MATCH=NAME_FOUND\n");
  }
  if(!service){printf("IOHID_RESOURCE_MATCH=NOT_FOUND\n");dlclose(h);return;}
  audit_hid_resource_registry(h,service);
  puts("IOHID_RESOURCE_OPEN_TYPE=0"); // published IOHIDResourceDeviceUserClient connection type
  mach_port_t connection=MACH_PORT_NULL;
  kern_return_t kr=service_open(service,mach_task_self(),0,&connection);
  printf("IOHID_RESOURCE_OPEN_RC=0x%08x\n",(unsigned)kr);
  if((uint32_t)kr==0xe00002e2u)puts("IOHID_RESOURCE_OPEN_REASON=kIOReturnNotPermitted");
  printf("IOHID_RESOURCE_OPEN=%s\n",kr==KERN_SUCCESS && connection!=MACH_PORT_NULL?"OK":"FAILED");
  if(connection!=MACH_PORT_NULL){
    kern_return_t close_rc=service_close(connection);
    printf("IOHID_RESOURCE_CLOSE_RC=0x%08x\n",(unsigned)close_rc);
  }
  obj_release(service);
  dlclose(h);
}
static int audit_kernel_registration(void) {
  printf("KERNEL_AUDIT_BEGIN build=%s uid=%ld euid=%ld\n",
      RYU_BUILD_STAMP,(long)getuid(),(long)geteuid());
  audit_task_entitlements();
  audit_kernel_codesign_state();
  audit_iokit_user_client();
  puts("KERNEL_AUDIT_END; no HID reports sent, no touch injected");
  return 0; // diagnostic: explicit result is in structured output
}


// V10: alternate HID EVENT-SYSTEM path survey, separate from the blocked
// IOHIDResourceDeviceUserClient. This probe NEVER creates or sends IOHIDEvents,
// never registers a virtual HID device and never hooks backboardd.
// A client reference or exported symbol does NOT establish event-dispatch rights.
static int audit_alternative_event_path(void) {
  puts("EVENT_PATH_AUDIT_BEGIN; no input generated");
  void *h=dlopen("/System/Library/Frameworks/IOKit.framework/IOKit",RTLD_NOW|RTLD_LOCAL);
  if(!h){puts("EVENT_PATH_IOKIT=UNAVAILABLE");return 2;}
  const char *names[]={
    "IOHIDEventSystemClientCreate",
    "IOHIDEventSystemClientCreateWithType",
    "IOHIDEventSystemClientCopyServices",
    "IOHIDEventSystemClientDispatchEvent",
    "IOHIDVirtualServiceClientCreate",
    "IOHIDVirtualServiceClientDispatchEvent",
    "IOHIDEventCreateDigitizerEvent",
    "IOHIDEventCreateDigitizerFingerEvent"
  };
  for(size_t i=0;i<sizeof(names)/sizeof(names[0]);++i){
    printf("EVENT_SYMBOL %s=%s\n",names[i],dlsym(h,names[i])?"AVAILABLE":"UNAVAILABLE");
  }
  CFTypeRef (*create_client)(CFAllocatorRef) =
    (CFTypeRef(*)(CFAllocatorRef))dlsym(h,"IOHIDEventSystemClientCreate");
  CFArrayRef (*copy_services)(CFTypeRef) =
    (CFArrayRef(*)(CFTypeRef))dlsym(h,"IOHIDEventSystemClientCopyServices");
  if(!create_client){puts("EVENT_SYSTEM_CLIENT=SKIPPED_NO_API");dlclose(h);return 0;}
  // Unlike IOHIDUserDeviceCreate, this is a standard HID event-system client.
  // We open it briefly and enumerate count only; we do not dispatch a touch.
  CFTypeRef client=create_client(kCFAllocatorDefault);
  if(!client){puts("EVENT_SYSTEM_CLIENT=CREATE_NULL");dlclose(h);return 0;}
  puts("EVENT_SYSTEM_CLIENT=CREATE_OK");
  if(copy_services){
    CFArrayRef services=copy_services(client);
    if(services){
      printf("EVENT_SYSTEM_SERVICES_COUNT=%ld\n",(long)CFArrayGetCount(services));
      CFRelease(services);
    }else puts("EVENT_SYSTEM_SERVICES=NOT_AVAILABLE");
  }else puts("EVENT_SYSTEM_SERVICES=SKIPPED_NO_API");
  CFRelease(client);
  puts("EVENT_PATH_AUDIT_END; no event sent; dispatch permission UNKNOWN; finger independence UNTESTED");
  dlclose(h);
  return 0;
}

static int load_hid(int basic) {
  iokit_handle=dlopen("/System/Library/Frameworks/IOKit.framework/IOKit",RTLD_NOW);
  if(!iokit_handle){fprintf(stderr,"ERR iokit_dl_open: %s\n",dlerror());return -1;}
  pCreate=(void*(*)(CFAllocatorRef,CFDictionaryRef))dlsym(iokit_handle,"IOHIDUserDeviceCreate");
  pSchedule=(void(*)(void*,CFRunLoopRef,CFStringRef))dlsym(iokit_handle,"IOHIDUserDeviceScheduleWithRunLoop");
  pHandle=(int(*)(void*,uint8_t*,CFIndex))dlsym(iokit_handle,"IOHIDUserDeviceHandleReport");
  if(!pCreate || !pSchedule || !pHandle){fprintf(stderr,"ERR missing IOHIDUserDevice symbol\n");return -2;}
  CFMutableDictionaryRef dict=CFDictionaryCreateMutable(kCFAllocatorDefault,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
  const uint8_t *descriptor=basic?RYU_BASIC_DESCRIPTOR:RYU_HID_DESCRIPTOR;
  size_t descriptor_size=basic?sizeof(RYU_BASIC_DESCRIPTOR):sizeof(RYU_HID_DESCRIPTOR);
  fprintf(stderr,"HID_PROBE_BEGIN build=%s kind=%s uid=%ld euid=%ld descriptor_bytes=%zu\n",
    RYU_BUILD_STAMP,basic?"vendor-basic":"touchscreen",(long)getuid(),(long)geteuid(),descriptor_size);
  CFDataRef desc=CFDataCreate(kCFAllocatorDefault,descriptor,(CFIndex)descriptor_size);
  CFDictionarySetValue(dict,CFSTR("ReportDescriptor"),desc);
  add_str(dict,"Product",basic?"RYU Virtual HID Registration Probe":"RYU Virtual Touchscreen P1");
  add_str(dict,"Manufacturer","RYU Research");
  add_str(dict,"Transport","Virtual");
  add_num(dict,"VendorID",0x1212); add_num(dict,"ProductID",0x1717);
  add_num(dict,"PrimaryUsagePage",basic?0xff00:0x0d); add_num(dict,"PrimaryUsage",basic?0x01:0x04);
  add_num(dict,"ReportInterval",8000); // request up to 125Hz; actual rate is input-driven
  errno=0;
  user_device=pCreate(kCFAllocatorDefault,dict);
  int create_errno=errno;
  CFRelease(desc); CFRelease(dict);
  if(!user_device){
    fprintf(stderr,"HID_PROBE_FAIL kind=%s uid=%ld euid=%ld errno=%d (errno is NOT a reliable CoreFoundation error code)\n",
      basic?"vendor-basic":"touchscreen",(long)getuid(),(long)geteuid(),create_errno);
    fprintf(stderr,"ERR IOHIDUserDeviceCreate returned NULL; verify effective signature/permissions and OS support.\n");
    return -3;
  }
  pSchedule(user_device,CFRunLoopGetMain(),kCFRunLoopDefaultMode);
  printf("HID_DEVICE_CREATED kind=%s (registration only; NOT proof of effective touch)\n",basic?"vendor-basic":"touchscreen");
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
 printf("BUILD_STAMP=%s\n", RYU_BUILD_STAMP);
 printf("WARNING: test on a non-critical screen; no claim of physical-finger independence yet.\n");
 signal(SIGPIPE,SIG_IGN);
 // SIGINT/SIGTERM default action terminates the user device and releases ONLY its contacts.
 uint8_t bytes[16]; arc4random_buf(bytes,sizeof(bytes));
 for(int i=0;i<16;i++)sprintf(secret+i*2,"%02x",bytes[i]);secret[32]=0;
 ryu_report_init(&current);
 if(argc>1 && strcmp(argv[1],"--audit-kernel")==0) return audit_kernel_registration();
 if(argc>1 && strcmp(argv[1],"--audit-event-path")==0) return audit_alternative_event_path();
 if(argc>1 && strcmp(argv[1],"--version")==0) {
   puts("VERSION_OK; no HID registration attempted");
   return 0;
 }
 if(argc>1 && strcmp(argv[1],"--report-test")==0){
   int rc=ryu_report_set(&current,0,1,200,300);
   printf("REPORT_TEST count=%u bytes=%zu rc=%d (no actual HID device created)\n",current.count,sizeof(current),rc);
   return rc==0?0:1;
 }
 int basic_probe=argc>1 && strcmp(argv[1],"--probe-basic")==0;
 int touch_probe=argc>1 && strcmp(argv[1],"--probe")==0;
 if(argc>1 && !basic_probe && !touch_probe) {
   fprintf(stderr,"ERR unknown option: %s\n",argv[1]);
   return 4;
 }
 if(load_hid(basic_probe)!=0)return 2;
 if(basic_probe || touch_probe){
   printf("PROBE_CREATED kind=%s. No HID input report sent; does not prove UIKit touch input.\n",basic_probe?"vendor-basic":"touchscreen");
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

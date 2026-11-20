#include <assert.h>
#include <stdio.h>
#include "ryu_hid_report.h"
int main(void){
  assert(sizeof(RyuContact)==6);assert(sizeof(RyuReport)==32);
  assert(RYU_HID_DESCRIPTOR[0]==0x05);
  RyuReport r; ryu_report_init(&r);assert(r.report_id==1 && r.count==0);
  assert(ryu_report_set(&r,0,1,20,20)==0 && r.count==1);
  assert(ryu_report_set(&r,1,1,400,850)==0 && r.count==2);
  assert(ryu_report_set(&r,0,2,21,20)==0 && r.count==2);
  assert(ryu_report_set(&r,0,0,21,20)==0 && r.count==1);
  assert((r.finger[1].flags&1)!=0); // UP slot0 MUST NOT cancel virtual slot1
  assert(ryu_report_set(&r,0,2,30,30)==-2);
  assert(ryu_report_set(&r,1,1,30,30)==-3);
  assert(ryu_report_set(&r,2,1,415,100)==-1);
  ryu_report_reset(&r);assert(r.count==0);
  puts("PASS: descriptor sizes, two virtual contacts, per-finger UP, guards, reset");
  puts("NOTE: no physical iPhone used, this does NOT prove physical+virtual concurrency");
  return 0;
}

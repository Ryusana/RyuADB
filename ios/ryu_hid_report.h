#ifndef RYU_HID_REPORT_H
#define RYU_HID_REPORT_H
#include <stdint.h>
#include <stddef.h>

#define RYU_MAX_CONTACTS 5
#define RYU_POINT_W 414
#define RYU_POINT_H 896
#define RYU_REPORT_ID 1

#if defined(_MSC_VER)
#pragma pack(push,1)
#define RYU_PACKED
#else
#define RYU_PACKED __attribute__((packed))
#endif

typedef struct RYU_PACKED { uint8_t flags, contact_id; uint16_t x, y; } RyuContact;
typedef struct RYU_PACKED { uint8_t report_id; RyuContact finger[RYU_MAX_CONTACTS]; uint8_t count; } RyuReport;
#if defined(_MSC_VER)
#pragma pack(pop)
#endif

// Each Finger collection: three 1-bit flags (tip,in-range,confidence),
// five padding bits, 8-bit contact ID, then 16-bit X and 16-bit Y.
#define RYU_FINGER_DESCRIPTOR \
  0x05,0x0d,0x09,0x22,0xa1,0x02, \
  0x09,0x42,0x09,0x32,0x09,0x47, \
  0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x03,0x81,0x02, \
  0x75,0x05,0x95,0x01,0x81,0x03, \
  0x09,0x51,0x15,0x00,0x26,0xff,0x00,0x75,0x08,0x95,0x01,0x81,0x02, \
  0x05,0x01,0x09,0x30,0x15,0x00,0x26,0x9e,0x01,0x75,0x10,0x95,0x01,0x81,0x02, \
  0x09,0x31,0x15,0x00,0x26,0x80,0x03,0x75,0x10,0x95,0x01,0x81,0x02, \
  0xc0

static const uint8_t RYU_HID_DESCRIPTOR[] = {
  0x05,0x0d, 0x09,0x04, 0xa1,0x01,
  0x85,RYU_REPORT_ID,
  RYU_FINGER_DESCRIPTOR, RYU_FINGER_DESCRIPTOR, RYU_FINGER_DESCRIPTOR,
  RYU_FINGER_DESCRIPTOR, RYU_FINGER_DESCRIPTOR,
  0x05,0x0d,0x09,0x54,0x15,0x00,0x25,RYU_MAX_CONTACTS,
  0x75,0x08,0x95,0x01,0x81,0x02,
  0xc0
};

static inline void ryu_report_init(RyuReport *r) {
    uint8_t *b=(uint8_t *)r;
    for(size_t i=0;i<sizeof(*r);i++) b[i]=0;
    r->report_id=RYU_REPORT_ID;
    for (int i=0;i<RYU_MAX_CONTACTS;i++) r->finger[i].contact_id=(uint8_t)(i+1);
}
static inline int ryu_report_set(RyuReport *r, int finger, int mode, int x, int y) {
    if(!r || finger<0 || finger>=RYU_MAX_CONTACTS || x<0 || x>RYU_POINT_W || y<0 || y>RYU_POINT_H) return -1;
    RyuContact *c=&r->finger[finger];
    // mode 1 DOWN, 2 MOVE, 0 UP. Every outgoing report is a full snapshot of VIRTUAL contacts only.
    if(mode==0) c->flags=0;
    else if(mode==1 || mode==2) {
       if(mode==2 && !(c->flags&1)) return -2; // MOVE without DOWN is forbidden
       if(mode==1 && (c->flags&1)) return -3; // DOWN twice is forbidden
       c->flags=0x07;
    } else return -4;
    c->x=(uint16_t)x; c->y=(uint16_t)y;
    int count=0; for(int i=0;i<RYU_MAX_CONTACTS;i++) if(r->finger[i].flags&1) ++count;
    r->count=(uint8_t)count;
    return 0;
}
static inline void ryu_report_reset(RyuReport *r) {
  ryu_report_init(r);
}

#endif

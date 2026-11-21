#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
int main(int argc,char**argv) {
    if(argc!=2 || strcmp(argv[1],"--once")!=0) {
        fputs("USAGE: ryucalctap --once (one opt-in notification only)\n",stderr);
        return 2;
    }
    CFNotificationCenterPostNotification(CFNotificationCenterGetDarwinNotifyCenter(),
        CFSTR("com.ryusana.calcprobe.tap.v12"),NULL,NULL,true);
    puts("RYU_V12_NOTIFICATION_POSTED (NOT an acknowledgment from Calculator)");
    return 0;
}

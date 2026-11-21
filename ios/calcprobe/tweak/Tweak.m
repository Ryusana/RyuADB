// RYU V14: Calculator-only UIKit delivery + before/after evidence.
// The tweak does nothing on load except register a Darwin notification.
// It sends exactly ONE down/up only when root sends the user-authorized request.
// Not intended for game use, no hooking of backboardd, no physical sender spoofing.
#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import <CoreFoundation/CoreFoundation.h>
#import <objc/message.h>
#import <objc/runtime.h>
#import <dlfcn.h>
#import <mach/mach_time.h>
#import <dispatch/dispatch.h>
#import <stdio.h>
#import <time.h>
#import <stdint.h>

@interface UIApplication (RYUPrivateEventRoute)
- (void)_enqueueHIDEvent:(CFTypeRef)event;
@end

static void ryu_log(const char *message) {
    NSString *path = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Caches/RYUCalcProbe.log"];
    FILE *f = fopen(path.fileSystemRepresentation, "a");
    if (f) {
        fprintf(f, "RYU_CALC_V12 t=%lld %s\n", (long long)time(NULL), message);
        fclose(f);
    }
    NSLog(@"RYU_CALC_V12 %s", message);
}

static UIWindow *ryu_front_window(void) {
    UIApplication *app = [UIApplication sharedApplication];
    for (UIScene *scene in app.connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]]) continue;
        if (scene.activationState != UISceneActivationStateForegroundActive) continue;
        for (UIWindow *window in ((UIWindowScene *)scene).windows)
            if (window.isKeyWindow && !window.hidden) return window;
    }
    UIWindow *legacy = app.keyWindow;
    if (legacy && !legacy.hidden) return legacy;
    return nil;
}

// Capture only the app-local Calculator window after an explicitly authorized
// test command. No background screen recording and no other app is hooked.
static void ryu_capture_calc(NSString *label, UIWindow *window) {
    if (![[NSBundle mainBundle].bundleIdentifier isEqualToString:@"com.apple.calculator"] || !window) return;
    CGSize size=window.bounds.size;
    if (size.width<10 || size.height<10 || size.width>1500 || size.height>2500) {
        ryu_log("SCREENSHOT_SKIP invalid_window_size"); return;
    }
    @autoreleasepool {
        UIGraphicsBeginImageContextWithOptions(size, YES, 1.0);
        BOOL ok=[window drawViewHierarchyInRect:window.bounds afterScreenUpdates:YES];
        UIImage *snapshot=ok ? UIGraphicsGetImageFromCurrentImageContext() : nil;
        NSData *png=snapshot ? UIImagePNGRepresentation(snapshot) : nil;
        UIGraphicsEndImageContext();
        if (!png || png.length<100 || png.length>8000000) {
            ryu_log("SCREENSHOT_FAIL render_or_size"); return;
        }
        NSString *leaf=[NSString stringWithFormat:@"RYUCalcProbe_%@.png",label];
        NSString *path=[NSHomeDirectory() stringByAppendingPathComponent:
                        [@"Library/Caches" stringByAppendingPathComponent:leaf]];
        BOOL written=[png writeToFile:path atomically:YES];
        // Record exact app-sandbox file path to avoid guessing its container on Windows.
        // The evidence collector accepts only fresh PNGs inside an app data container.
        char message[720];
        snprintf(message,sizeof(message),"SCREENSHOT_%s=%s bytes=%lu path=%s",[label UTF8String],
                 written?"SAVED":"WRITE_FAILED",(unsigned long)png.length,
                 path.fileSystemRepresentation);
        ryu_log(message);
    }
}

static void ryu_send_one_tap(void) {
    if (![[NSBundle mainBundle].bundleIdentifier isEqualToString:@"com.apple.calculator"]) return;
    UIWindow *window = ryu_front_window();
    if (!window) { ryu_log("ERROR NO_FOREGROUND_KEY_WINDOW"); return; }
    SEL getContext = sel_registerName("_contextId");
    if (![window respondsToSelector:getContext]) { ryu_log("ERROR NO_WINDOW_CONTEXT_SELECTOR"); return; }
    uint32_t context = ((uint32_t(*)(id,SEL))objc_msgSend)(window,getContext);
    if (!context) { ryu_log("ERROR ZERO_WINDOW_CONTEXT"); return; }
    UIApplication *app = [UIApplication sharedApplication];
    SEL enqueue = sel_registerName("_enqueueHIDEvent:");
    if (![app respondsToSelector:enqueue]) { ryu_log("ERROR ENQUEUE_SELECTOR_MISSING"); return; }

    void *hid = dlopen("/System/Library/Frameworks/IOKit.framework/IOKit", RTLD_NOW | RTLD_LOCAL);
    void *bks = dlopen("/System/Library/PrivateFrameworks/BackBoardServices.framework/BackBoardServices",RTLD_NOW | RTLD_LOCAL);
    if (!hid || !bks) { ryu_log("ERROR MISSING_FRAMEWORK"); if(hid)dlclose(hid); if(bks)dlclose(bks); return; }

    typedef CFTypeRef(*MakeHand)(CFAllocatorRef,uint64_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,double,double,double,double,double,unsigned char,unsigned char,uint32_t);
    typedef CFTypeRef(*MakeFinger)(CFAllocatorRef,uint64_t,uint32_t,uint32_t,uint32_t,double,double,double,double,double,unsigned char,unsigned char,uint32_t);
    typedef void(*Append)(CFTypeRef,CFTypeRef,uint32_t);
    typedef void(*SetInteger)(CFTypeRef,uint32_t,CFIndex);
    typedef void(*SetContext)(CFTypeRef,uint32_t,uint8_t,uint8_t,CFStringRef,double,float);
    MakeHand handfn=(MakeHand)dlsym(hid,"IOHIDEventCreateDigitizerEvent");
    MakeFinger fingerfn=(MakeFinger)dlsym(hid,"IOHIDEventCreateDigitizerFingerEvent");
    Append append=(Append)dlsym(hid,"IOHIDEventAppendEvent");
    SetInteger setint=(SetInteger)dlsym(hid,"IOHIDEventSetIntegerValue");
    SetContext setctx=(SetContext)dlsym(bks,"BKSHIDEventSetDigitizerInfo");
    if (!handfn || !fingerfn || !append || !setint || !setctx) {
        ryu_log("ERROR REQUIRED_EVENT_SYMBOL_MISSING"); dlclose(hid);dlclose(bks);return;
    }

    ryu_capture_calc(@"before", window);
    char note[220];
    snprintf(note,sizeof(note),"ROUTE_BEGIN context=%u coord_mode=WINDOW_POINTS norm_x=0.375 norm_y=0.700",context);
    ryu_log(note);
    // Previous x=0.5 was on the boundary between the two center keypad columns.
    // Probe UIKit hit-testing before sending any event; no accessibility or private data is stored.
    CGPoint planned = CGPointMake(CGRectGetWidth(window.bounds)*0.375,
                                  CGRectGetHeight(window.bounds)*0.700);
    UIView *hit = [window hitTest:planned withEvent:nil];
    NSString *hitName = hit ? NSStringFromClass([hit class]) : @"NONE";
    NSString *parentName = hit.superview ? NSStringFromClass([hit.superview class]) : @"NONE";
    char hitLog[360];
    snprintf(hitLog, sizeof(hitLog),
             "TARGET_HIT_TEST window=%.1fx%.1f planned=%.1f,%.1f view=%s parent=%s",
             CGRectGetWidth(window.bounds), CGRectGetHeight(window.bounds),
             planned.x, planned.y, [hitName UTF8String], [parentName UTF8String]);
    ryu_log(hitLog);
    // V14: UIKit reported the earlier normalized inputs as (0.4, 0.7) points.
    // Test window-point coordinates computed by hitTest, not 0..1 normalized values.
    // This is an app-only research experiment; physical finger independence untested.
    const uint32_t integrated = (11u<<16) + 25u;
    {
        const unsigned char touching=1;
        const uint64_t timestamp=mach_absolute_time();
        const uint32_t parentMask=2u; // changed Touch
        const uint32_t childMask=3u; // changed Range | Touch
        CFTypeRef hand=handfn(kCFAllocatorDefault,timestamp,3,0,0,parentMask,0,
                              0.0,0.0,0.0,0.0,0.0,touching,touching,0);
        CFTypeRef finger=fingerfn(kCFAllocatorDefault,timestamp,0x5151,0x5151,childMask,
                                  planned.x,planned.y,0.0,0.0,0.0,touching,touching,0);
        if (!hand || !finger) {
            if(hand)CFRelease(hand);
            if(finger)CFRelease(finger);
            ryu_log("ERROR HID_EVENT_CREATE_NULL");return;
        }
        setint(hand,integrated,1);
        append(hand,finger,0);
        setctx(hand,context,0,0,NULL,0.0,0.0);
        ((void(*)(id,SEL,CFTypeRef))objc_msgSend)(app,enqueue,hand);
        ryu_log("APP_ENQUEUE_DOWN_CALLED");
        CFRelease(finger);CFRelease(hand);
        {
            // Avoid blocking the main thread for 75 ms: the UP is queued later.
            // The single test still has a strict maximum of one down+up pair.
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 75000000),dispatch_get_main_queue(), ^{
                const uint64_t t2=mach_absolute_time();
                CFTypeRef up=handfn(kCFAllocatorDefault,t2,3,0,0,2u,0,
                                     0.0,0.0,0.0,0.0,0.0,0,0,0);
                CFTypeRef fup=fingerfn(kCFAllocatorDefault,t2,0x5151,0x5151,3u,
                                        planned.x,planned.y,0.0,0.0,0.0,0,0,0);
                if(up && fup) {
                    setint(up,integrated,1);
                    append(up,fup,0);
                    setctx(up,context,0,0,NULL,0.0,0.0);
                    ((void(*)(id,SEL,CFTypeRef))objc_msgSend)(app,enqueue,up);
                    ryu_log("APP_ENQUEUE_UP_CALLED");
                    // One after-frame, 240 ms after UP: observe UI visually
                    // without opening a new app or waiting for user screenshots.
                    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 240000000),
                                   dispatch_get_main_queue(), ^{
                        UIWindow *current=ryu_front_window();
                        if (current) ryu_capture_calc(@"after",current);
                        else ryu_log("SCREENSHOT_after=SKIP_NO_WINDOW");
                    });
                }else ryu_log("ERROR UP_EVENT_CREATE_NULL");
                if(fup)CFRelease(fup);
                if(up)CFRelease(up);
            });
        }
    }
    // Keep framework references open for delayed dispatch: these symbols are
    // also loaded by UIKit, but avoiding dlclose prevents any unload race.
}

// Observe actual UIKit touch delivery. Calls original implementation unmodified.
static void (*ryu_original_sendEvent)(id,SEL,UIEvent *) = NULL;
static void ryu_observe_sendEvent(id receiver, SEL command, UIEvent *event) {
    if (event.type == UIEventTypeTouches) {
        NSUInteger began=0, ended=0, moved=0;
        for (UITouch *touch in event.allTouches) {
            if (touch.phase == UITouchPhaseBegan) began++;
            if (touch.phase == UITouchPhaseEnded) ended++;
            if (touch.phase == UITouchPhaseMoved) moved++;
        }
        if (began || ended || moved) {
            char msg[180];
            UITouch *sample = [event.allTouches anyObject];
            UIWindow *win = sample.window;
            CGPoint point = win ? [sample locationInView:win] : CGPointZero;
            NSString *name = sample.view ? NSStringFromClass([sample.view class]) : @"NONE";
            snprintf(msg,sizeof(msg),"UIKIT_TOUCH_EVENT began=%lu ended=%lu moved=%lu pos=%.1f,%.1f view=%.65s",
                (unsigned long)began,(unsigned long)ended,(unsigned long)moved,
                point.x,point.y,[name UTF8String]);
            ryu_log(msg);
        }
    }
    if (ryu_original_sendEvent) ryu_original_sendEvent(receiver,command,event);
}

static void ryu_cb(CFNotificationCenterRef center,void *observer,CFStringRef name,const void *object,CFDictionaryRef info) {
    (void)center;(void)observer;(void)name;(void)object;(void)info;
    dispatch_async(dispatch_get_main_queue(), ^{ ryu_send_one_tap(); });
}
__attribute__((constructor)) static void ryu_init(void) {
    @autoreleasepool {
        if (![[NSBundle mainBundle].bundleIdentifier isEqualToString:@"com.apple.calculator"]) return;
        Method method=class_getInstanceMethod([UIApplication class],@selector(sendEvent:));
        if (method) {
            ryu_original_sendEvent=(void (*)(id,SEL,UIEvent *))
                method_setImplementation(method,(IMP)ryu_observe_sendEvent);
            ryu_log("UIKIT_EVENT_OBSERVER_INSTALLED");
        } else ryu_log("ERROR SEND_EVENT_SELECTOR_MISSING");
        CFNotificationCenterAddObserver(CFNotificationCenterGetDarwinNotifyCenter(),
            NULL,ryu_cb,CFSTR("com.ryusana.calcprobe.tap.v12"),NULL,
            CFNotificationSuspensionBehaviorDeliverImmediately);
        ryu_log("TWEAK_LOADED_LISTENER_READY; NO_AUTO_TOUCH");
    }
}

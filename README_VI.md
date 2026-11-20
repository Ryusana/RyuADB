# RYU DUAL TOUCH P1 — VIRTUAL HID (iPhone 11 Pro Max / iOS 17.5 / Dopamine)

## VÌ SAO P1 KHÁC ZXTOUCH?

Đây là **một mã nguồn thử nghiệm hoàn chỉnh gồm iOS HID device + socket server + Windows GUI**, không phải bản dựng lại ZXTouch. Thay vì gửi IOHIDEventSystemClientDispatchEvent với senderID mượn từ cảm ứng vật lý, `ios/ryu_virtual_hid.c` đăng ký một **IOHIDUserDevice riêng** (HID Usage Page=0x0D / Usage=0x04), rồi đưa các gói report vào thiết bị ảo đó. Chỉ quản lý 5 ngón ẢO, không đọc/chạm/sửa danh sách ngón thật. `UP`, `RESET`, disconnect chỉ thay đổi gói report của thiết bị ảo này.

**CHƯA THỂ KHẲNG ĐỊNH HAI NGÓN ĐỘC LẬP TRÊN IPHONE:** có bằng chứng phương pháp này hoạt động trên iPad jailbreak chạy iOS 12.5.x; **CHƯA có bằng chứng nó hoạt động trên iOS 17.5 arm64e / iPhone12,5 / Dopamine**. Không có iPhone thật hoặc Theos iOS SDK trong môi trường xây dựng hiện tại, nên chưa biên dịch được `.deb` trên iOS, chưa chứng minh iOS chấp nhận HID descriptor/entitlements và chưa đo FPS hay độ trễ. `HID_DEVICE_CREATED` hoặc `OK` chỉ có nghĩa kernel/driver chấp nhận khâu tương ứng, **KHÔNG chứng minh ngón thật được bảo toàn**.

## Thư mục

- `ios/ryu_virtual_hid.c`: chương trình trên iPhone. Đăng ký HID ảo, TCP port 6017, token 128-bit, nhận DOWN/MOVE/UP/RESET, phát report 32 byte (5 ngón), đóng phiên => nhả ngón ảo.
- `ios/ryu_hid_report.h`: HID descriptor và quản lý trạng thái ngón ảo. Tọa độ **điểm (points)** portrait 414×896.
- `ios/Makefile`, `ios/control`, `ios/entitlements.plist`: nguồn Theos rootless cho Dopamine. Các entitlement HID có thể vẫn cần hiệu chỉnh dựa trên kết quả thực tế iOS 17.
- `windows/ryu_usb_mux.py`: Apple USBMux, chọn đúng một thiết bị USB, Connect đến port 6017.
- `windows/ryu_windows_controller.py`: Tkinter Windows 10 không cần pip; lớp phủ mouse và TCP có xác thực. Hỗ trợ USBMux trước, Wi-Fi dự phòng. Video tiếp tục chiếu bằng **iPhoneMirror native USB/H.264**.
- `tests/`: test Windows, `ios/ryu_report_selftest.c`: test cấu trúc gói HID (chạy GCC/Clang trên PC).
- `.github/workflows/build-ios.yml`: quy trình tạo gói DEB bằng GitHub Actions macOS. Workflow chưa được chạy thực tế trong phiên này.

## Biên dịch iOS (chưa xác minh)

1. Nếu có Theos + iOS SDK: trong `ios/`, chạy `make package FINALPACKAGE=1` với biến `THEOS` trỏ tới Theos. Nếu không có Mac, có thể upload cả cây mã nguồn này vào repo GitHub riêng và chạy **Actions → Build RYU iOS rootless virtual HID research tool → Run workflow**. Tải artifact DEB từ kết quả workflow. Kết quả Actions là **build**, không phải test touch.
2. Lưu ý: iPhone 11 Pro Max trên iOS 17.5 có CPU arm64e. Gói `rootless` chạy trên arm64 nhưng khả năng entitlement và IOHIDUserDevice hoạt động phụ thuộc nền jailbreak. Không cài ép/bỏ qua lỗi chữ ký nếu hệ thống từ chối.
3. Cài DEB bằng Sileo/Filza sau khi kiểm tra mã nguồn, hoặc bằng `dpkg -i` qua SSH/NewTerm. Tool không tự khởi động và không hook SpringBoard/backboardd.

## Test P1 trên iPhone — từ thấp đến cao, không dùng trong game lúc đầu

1. Mở terminal trên iPhone, chạy `ryuvirtualhid --report-test` để tự kiểm tra report; bước này không đăng ký HID.
2. Chạy `ryuvirtualhid --probe` để thử tạo virtual device; dòng `PROBE_CREATED` vẫn chưa có nghĩa iOS nhận touch. Nếu `virtual HID creation rejected`, dừng và gửi lỗi (liên quan entitlement / IOKit / chữ ký / OS), không đổi cấu hình bừa.
3. Chạy `ryuvirtualhid` trong terminal foreground. Nó in dòng `LISTEN 0.0.0.0:6017 ... TOKEN=<32hex>`. Chỉ dùng Wi-Fi tin cậy vì TCP không mã hóa. **KHÔNG mở port 6017 lên Internet**, không chia sẻ token.
4. Cắm iPhone USB, mở RYU Native/iPhoneMirror **phản chiếu hình như trước**. Trên laptop bấm `windows/RUN_WINDOWS.bat`, ưu tiên chọn **USB** (đi qua Apple Mobile Device USBMux; không đổi driver) và token từ terminal. Nếu USBMux không kết nối trong khi iPhoneMirror chiếu vì cấu hình QuickTime USB, chọn **Wi-Fi** và nhập IP iPhone + token. Không dùng ZXTouch.
5. Bấm **Mở lớp phủ**, căn khung phủ lên đúng phần hình game đang chiếu (không tính viền), chọn Landscape left/right theo hướng đúng. Sau khi CONNECT thành công, mới **ARM**. Lệnh click chỉ được gửi khi ARM.
6. Test tap trên màn hình chính hoặc app vẽ; nếu tap hoạt động, **giữ một ngón tay THẬT** trong app thử cảm ứng, rồi đồng thời click/giữ/kéo một ngón từ laptop. Quan sát có `touchesCancelled`/nhả chiêu hay đổi vị trí ngón thật không. Lặp lại ít nhất 20 lượt. **Bất kỳ lần nào ngón thật bị nhả/hủy là FAIL**, không dùng trong game. Trường hợp touch ảo không phản hồi, ghi nhận FAIL chức năng đăng ký/route HID, không báo thành công.
7. Dừng bằng `ARM/DISARM` hoặc `DỪNG/ NHẢ NGÓN ẢO`. Để đóng iPhone tool, nhấn Ctrl+C trong terminal (process terminate => virtual HID device removed). Không cài launch daemon lúc chưa đạt test.

### Giới hạn & cảnh báo

- **Chưa có một bản `.deb` đã build/test thành công trên máy bạn.** Source được soạn đủ phần client/server/descriptor nhưng có thể cần sửa theo iOS 17.5.
- Không hack driver, không sửa iOS hiện có, không hook backboardd. Đây là lựa chọn ít nguy cơ phá luồng cảm ứng thật hơn so với việc giả mạo `senderID` của phần cứng, nhưng không bảo đảm tránh xung đột cho đến khi kiểm chứng thực tế.
- Không chủ động thay đổi thiết lập jailbreak, không chạy cùng các macro touch cũ khi thử nghiệm.
- Token được in ở terminal iPhone khi chạy; dùng mạng cục bộ, tránh Wi-Fi công cộng.
- Sai số điểm/màn hình: iPhone 11 Pro Max có portrait 414×896 points; code sử dụng tọa độ HID point-space, không gửi theo 2688×1242 pixels của QuickTime.
- Tốc độ mục tiêu nhận MOVE 125 Hz; thực tế phụ thuộc iOS HID stack, TCP, màn hình và CPU. Chưa benchmark.

## Nguồn tham khảo, không chép nguyên dự án vào ZIP

- Apple IOKit `IOHIDUserDevice`: https://github.com/opensource-apple/IOKitUser/blob/master/hid.subproj/IOHIDUserDevice.h
- Apple IOHIDFamily test source: https://github.com/aosm/IOHIDFamily/blob/master/tools/IOHIDUserDeviceTest.c
- Bằng chứng ứng dụng trên iOS 12.5.x jailbreak (không phải iOS 17.5): https://gist.github.com/ebowwa/a86d961f2f9ebb4d602388ca59dd2698
- ZXTouch Rootless gốc (GPLv3) để đối chiếu: https://github.com/Epic0001/zxtouchrootless
- WebKit `HIDEventGenerator.mm` để tham khảo hành vi report và state: https://github.com/WebKit/WebKit/blob/main/Tools/WebKitTestRunner/ios/HIDEventGenerator.mm

**Quy tắc RYU:** chỉ công bố hai luồng độc lập sau bài test ngón thật + laptop trên chính iPhone iOS 17.5; không xem thành công giả từ socket/HID report là đạt. Không sửa đường video USB 60FPS.

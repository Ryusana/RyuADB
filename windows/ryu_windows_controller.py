"""RYU Virtual HID P1 Windows control surface, stdlib only.
Video is still drawn by iPhoneMirror; this app only overlays a mouse touch region.
UNVERIFIED on iOS 17.5. Requires compiled ryuvirtualhid on the iPhone.
"""
from __future__ import annotations
import collections
import queue
import socket
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox
from dataclasses import dataclass
from ryu_usb_mux import open_usbmux_device

PORT = 6017

def to_portrait(u: float, v: float, orientation: str, w: int = 414, h: int = 896):
    """Map 0..1 normalized image point into portrait iPhone UIKit points."""
    u, v = max(0., min(1., u)), max(0., min(1., v))
    if orientation == 'Landscape left':
        x, y = v * w, (1-u) * h
    elif orientation == 'Landscape right':
        x, y = (1-v) * w, u * h
    elif orientation == 'Upside down':
        x, y = (1-u) * w, (1-v) * h
    else:
        x, y = u * w, v * h
    return round(x), round(y)

@dataclass
class Cmd:
    op: str
    finger: int = 0
    x: int = 0
    y: int = 0

class Link:
    """Ordered send/ack worker. Drops obsolete MOVE, never drops DOWN or UP."""
    def __init__(self, on_status):
        self.on_status=on_status
        self.q: queue.Queue[Cmd]=queue.Queue(maxsize=512)
        self.sock=None
        self.file=None
        self.active=False
        self.thread=None
        self.lock=threading.Lock()
    def open(self, mode, ip, token):
        if self.active: self.close()
        if len(token)!=32 or any(c not in '0123456789abcdef' for c in token):
            raise ValueError('Token phải có 32 ký tự hex từ cửa sổ iPhone.')
        sock = open_usbmux_device(PORT) if mode == "USB" else socket.create_connection((ip,PORT), timeout=3)
        sock.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
        sock.settimeout(3)
        reader=sock.makefile('rb')
        sock.sendall(('HELLO '+token+'\n').encode('ascii'))
        reply=reader.readline(160).decode(errors='replace').strip()
        if not reply.startswith('READY 414 896 5'):
            reader.close();sock.close();raise ConnectionError('iPhone từ chối: '+reply)
        self.sock,self.file=sock,reader
        self.active=True
        self.thread=threading.Thread(target=self._sendloop,daemon=True)
        self.thread.start()
    def queue_cmd(self, cmd):
        if not self.active:return False
        if cmd.op=='MOVE' and self.q.qsize()>4:return False
        try:self.q.put_nowait(cmd);return True
        except queue.Full:
            if cmd.op!='MOVE':
                # Never silently lose an UP. Fail safe: tear down connection, releasing virtual contacts.
                self.close()
                self.on_status('Mất kết nối do hàng đợi đầy; iPhone sẽ nhả ngón ảo')
            return False
    def _sendloop(self):
        try:
            last_io=time.monotonic()
            while self.active:
                try: cmd=self.q.get(timeout=0.2)
                except queue.Empty:
                    if time.monotonic()-last_io>=1.0:
                        self.sock.sendall(b"PING\n")
                        pong=self.file.readline(120).decode(errors="replace").strip()
                        if pong!="PONG":raise RuntimeError("Heartbeat thất bại: "+pong)
                        last_io=time.monotonic()
                    continue
                payload = (cmd.op+'\n') if cmd.op=='RESET' else (f'{cmd.op} {cmd.finger} {cmd.x} {cmd.y}\n')
                self.sock.sendall(payload.encode('ascii'))
                result=self.file.readline(120).decode(errors='replace').strip()
                if not result.startswith('OK'):
                    raise RuntimeError(f'iPhone trả về: {result or "đã ngắt"}')
                last_io=time.monotonic()
        except Exception as exc:
            self.on_status('Lỗi điều khiển: '+str(exc))
        finally:
            self.active=False
            self._close_sock()
    def _close_sock(self):
        with self.lock:
            try:
                if self.sock:self.sock.shutdown(socket.SHUT_RDWR)
            except OSError: pass
            try:
                if self.sock:self.sock.close()
            except OSError: pass
            self.sock=None
    def close(self):
        if self.active: self.queue_cmd(Cmd('RESET'))
        self.active=False
        self._close_sock()
        while not self.q.empty():
            try:self.q.get_nowait()
            except queue.Empty:break

class Controller(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title('RYU DUAL TOUCH • P1 Virtual HID • iOS 17.5 TEST')
        self.geometry('660x485')
        self.configure(bg='#10151f')
        self.messages=queue.SimpleQueue()
        self.link=Link(lambda m:self.messages.put(m))
        self.ip=tk.StringVar(value='192.168.1.100')
        self.transport=tk.StringVar(value='USB')
        self.token=tk.StringVar()
        self.rotation=tk.StringVar(value='Landscape left')
        self.w=tk.IntVar(value=1000);self.h=tk.IntVar(value=490)
        self.x=tk.IntVar(value=250);self.y=tk.IntVar(value=160)
        self.status=tk.StringVar(value='Chưa kết nối • Chưa ARM • Chưa test iOS thực')
        self.armed=False;self.overlay=None;self.finger_down=False;self.last=(0,0)
        self.last_move_at=0.0
        self._draw()
        self.after(100,self._poll)
        self.protocol('WM_DELETE_WINDOW',self._quit)
    def _draw(self):
        top=tk.Frame(self,bg='#10151f');top.pack(fill='both',expand=True,padx=20,pady=18)
        tk.Label(top,text='RYU Virtual Touch — hai thiết bị HID tách biệt',fg='#e9f2ff',bg='#10151f',font=('Segoe UI',16,'bold')).pack(anchor='w')
        tk.Label(top,text='Video USB qua iPhoneMirror; cảm ứng qua thiết bị HID ảo trên iPhone jailbreak. Chưa xác minh iOS 17.5.',fg='#9eafc7',bg='#10151f',wraplength=615,justify='left').pack(anchor='w',pady=(3,14))
        f=tk.Frame(top,bg='#10151f');f.pack(fill='x')
        tk.Label(f,text='iPhone IP:',fg='white',bg='#10151f').grid(row=0,column=0,sticky='w')
        tk.Entry(f,textvariable=self.ip,width=19).grid(row=0,column=1,sticky='w',padx=9)
        ttk.Combobox(f,textvariable=self.transport,values=['USB','Wi-Fi'],state='readonly',width=10).grid(row=0,column=2,sticky='w')
        tk.Label(f,text='Token (từ iPhone):',fg='white',bg='#10151f').grid(row=1,column=0,sticky='w',pady=8)
        tk.Entry(f,textvariable=self.token,width=38,show='•').grid(row=1,column=1,columnspan=2,sticky='w',padx=9,pady=8)
        ttk.Button(f,text='Kết nối',command=self.connect).grid(row=0,column=3,padx=10)
        ttk.Button(f,text='Ngắt',command=self.disconnect).grid(row=1,column=3,padx=10)
        tk.Label(top,text='Vùng ảnh video trên màn hình Windows (pixels) — kéo chuột phải trên lớp phủ để chỉnh vị trí',fg='#9eafc7',bg='#10151f').pack(anchor='w',pady=(12,6))
        geo=tk.Frame(top,bg='#10151f');geo.pack(anchor='w')
        for name,var in [('X',self.x),('Y',self.y),('W',self.w),('H',self.h)]:
            tk.Label(geo,text=name,fg='white',bg='#10151f').pack(side='left')
            tk.Entry(geo,textvariable=var,width=7).pack(side='left',padx=(4,15))
        tk.Label(top,text='Hướng video',fg='white',bg='#10151f').pack(anchor='w',pady=(12,4))
        ttk.Combobox(top,textvariable=self.rotation,values=['Landscape left','Landscape right','Portrait','Upside down'],state='readonly',width=30).pack(anchor='w')
        actions=tk.Frame(top,bg='#10151f');actions.pack(fill='x',pady=15)
        ttk.Button(actions,text='Mở / cập nhật lớp phủ',command=self.create_overlay).pack(side='left',padx=(0,8))
        ttk.Button(actions,text='ARM / DISARM',command=self.arm_toggle).pack(side='left',padx=(0,8))
        ttk.Button(actions,text='DỪNG / NHẢ NGÓN ẢO',command=self.emergency).pack(side='left')
        tk.Label(top,textvariable=self.status,fg='#ffd18b',bg='#10151f',wraplength=620,justify='left').pack(anchor='w',pady=5)
        tk.Label(top,text='Giữ ngón thật trên iPhone rồi click bằng laptop để thử đồng thời. Không thử trong trận thật trước khi đạt.',fg='#9eafc7',bg='#10151f',wraplength=620).pack(anchor='w',pady=(6,0))
    def _poll(self):
        while not self.messages.empty():self.status.set(self.messages.get())
        self.after(100,self._poll)
    def connect(self):
        ip = self.ip.get().strip()
        token = self.token.get().strip().lower()
        mode = self.transport.get().strip()
        def work():
            try:
                self.link.open(mode,ip,token)
                self.messages.put('ĐÃ KẾT NỐI qua '+mode+' đến HID ảo. CHƯA xác minh touch trên iPhone.')
            except Exception as exc:self.messages.put('Kết nối thất bại: '+str(exc))
        threading.Thread(target=work,daemon=True).start()
    def disconnect(self):self.emergency();self.link.close();self.status.set('Đã ngắt; chỉ ngón HID ảo được nhả')
    def _geometry(self):
        x,y,w,h=(int(v.get()) for v in (self.x,self.y,self.w,self.h))
        if w<70 or h<70:raise ValueError('W/H phải >=70')
        return x,y,w,h
    def create_overlay(self):
        try:x,y,w,h=self._geometry()
        except Exception as exc:messagebox.showerror('Vùng phủ',str(exc));return
        self.armed=False
        if self.overlay:self.overlay.destroy()
        t=tk.Toplevel(self);self.overlay=t
        t.overrideredirect(True);t.attributes('-topmost',True);t.attributes('-alpha',0.23)
        t.geometry(f'{w}x{h}+{x}+{y}')
        t.configure(bg='#4474aa')
        border=tk.Frame(t,bg='#eec35a',borderwidth=4,relief='solid');border.pack(fill='both',expand=True)
        body=tk.Frame(border,bg='#16324c');body.pack(fill='both',expand=True)
        msg=tk.Label(body,text='RYU TOUCH • DISARMED • Chuột phải kéo vùng này',fg='#ffffff',bg='#16324c',font=('Segoe UI',13,'bold'))
        msg.pack(expand=True)
        for widget in (t,border,body,msg):
            widget.bind('<ButtonPress-1>',self._down)
            widget.bind('<B1-Motion>',self._move)
            widget.bind('<ButtonRelease-1>',self._up)
            widget.bind('<ButtonPress-3>',self._drag_start)
            widget.bind('<B3-Motion>',self._drag)
        self._origin=None
        t.bind('<Escape>',lambda e:self.emergency())
        self._overlay_caption=msg
        self._set_overlay_caption()
        self.status.set('Vùng phủ đang hiển thị; chỉnh khớp với vùng video, sau đó ARM')
    def _set_overlay_caption(self):
        if self.overlay:
            self._overlay_caption.configure(text='RYU TOUCH • '+ ('ARMED (ĐANG BẬT)' if self.armed else 'DISARMED') + ' • ESC: Dừng')
    def _drag_start(self,event):self._origin=(event.x_root,event.y_root,self.overlay.winfo_x(),self.overlay.winfo_y())
    def _drag(self,event):
        if not self._origin or self.finger_down:return
        sx,sy,ox,oy=self._origin
        nx,ny=ox+event.x_root-sx,oy+event.y_root-sy
        self.overlay.geometry(f'+{nx}+{ny}');self.x.set(nx);self.y.set(ny)
    def arm_toggle(self):
        if self.armed:self.emergency();return
        if not self.link.active:
            self.status.set('CHƯA KẾT NỐI — không ARM');return
        if not self.overlay:self.create_overlay()
        self.armed=True;self._set_overlay_caption();self.status.set('Đã ARM • đang thử nghiệm, không bảo đảm độc lập ngón vật lý')
    def _coord(self,event):
        w,h=self.overlay.winfo_width(),self.overlay.winfo_height()
        u=(event.x_root-self.overlay.winfo_rootx())/max(1,w)
        v=(event.y_root-self.overlay.winfo_rooty())/max(1,h)
        return to_portrait(u,v,self.rotation.get())
    def _down(self,event):
        if not self.armed or self.finger_down:return
        self.last=self._coord(event)
        self.finger_down=self.link.queue_cmd(Cmd('DOWN',0,*self.last))
    def _move(self,event):
        if not self.finger_down:return
        now=time.perf_counter()
        if now-self.last_move_at<0.008:return
        self.last_move_at=now;self.last=self._coord(event)
        self.link.queue_cmd(Cmd('MOVE',0,*self.last))
    def _up(self,event):
        if not self.finger_down:return
        self.last=self._coord(event)
        self.link.queue_cmd(Cmd('UP',0,*self.last));self.finger_down=False
    def emergency(self):
        if self.finger_down:self.link.queue_cmd(Cmd('UP',0,*self.last))
        self.finger_down=False;self.armed=False
        if self.link.active:self.link.queue_cmd(Cmd('RESET'))
        self._set_overlay_caption();self.status.set('DISARM • toàn bộ ngón ảo đã được yêu cầu nhả, không tác động ngón thật')
    def _quit(self):
        self.emergency();self.link.close();self.destroy()

if __name__=='__main__':Controller().mainloop()

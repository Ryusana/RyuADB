"""Minimal Windows Apple USBMux v1 plist Connect transport, standalone (no pip)."""
from __future__ import annotations
import plistlib
import socket
import struct

class USBMuxError(RuntimeError):pass

def _readn(sock,n):
    b=bytearray()
    while len(b)<n:
        chunk=sock.recv(n-len(b))
        if not chunk:raise USBMuxError('USBMux đã đóng kết nối')
        b.extend(chunk)
    return bytes(b)

def _send(sock,msg,tag):
    data=plistlib.dumps(msg,fmt=plistlib.FMT_XML)
    sock.sendall(struct.pack('<IIII',16+len(data),1,8,tag)+data)

def _recv(sock):
    size,version,msg,tag=struct.unpack('<IIII',_readn(sock,16))
    if not 16<=size<=2_000_000 or version!=1 or msg!=8:
        raise USBMuxError('USBMux phản hồi header không hợp lệ')
    return plistlib.loads(_readn(sock,size-16))

def open_usbmux_device(device_port=6017,host_port=27015,timeout=3):
    sock=socket.create_connection(('127.0.0.1',host_port),timeout)
    sock.settimeout(timeout)
    try:
        base={'ClientVersionString':'RYU Virtual HID/0.1','ProgName':'RYU Virtual Touch','kLibUSBMuxVersion':3}
        _send(sock,dict(base,MessageType='ListDevices'),1)
        result=_recv(sock)
        devices=[d for d in result.get('DeviceList',[]) if d.get('Properties',{}).get('ConnectionType','').lower()=='usb']
        if len(devices)!=1:raise USBMuxError(f'USBMux có {len(devices)} iPhone USB, cần đúng 1')
        did=devices[0].get('DeviceID')
        if not isinstance(did,int):raise USBMuxError('USBMux không có DeviceID')
        _send(sock,dict(base,MessageType='Connect',DeviceID=did,PortNumber=socket.htons(device_port)),2)
        result=_recv(sock)
        if result.get('MessageType')!='Result' or result.get('Number')!=0:
            raise USBMuxError('Không mở được kênh USBMux đến cổng 6017 của iPhone')
        sock.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
        return sock
    except Exception:
        sock.close();raise

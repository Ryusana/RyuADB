import pathlib, queue, socketserver, sys, threading, time, unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'windows'))
import ryu_windows_controller as client
TOKEN='0123456789abcdef'*2

class MockHandler(socketserver.StreamRequestHandler):
    def handle(self):
        auth=self.rfile.readline().decode().strip()
        if auth!='HELLO '+TOKEN:
            self.wfile.write(b'ERR AUTH\n');self.wfile.flush();return
        self.wfile.write(b'READY 414 896 5\n');self.wfile.flush()
        while True:
            line=self.rfile.readline()
            if not line:return
            if line==b'PING\n':
                self.wfile.write(b'PONG\n');self.wfile.flush();continue
            self.server.commands.put(line.decode().strip())
            self.wfile.write(b'OK\n');self.wfile.flush()
class Srv(socketserver.ThreadingTCPServer):
    allow_reuse_address=True
    daemon_threads=True
    def __init__(self,*args,**kwargs):
        super().__init__(*args,**kwargs);self.commands=queue.Queue()
class NetworkTest(unittest.TestCase):
    def setUp(self):
        self.srv=Srv(('127.0.0.1',0),MockHandler)
        self.port=client.PORT;client.PORT=self.srv.server_address[1]
        self.thread=threading.Thread(target=self.srv.serve_forever,daemon=True);self.thread.start()
        self.link=client.Link(lambda m:None)
    def tearDown(self):
        self.link.close();client.PORT=self.port
        self.srv.shutdown();self.srv.server_close();self.thread.join(timeout=2)
    def test_commands_keep_order(self):
        self.link.open('Wi-Fi','127.0.0.1',TOKEN)
        self.assertTrue(self.link.queue_cmd(client.Cmd('DOWN',0,100,100)))
        self.assertTrue(self.link.queue_cmd(client.Cmd('MOVE',0,101,101)))
        self.assertTrue(self.link.queue_cmd(client.Cmd('UP',0,101,101)))
        got=[self.srv.commands.get(timeout=2) for _ in range(3)]
        self.assertEqual(got,['DOWN 0 100 100','MOVE 0 101 101','UP 0 101 101'])
    def test_held_contact_heartbeat(self):
        self.link.open('Wi-Fi','127.0.0.1',TOKEN)
        self.link.queue_cmd(client.Cmd('DOWN',0,180,420))
        self.assertEqual(self.srv.commands.get(timeout=2),'DOWN 0 180 420')
        time.sleep(1.35) # heartbeat ran during hold, connection should stay up
        self.assertTrue(self.link.active)
        self.link.queue_cmd(client.Cmd('UP',0,180,420))
        self.assertEqual(self.srv.commands.get(timeout=2),'UP 0 180 420')
    def test_auth(self):
        with self.assertRaises((ValueError,ConnectionError)):
            self.link.open('Wi-Fi','127.0.0.1','0'*32)
if __name__=='__main__':unittest.main()

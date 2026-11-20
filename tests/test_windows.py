import sys, pathlib, unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'windows'))
from ryu_windows_controller import to_portrait
class Coords(unittest.TestCase):
    def test_portrait(self):self.assertEqual(to_portrait(.5,.5,'Portrait'),(207,448))
    def test_landscape_left(self):
        self.assertEqual(to_portrait(0,0,'Landscape left'),(0,896))
        self.assertEqual(to_portrait(1,1,'Landscape left'),(414,0))
    def test_landscape_right(self):
        self.assertEqual(to_portrait(0,0,'Landscape right'),(414,0))
        self.assertEqual(to_portrait(1,1,'Landscape right'),(0,896))
    def test_boundaries(self):self.assertEqual(to_portrait(5,-2,'Portrait'),(414,0))
if __name__=='__main__': unittest.main()

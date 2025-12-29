import unittest
import pyzgc

class TestBasics(unittest.TestCase):
    def test_object_creation(self):
        obj = pyzgc.Object()
        self.assertIsNotNone(obj)
        self.assertTrue(pyzgc.is_young(obj))

    def test_store_load(self):
        obj = pyzgc.Object()
        val = "Hello"
        obj.store(0, val)
        loaded = obj.load(0)
        self.assertEqual(loaded, val)

    def test_store_load_integers(self):
        obj = pyzgc.Object()
        val = 12345
        obj.store(1, val)
        loaded = obj.load(1)
        self.assertEqual(loaded, val)

    def test_getitem_setitem(self):
        obj = pyzgc.Object()
        val = "Item"
        obj[0] = val
        self.assertEqual(obj[0], val)

    def test_out_of_bounds(self):
        obj = pyzgc.Object()
        with self.assertRaises(IndexError):
            obj.load(100)
        with self.assertRaises(IndexError):
            obj.store(100, "Fail")

if __name__ == "__main__":
    unittest.main()

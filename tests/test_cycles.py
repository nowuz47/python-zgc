import unittest
import pyzgc
import gc

class TestCycles(unittest.TestCase):
    def setUp(self):
        pyzgc.start_gc()

    def tearDown(self):
        pyzgc.stop_gc()

    def test_simple_cycle(self):
        # A -> B -> A
        a = pyzgc.Object()
        b = pyzgc.Object()
        a.store(0, b)
        b.store(0, a)
        
        # Break references from stack
        del a
        del b
        
        # Force GC
        pyzgc.gc()
        # We can't easily verify they are gone without weakrefs or internal stats,
        # but we can verify no crash.

    def test_promotion(self):
        obj = pyzgc.Object()
        # pyzgc.add_root(obj) # Automatic scanning should handle this
        self.assertTrue(pyzgc.is_young(obj))
        
        # Trigger Minor GC
        pyzgc.minor_gc()
        
        # Should be promoted to Old Gen
        self.assertTrue(pyzgc.is_old(obj))

    def test_remset(self):
        # Old -> Young reference
        old_obj = pyzgc.Object()
        # pyzgc.add_root(old_obj) # Automatic scanning
        pyzgc.minor_gc() # Promote to Old
        self.assertTrue(pyzgc.is_old(old_obj))
        
        young_obj = pyzgc.Object()
        self.assertTrue(pyzgc.is_young(young_obj))
        
        # Create Old -> Young reference (should trigger Write Barrier)
        old_obj.store(0, young_obj)
        
        # Minor GC should find young_obj via RemSet and promote it
        pyzgc.minor_gc()
        
        self.assertTrue(pyzgc.is_old(young_obj))
        self.assertEqual(old_obj.load(0), young_obj)

if __name__ == "__main__":
    unittest.main()

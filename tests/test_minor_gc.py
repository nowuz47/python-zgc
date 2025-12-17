import pyzgc
import unittest
import sys

class TestMinorGC(unittest.TestCase):
    def test_minor_gc_promotion(self):
        # Mask to ignore top 4 bits (Color)
        ADDR_MASK = 0x0FFFFFFFFFFFFFFF
        
        # 1. Create an Old Object
        old_obj = pyzgc.Object()
        
        # Allocate fillers to force old_obj promotion
        fillers1 = []
        for i in range(50000): 
            o = pyzgc.Object()
            fillers1.append(o)
            
        pyzgc.add_root(old_obj)
        pyzgc.gc() # Full GC. old_obj promoted.
        
        # Trigger barrier
        old_obj.store(0, None) 
        
        old_addr_before = pyzgc.get_body_address(old_obj)
        print(f"Old Object Address (Before): {hex(old_addr_before)}")
        
        # 2. Create a Young Object
        young_obj = pyzgc.Object()
        young_obj.store(0, 123) 
        
        # Allocate fillers to force young_obj page retirement
        fillers2 = []
        for i in range(50000):
            o = pyzgc.Object()
            fillers2.append(o)
        
        young_addr_before = pyzgc.get_body_address(young_obj)
        print(f"Young Object Address (Before): {hex(young_addr_before)}")
        
        # 3. Assign Young to Old (Trigger Write Barrier)
        old_obj.store(1, young_obj)
        
        # 4. Run Minor GC
        print("Running Minor GC...")
        pyzgc.add_root(old_obj)
        pyzgc.minor_gc()
        
        # 5. Verify
        
        # Trigger barrier on old_obj
        old_obj.load(1)
        old_addr_after = pyzgc.get_body_address(old_obj)
        
        # Trigger barrier on young_obj
        young_obj.load(0) 
        young_addr_after = pyzgc.get_body_address(young_obj)
        
        print(f"Old Object Address (After): {hex(old_addr_after)}")
        print(f"Young Object Address (After): {hex(young_addr_after)}")
        
        # Old object should NOT move (ignoring color)
        self.assertEqual(old_addr_before & ADDR_MASK, old_addr_after & ADDR_MASK, 
                         "Old object should not move during Minor GC")
        
        # Young object SHOULD move (promoted)
        self.assertNotEqual(young_addr_before & ADDR_MASK, young_addr_after & ADDR_MASK, 
                            "Young object should move (promote) during Minor GC")
        
        # Check connectivity
        retrieved_child = old_obj.load(1)
        self.assertEqual(retrieved_child, young_obj)

if __name__ == '__main__':
    unittest.main()

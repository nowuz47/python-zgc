import unittest
import sys
import os
import time

# Ensure we can import pyzgc
sys.path.insert(0, os.path.abspath('.'))
import pyzgc

class TestGenerationalZGC(unittest.TestCase):
    def test_promotion_and_remset(self):
        print("\n[Test] Generational ZGC: Promotion & RemSet")
        
        # 1. Create an object that will become Old
        old_obj = pyzgc.Object()
        self.assertTrue(pyzgc.is_young(old_obj), "New object should be Young")
        
        # 2. Trigger Minor GC to promote it
        print("Promoting old_obj...")
        pyzgc.add_root(old_obj)  # Must add as root!
        pyzgc.trigger_minor_gc()
        self.assertTrue(pyzgc.is_old(old_obj), "Object should be promoted to Old")
        
        # 3. Create a Young object
        young_obj = pyzgc.Object()
        self.assertTrue(pyzgc.is_young(young_obj), "New object should be Young")
        
        # 4. Create Old -> Young reference
        # This should trigger the Write Barrier and add old_obj to RemSet
        old_obj[0] = young_obj
        
        # 5. Trigger Minor GC
        # - young_obj should be marked (via RemSet) and promoted
        # - old_obj[0] should be updated to new address
        print("Triggering Minor GC with RemSet...")
        pyzgc.add_root(old_obj) # Keep old_obj alive
        pyzgc.trigger_minor_gc()
        
        # 6. Verify
        self.assertTrue(pyzgc.is_old(young_obj), "Young object should be promoted")
        self.assertEqual(old_obj[0], young_obj, "Reference should be valid")
        
        # Verify values survive
        young_obj[0] = "survived"
        self.assertEqual(old_obj[0][0], "survived")
        
        print("Generational Test Passed!")

if __name__ == '__main__':
    unittest.main()

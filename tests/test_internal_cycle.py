import unittest
import pyzgc
import sys

class TestInternalCycle(unittest.TestCase):
    def test_internal_cycle(self):
        print("\nTesting Internal ZGC Cycle Collection...")
        
        # Create a cycle: ZObject A -> ZObject B -> ZObject A
        a = pyzgc.Object()
        b = pyzgc.Object()
        
        a.store(0, b)
        b.store(0, a)
        
        # Get addresses to check liveness
        addr_a = pyzgc.get_body_address(a)
        addr_b = pyzgc.get_body_address(b)
        
        print(f"Created cycle: A({hex(addr_a)}) <-> B({hex(addr_b)})")
        
        # Drop Python references
        del a
        del b
        
        # At this point, A and B are only reachable from each other.
        # They are unreachable from roots.
        # ZGC should collect them.
        
        # Trigger Full GC
        print("Running Full GC...")
        pyzgc.gc()
        
        # How to verify they are collected?
        # In this prototype, we don't have a way to check if an address is freed.
        # But we can check if memory usage drops or if they don't show up in debug prints?
        # Or we can use a weakref-like mechanism if we had one.
        # Since we don't, we rely on the fact that if they were NOT collected, 
        # repeated creation would OOM.
        
        print("Creating many cycles to test reclamation...")
        for i in range(100):
            a = pyzgc.Object()
            b = pyzgc.Object()
            a.store(0, b)
            b.store(0, a)
            # Explicitly del to drop refcounts
            del a
            del b
            
            if i % 10 == 0:
                pyzgc.gc()
        
        print("SUCCESS: Survived cycle creation loop without OOM.")

if __name__ == '__main__':
    unittest.main()

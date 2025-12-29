import unittest
import pyzgc
import time

class TestSignalBarrier(unittest.TestCase):
    def setUp(self):
        # pyzgc.start_gc() # Disable background GC to avoid race during test
        # Ensure signal barrier is enabled (it should be default now)
        import sys
        # if sys.platform == 'darwin':
        #     print("Skipping signal barrier test on macOS due to known hang")
        #     self.skipTest("Signal barriers hang on macOS")
        pyzgc.enable_signal_barrier()

    def tearDown(self):
        # pyzgc.stop_gc()
        pass

    def test_barrier_correctness(self):
        # Create a graph of objects
        root = pyzgc.Object()
        child = pyzgc.Object()
        root.store(0, child)
        
        # Trigger GC to force relocation and protection
        # We need enough objects to fill a page or force a cycle
        print("Allocating objects to trigger GC...")
        objects = []
        for i in range(1000):
            o = pyzgc.Object()
            objects.append(o)
        
        # Manually trigger GC
        print("Triggering Minor GC...")
        pyzgc.minor_gc()
        
        # Access the child through the root
        # This should trigger the signal barrier if the page was protected
        print("Accessing child...")
        retrieved_child = root.load(0)
        
        self.assertIsNotNone(retrieved_child)
        self.assertEqual(retrieved_child, child)
        print("Access successful!")

    def test_disable_barrier(self):
        pyzgc.disable_signal_barrier()
        # Should still work (fallback to software barrier? No, we disabled software barrier in code!)
        # Wait, if we disable signal barrier, zbarrier_is_signal_mode() returns false.
        # So zobject.c WILL use software barrier.
        
        root = pyzgc.Object()
        child = pyzgc.Object()
        root.store(0, child)
        
        pyzgc.minor_gc()
        
        retrieved_child = root.load(0)
        self.assertEqual(retrieved_child, child)
        
        # Re-enable for other tests
        pyzgc.enable_signal_barrier()

if __name__ == "__main__":
    unittest.main()

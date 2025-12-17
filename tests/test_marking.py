import pyzgc
import time
import sys

print("Testing Concurrent Marking...")
try:
    # Create a graph: Root -> Child -> Grandchild
    root = pyzgc.Object()
    child = pyzgc.Object()
    grandchild = pyzgc.Object()
    
    root.store(0, child)
    child.store(0, grandchild)
    
    print("Created object graph: Root -> Child -> Grandchild")
    
    # Add root to GC (simulating root scanning)
    print("Adding root to GC...")
    pyzgc.add_root(root)
    
    # Run ONE GC cycle synchronously
    print("Running GC cycle...")
    pyzgc.gc()
    
    # Verify marking
    print("Verifying marks...")
    # Note: is_marked checks the bitmap. 
    # Since we clear bitmaps at the START of the cycle, 
    # and we just ran a cycle, the bitmaps should be populated for live objects.
    
    if not pyzgc.is_marked(root):
        raise Exception("Root is not marked!")
    if not pyzgc.is_marked(child):
        raise Exception("Child is not marked!")
    if not pyzgc.is_marked(grandchild):
        raise Exception("Grandchild is not marked!")
        
    # Verify non-reachable object is NOT marked
    unreachable = pyzgc.Object()
    if pyzgc.is_marked(unreachable):
        raise Exception("Unreachable object IS marked (should not be)!")
        
    print("Marking test passed!")
except Exception as e:
    print(f"Test failed: {e}")
    sys.exit(1)

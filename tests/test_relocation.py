import pyzgc
import time
import sys

print("Testing Concurrent Relocation & Remapping...")

try:
    # 1. Create a root object and a child
    root = pyzgc.Object()
    child = pyzgc.Object()
    root.store(0, child)
    
    # Get initial address of child's body
    child_addr_1 = pyzgc.get_body_address(child)
    print(f"Initial Root Body: {hex(pyzgc.get_body_address(root))}")
    print(f"Initial Child Body: {hex(child_addr_1)}")
    
    # 2. Allocate many objects to force a new page creation
    # This ensures that 'child' is in an old page that will be candidate for evacuation.
    print("Allocating filler objects to force new page...")
    fillers = []
    for i in range(50000):
        fillers.append(pyzgc.Object())
        
    # 3. Add root to GC
    pyzgc.add_root(root)
    
    # 4. Run GC Cycle
    print("Running GC cycle...")
    pyzgc.gc()
    
    # 5. Trigger Load Barrier
    # The child object should have been moved.
    # But 'root' still points to the old address (with old color).
    # Loading from 'root' should trigger the barrier, which updates 'root->body' (if needed)
    # AND returns the child.
    # Wait, 'root' itself wasn't moved (it's a Handle).
    # But 'child' (Body) was moved.
    # 'root.slots[0]' points to 'child' (Handle).
    # Handles don't move.
    # So 'root.slots[0]' value doesn't change.
    
    # Wait, what are we testing?
    # We want to test that 'child's BODY moved.
    # 'child' is a Handle.
    # We can check 'child's body address.
    
    # But does 'child's body address update automatically?
    # Only if we access 'child' via a barrier?
    # 'child' is a Python object.
    # If we call 'pyzgc.get_body_address(child)', it reads 'child->body'.
    # Does 'get_body_address' trigger a barrier?
    # No, it just reads the pointer.
    
    # However, if 'child' was moved, 'child->body' should point to the new location?
    # Who updates 'child->body'?
    # The GC does NOT update handles (unless we have root scanning that updates handles).
    # Our 'zgc_add_root' only pushes to mark stack. It doesn't register the handle location for update.
    # So 'child->body' is STALE.
    
    # BUT, we have a Self-Healing Barrier.
    # When we access 'child', we trigger the barrier.
    # 'pyzgc.get_body_address' does NOT trigger barrier (it's a C function reading the struct).
    
    # So we need to trigger the barrier on 'child'.
    # We can do 'child.load(0)'? (If child has slots).
    # Or 'root.load(0)' returns 'child'.
    # Does 'root.load(0)' trigger barrier on 'child'?
    # 'root.load(0)' triggers barrier on 'root' (to read slot safely).
    # It returns 'child' (Handle).
    # Then it calls 'zbarrier_load(child)' on the result!
    # 'zbarrier_load(child)' checks 'child->body' color.
    # If bad, it calls 'zbarrier_fix_pointer(child)'.
    # 'zbarrier_fix_pointer' looks up forwarding table and updates 'child->body'.
    
    # So:
    print("Triggering barrier via root.load(0)...")
    loaded_child = root.load(0)
    
    # Now 'child->body' should be updated.
    child_addr_2 = pyzgc.get_body_address(child)
    print(f"New Child Body: {hex(child_addr_2)}")
    
    # Mask out the top 4 bits (Color)
    mask = (1 << 60) - 1
    raw_addr_1 = child_addr_1 & mask
    raw_addr_2 = child_addr_2 & mask
    
    print(f"Raw Child Body 1: {hex(raw_addr_1)}")
    print(f"Raw Child Body 2: {hex(raw_addr_2)}")
    
    if raw_addr_1 == raw_addr_2:
        print("FAILURE: Child body address did not change! (Relocation failed)")
    else:
        print("SUCCESS: Child body address changed! (Relocation + Remapping worked)")
        
except Exception as e:
    print(f"Test failed: {e}")
    sys.exit(1)

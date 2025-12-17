import pyzgc
import sys
import random

print("Starting Stress Test...")

try:
    # Root object
    root = pyzgc.Object()
    pyzgc.add_root(root)
    
    # Keep track of live objects to verify they are not collected/corrupted
    live_objects = []
    
    # 1. Build initial graph
    print("Building initial graph...")
    for i in range(100):
        obj = pyzgc.Object()
        root.store(i % 10, obj) # Store in slots 0-9
        live_objects.append(obj)
        
    # 2. Stress Loop
    print("Entering stress loop...")
    for cycle in range(20):
        print(f"Cycle {cycle+1}/20")
        
        # Allocate many objects (garbage)
        for _ in range(10000):
            tmp = pyzgc.Object()
            
        # Mutate graph
        # Replace some slots in root
        for i in range(10):
            new_obj = pyzgc.Object()
            root.store(i, new_obj)
            live_objects.append(new_obj)
            
        # Run GC
        pyzgc.gc()
        
        # Access objects to trigger barriers
        for i in range(10):
            obj = root.load(i)
            # Just accessing it is enough to trigger barrier
            
    print("Stress Test Passed!")
    
except Exception as e:
    print(f"Stress Test Failed: {e}")
    sys.exit(1)

import pyzgc
import time
import sys

print("Testing Background GC Thread...")
try:
    print("Starting GC thread...")
    pyzgc.start_gc()
    
    print("Sleeping for 1 second to let GC run cycles...")
    time.sleep(1)
    
    print("Stopping GC thread...")
    pyzgc.stop_gc()
    
    print("GC Thread test passed!")
except Exception as e:
    print(f"Test failed: {e}")
    sys.exit(1)

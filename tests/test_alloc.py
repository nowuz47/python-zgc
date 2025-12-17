import pyzgc
import sys

print("Testing pyzgc allocation...")
try:
    # Allocate 1KB
    addr = pyzgc.allocate(1024)
    print(f"Allocated 1KB at: {hex(addr)}")
    
    # Allocate 1MB
    addr2 = pyzgc.allocate(1024 * 1024)
    print(f"Allocated 1MB at: {hex(addr2)}")
    
    # Allocate another 1MB (should trigger new page if page size is 2MB and we used >1MB already)
    # Actually, 1KB + 1MB fits in 2MB page.
    # Let's allocate 2MB (should fail or trigger new page if we implement large pages, but for now it might just fit or fail depending on logic)
    # Our allocator aligns to 8 bytes.
    
    addr3 = pyzgc.allocate(1024 * 1024)
    print(f"Allocated 1MB at: {hex(addr3)}")

    print("Allocation test passed!")
except Exception as e:
    print(f"Test failed: {e}")
    sys.exit(1)

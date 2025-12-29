import pyzgc
import sys

def main():
    print("Testing pyzgc allocation...")
    try:
        # Allocate 1KB
        addr = pyzgc.allocate(1024)
        print(f"Allocated 1KB at: {hex(addr)}")
        
        # Allocate 1MB
        addr2 = pyzgc.allocate(1024 * 1024)
        print(f"Allocated 1MB at: {hex(addr2)}")
        
        # Allocate another 1MB
        addr3 = pyzgc.allocate(1024 * 1024)
        print(f"Allocated 1MB at: {hex(addr3)}")

        print("Allocation test passed!")
    except Exception as e:
        print(f"Test failed: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
elif "unittest" in sys.modules:
    main()

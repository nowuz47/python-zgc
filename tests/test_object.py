import pyzgc
import sys

print("Testing pyzgc.Object...")
try:
    # Create a ZObject
    obj = pyzgc.Object()
    print(f"Created object: {obj}")
    print(f"Object type: {type(obj)}")
    
    # Verify it has a valid address (we can't easily check the address from Python without id(), but id() in CPython is the address)
    addr = id(obj)
    print(f"Object address: {hex(addr)}")
    
    # Create another object
    obj2 = pyzgc.Object()
    print(f"Created object 2: {obj2}")
    print(f"Object 2 address: {hex(id(obj2))}")
    
    # Check if they are different
    if obj is obj2:
        raise Exception("Objects are the same!")
        
    print("ZObject test passed!")
except Exception as e:
    print(f"Test failed: {e}")
    sys.exit(1)

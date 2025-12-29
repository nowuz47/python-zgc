import pyzgc
import sys

def main():
    print("Testing Load Barrier...")
    try:
        # Create two objects
        obj1 = pyzgc.Object()
        obj2 = pyzgc.Object()
        
        # Store obj2 in obj1's slot 0
        print("Storing obj2 in obj1.slots[0]...")
        obj1.store(0, obj2)
        
        # Load obj2 from obj1's slot 0 (should trigger barrier)
        print("Loading obj2 from obj1.slots[0]...")
        loaded_obj = obj1.load(0)
        
        if loaded_obj is not obj2:
            raise Exception("Loaded object does not match stored object!")
            
        print("Load Barrier test passed!")
    except Exception as e:
        print(f"Test failed: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
elif "unittest" in sys.modules:
    # If imported by unittest discovery, run main
    main()

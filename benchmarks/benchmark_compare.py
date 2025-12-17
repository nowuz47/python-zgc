import sys
import os
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import pyzgc
import time
import gc

# Number of objects to allocate
N = 1000000

class StandardObject:
    def __init__(self):
        self.slots = [None] * 10
    
    def store(self, i, val):
        self.slots[i] = val
        
    def load(self, i):
        return self.slots[i]

def benchmark_standard():
    print(f"Standard GC: Allocating {N} objects...")
    start = time.time()
    objects = [StandardObject() for _ in range(N)]
    end = time.time()
    print(f"Standard GC Allocation Time: {end - start:.4f} seconds")
    
    print("Standard GC: Accessing objects...")
    start = time.time()
    for obj in objects:
        obj.store(0, obj)
        _ = obj.load(0)
    end = time.time()
    print(f"Standard GC Access Time: {end - start:.4f} seconds")
    
    # Force GC
    start = time.time()
    del objects
    gc.collect()
    end = time.time()
    print(f"Standard GC Collection Time: {end - start:.4f} seconds")

def benchmark_pyzgc():
    print(f"\npyzgc: Allocating {N} objects...")
    start = time.time()
    # We store them in a list to keep them alive, but pyzgc manages their internal memory
    objects = [pyzgc.Object() for _ in range(N)]
    end = time.time()
    print(f"pyzgc Allocation Time: {end - start:.4f} seconds")
    
    print("pyzgc: Accessing objects (with Load Barrier)...")
    start = time.time()
    for obj in objects:
        obj.store(0, obj)
        _ = obj.load(0)
    end = time.time()
    print(f"pyzgc Access Time: {end - start:.4f} seconds")
    
    # Note: pyzgc currently doesn't implement free/reclaim in this prototype,
    # so we can't measure collection time fairly yet.

if __name__ == "__main__":
    # Disable print in zbarrier for benchmark to be fair (I/O is slow)
    # We'll assume the user recompiles or we accept the I/O overhead if it's small,
    # but actually the previous zbarrier.c has a printf. We should remove it for benchmarking.
    
    benchmark_standard()
    benchmark_pyzgc()

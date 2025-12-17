import time
import os
import psutil
import sys
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import pyzgc
import gc

# Standard Python Object
class StandardObject:
    def __init__(self):
        self.slot = None

def get_memory_usage():
    process = psutil.Process(os.getpid())
    return process.memory_info().rss / (1024 * 1024) # MB

def benchmark_efficiency():
    NUM_OBJECTS = 10000000
    
    print(f"Benchmarking Efficiency with {NUM_OBJECTS} objects...\n")
    
    # --- Standard GC ---
    gc.collect()
    mem_before = get_memory_usage()
    start_time = time.time()
    
    std_objects = [StandardObject() for _ in range(NUM_OBJECTS)]
    
    end_time = time.time()
    mem_after = get_memory_usage()
    
    std_alloc_time = end_time - start_time
    std_mem_usage = mem_after - mem_before
    std_throughput = NUM_OBJECTS / std_alloc_time
    
    print(f"[Standard GC]")
    print(f"  Allocation Time: {std_alloc_time:.4f} s")
    print(f"  Throughput:      {std_throughput:,.0f} ops/s")
    print(f"  Memory Usage:    {std_mem_usage:.2f} MB")
    print(f"  Per Object:      {std_mem_usage * 1024 * 1024 / NUM_OBJECTS:.0f} bytes")
    
    # Cleanup
    del std_objects
    gc.collect()
    
    print("-" * 40)
    
    # --- pyzgc ---
    # Reset heap if possible? No, just allocate new.
    mem_before = get_memory_usage()
    start_time = time.time()
    
    # pyzgc objects
    # Note: We store them in a list to keep them alive
    z_objects = []
    start_time = time.time()
    for _ in range(NUM_OBJECTS):
        z_objects.append(pyzgc.Object())
        
    end_time = time.time()
    mem_after = get_memory_usage()
    
    z_alloc_time = end_time - start_time
    z_mem_usage = mem_after - mem_before
    z_throughput = NUM_OBJECTS / z_alloc_time
    
    print(f"[pyzgc]")
    print(f"  Allocation Time: {z_alloc_time:.4f} s")
    print(f"  Throughput:      {z_throughput:,.0f} ops/s")
    print(f"  Memory Usage:    {z_mem_usage:.2f} MB")
    print(f"  Per Object:      {z_mem_usage * 1024 * 1024 / NUM_OBJECTS:.0f} bytes")
    
    print("-" * 40)
    print("Comparison (pyzgc vs Standard):")
    print(f"  Throughput: {z_throughput / std_throughput:.2f}x")
    print(f"  Memory:     {z_mem_usage / std_mem_usage:.2f}x (Lower is better if < 1)")

if __name__ == "__main__":
    benchmark_efficiency()

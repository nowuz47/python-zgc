import time
import threading
import psutil
import os
import gc
import sys
import platform

# Configuration
NUM_OBJECTS = 10_000_000
NUM_THREADS = 4
DURATION_SECONDS = 5

# Parse args early to configure environment
import argparse
parser = argparse.ArgumentParser()
parser.add_argument('--json', action='store_true')
parser.add_argument('--no-pyzgc', action='store_true')
args = parser.parse_args()

# Try to import pyzgc
HAS_PYZGC = False
if not args.no_pyzgc:
    try:
        import pyzgc
        HAS_PYZGC = True
    except ImportError:
        pass

def get_memory_usage():
    process = psutil.Process(os.getpid())
    return process.memory_info().rss / 1024 / 1024  # MB

class Benchmark:
    def __init__(self, name):
        self.name = name
        self.results = {}

    def run(self):
        print(f"--- Running Benchmark: {self.name} ---")
        print(f"Python: {sys.version.split()[0]} ({platform.python_implementation()})")
        print(f"GIL Enabled: {getattr(sys, '_is_gil_enabled', lambda: True)() if hasattr(sys, '_is_gil_enabled') else True}")
        print(f"pyzgc Enabled: {HAS_PYZGC}")
        
        self.measure_allocation()
        if HAS_PYZGC:
            self.measure_pyzgc_allocation()
        self.measure_thread_scaling()
        self.measure_json_parsing()
        self.measure_graph_exploration()
        self.measure_web_requests()
        
        print("-" * 40)
        return self.results

    def measure_allocation(self):
        print(f"[Test] Allocation ({NUM_OBJECTS} objects)...")
        gc.collect()
        if HAS_PYZGC: pyzgc.gc()
        start_mem = get_memory_usage()
        start_time = time.time()
        
        # Allocate a list of objects
        class SimpleObj:
            def __init__(self, x):
                self.x = x
        
        objs = [SimpleObj(i) for i in range(NUM_OBJECTS)]
        
        end_time = time.time()
        peak_mem = get_memory_usage()
        
        duration = end_time - start_time
        print(f"  Time: {duration:.4f}s")
        print(f"  Memory Overhead: {peak_mem - start_mem:.2f} MB")
        
        self.results['allocation_time'] = duration
        self.results['allocation_mem'] = peak_mem - start_mem
        
        # Cleanup
        del objs
        gc.collect()
        if HAS_PYZGC:
            pyzgc.gc()

    def measure_pyzgc_allocation(self):
        print(f"[Test] pyzgc.Object Allocation ({NUM_OBJECTS} objects)...")
        gc.collect()
        pyzgc.gc()
        start_mem = get_memory_usage()
        start_time = time.time()
        
        # Allocate pyzgc objects
        objs = [pyzgc.Object() for _ in range(NUM_OBJECTS)]
        
        end_time = time.time()
        peak_mem = get_memory_usage()
        
        duration = end_time - start_time
        print(f"  Time: {duration:.4f}s")
        print(f"  Memory Overhead: {peak_mem - start_mem:.2f} MB")
        
        self.results['pyzgc_allocation_time'] = duration
        self.results['pyzgc_allocation_mem'] = peak_mem - start_mem
        
        # Cleanup
        del objs
        pyzgc.gc()

    def measure_thread_scaling(self):
        print(f"[Test] Thread Scaling ({NUM_THREADS} threads)...")
        
        def worker(count):
            # CPU bound work mixed with allocation
            l = []
            for i in range(count):
                l.append(i * i)
                if i % 1000 == 0:
                    l = [] # Churn
        
        start_time = time.time()
        threads = []
        work_per_thread = 5_000_000 // NUM_THREADS
        
        for _ in range(NUM_THREADS):
            t = threading.Thread(target=worker, args=(work_per_thread,))
            threads.append(t)
            t.start()
            
        for t in threads:
            t.join()
            
        end_time = time.time()
        duration = end_time - start_time
        print(f"  Time: {duration:.4f}s")
        self.results['threading_time'] = duration

    def measure_json_parsing(self):
        print(f"[Test] Massive JSON Parsing...")
        import json
        
        # Generate a large dict structure
        data = []
        for i in range(100_000):
            data.append({
                "id": i,
                "name": f"item_{i}",
                "tags": ["a", "b", "c"],
                "meta": {"x": i, "y": i*2}
            })
        json_str = json.dumps(data)
        del data
        gc.collect()
        if HAS_PYZGC: pyzgc.gc()
        
        start_mem = get_memory_usage()
        start_time = time.time()
        
        parsed = json.loads(json_str)
        
        end_time = time.time()
        peak_mem = get_memory_usage()
        
        duration = end_time - start_time
        print(f"  Time: {duration:.4f}s")
        print(f"  Memory Overhead: {peak_mem - start_mem:.2f} MB")
        self.results['json_time'] = duration
        self.results['json_mem'] = peak_mem - start_mem
        
        del parsed
        del json_str
        gc.collect()

    def measure_graph_exploration(self):
        print(f"[Test] Massive Graph Exploration (Standard)...")
        
        class Node:
            def __init__(self, val):
                self.val = val
                self.children = []
        
        # Build Graph
        nodes = [Node(i) for i in range(100_000)]
        for i in range(100_000 - 1):
            nodes[i].children.append(nodes[i+1]) # Linked list-ish
            if i % 2 == 0 and i < 90_000:
                nodes[i].children.append(nodes[i+100]) # Random links
        
        gc.collect()
        if HAS_PYZGC: pyzgc.gc()
        
        start_time = time.time()
        
        # Traverse
        visited = set()
        stack = [nodes[0]]
        count = 0
        while stack:
            n = stack.pop()
            if n in visited: continue
            visited.add(n)
            count += 1
            for c in n.children:
                stack.append(c)
                
        end_time = time.time()
        print(f"  Time: {end_time - start_time:.4f}s (Visited {count})")
        self.results['graph_time'] = end_time - start_time
        
        del nodes
        del visited
        del stack
        gc.collect()
        
        if HAS_PYZGC:
            print(f"[Test] Massive Graph Exploration (pyzgc.Object)...")
            # Simulate Node using pyzgc.Object
            # We use store/load to simulate fields. 0=val, 1=next_node, 2=other_node
            
            p_nodes = [pyzgc.Object() for _ in range(100_000)]
            for i in range(100_000):
                p_nodes[i][0] = i # val
                
            for i in range(100_000 - 1):
                # We can't easily store a list in pyzgc.Object slots without a wrapper, 
                # so we'll simulate a fixed degree graph for fair comparison or just use slots
                p_nodes[i][1] = p_nodes[i+1]
                if i % 2 == 0 and i < 90_000:
                    p_nodes[i][2] = p_nodes[i+100]
            
            pyzgc.gc()
            start_time = time.time()
            
            # Traverse
            # We need a way to track visited. pyzgc objects are hashable? 
            # If not, we use IDs.
            visited_ids = set()
            stack = [p_nodes[0]]
            count = 0
            while stack:
                n = stack.pop()
                nid = id(n) # Python ID
                if nid in visited_ids: continue
                visited_ids.add(nid)
                count += 1
                
                # Load children
                c1 = n[1]
                if c1: stack.append(c1)
                c2 = n[2]
                if c2: stack.append(c2)
            
            end_time = time.time()
            print(f"  Time: {end_time - start_time:.4f}s (Visited {count})")
            self.results['pyzgc_graph_time'] = end_time - start_time
            
            del p_nodes
            pyzgc.gc()

    def measure_web_requests(self):
        print(f"[Test] Simulated Web Request Handling...")
        
        class Request:
            def __init__(self, headers, body):
                self.headers = headers
                self.body = body
        
        class Response:
            def __init__(self, status, body):
                self.status = status
                self.body = body
                
        def handle_request(req):
            # Simulate parsing/processing
            user_agent = req.headers.get("User-Agent", "")
            data = req.body.upper()
            return Response(200, data)
            
        requests = [Request({"User-Agent": f"Agent_{i}"}, f"data_{i}") for i in range(50_000)]
        
        gc.collect()
        if HAS_PYZGC: pyzgc.gc()
        
        start_mem = get_memory_usage()
        start_time = time.time()
        
        responses = []
        for r in requests:
            responses.append(handle_request(r))
            
        end_time = time.time()
        peak_mem = get_memory_usage()
        
        duration = end_time - start_time
        print(f"  Time: {duration:.4f}s")
        print(f"  Memory Overhead: {peak_mem - start_mem:.2f} MB")
        self.results['web_time'] = duration
        self.results['web_mem'] = peak_mem - start_mem
        
        del requests
        del responses
        gc.collect()

if __name__ == "__main__":
    b = Benchmark("Standard Suite")
    results = b.run()
    
    if args.json:
        import json
        print("JSON_RESULT:" + json.dumps(results))

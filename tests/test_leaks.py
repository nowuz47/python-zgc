import unittest
import subprocess
import sys
import os

class TestLeaks(unittest.TestCase):
    def test_memory_stability(self):
        """
        Run a memory leak test in a separate process to avoid unittest interference.
        The script runs a stress loop and asserts memory stability.
        """
        script = """
import pyzgc
import gc
import sys
import os
import psutil

def run_test():
    process = psutil.Process(os.getpid())
    
    # Warmup
    for _ in range(5):
        l = []
        for _ in range(1000):
            l.append(pyzgc.Object())
        l.clear()
        gc.collect()

    start_mem = process.memory_info().rss / 1024 / 1024
    print(f"Start Mem: {start_mem:.2f} MB")
    
    # Stress loop
    iterations = 20
    objects_per_iter = 10000
    
    for i in range(iterations):
        l = []
        for _ in range(objects_per_iter):
            l.append(pyzgc.Object())
        l.clear()
        # Explicitly delete list to help GC
        del l
        gc.collect()
        pyzgc.gc()
        pyzgc.gc()
        
    end_mem = process.memory_info().rss / 1024 / 1024
    print(f"End Mem:   {end_mem:.2f} MB")
    
    diff_mb = end_mem - start_mem
    print(f"Diff:      {diff_mb:.2f} MB")
    
    if diff_mb > 10.0:
        print("FAIL: Memory usage grew significantly")
        sys.exit(1)
    else:
        print("PASS: Memory usage stable")
        sys.exit(0)

if __name__ == "__main__":
    run_test()
"""
        # Run the script in a subprocess
        env = os.environ.copy()
        env["PYTHONPATH"] = os.getcwd()
        
        result = subprocess.run(
            [sys.executable, "-c", script],
            env=env,
            capture_output=True,
            text=True
        )
        
        print(result.stdout)
        print(result.stderr)
        
        self.assertEqual(result.returncode, 0, f"Memory leak detected: {result.stdout}")

if __name__ == "__main__":
    unittest.main()

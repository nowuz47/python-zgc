import pyzgc
import threading
import time

def worker(n):
    for i in range(1000):
        # Allocate objects concurrently
        obj = pyzgc.Object()
        # Do some work
        obj.store(0, obj)

print("Starting Multi-threaded Allocation Test...")
threads = []
start = time.time()

for i in range(10):
    t = threading.Thread(target=worker, args=(i,))
    threads.append(t)
    t.start()

for t in threads:
    t.join()

end = time.time()
print(f"Test finished in {end - start:.4f} seconds.")
print("No crashes observed.")

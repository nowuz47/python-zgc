import pyzgc

print("Testing ZObject repr...")
z = pyzgc.Object()
print(f"Repr: {z}")

# Verify format
r = repr(z)
if "<pyzgc.Object at 0x" in r and "gen=0" in r:
    print("SUCCESS: Repr format correct.")
else:
    print(f"FAILURE: Unexpected repr format: {r}")

# Test freed object repr (if possible to simulate safely without crashing)
# We can't easily test freed object repr because accessing it usually means segfault or it's gone.
# But we can verify that valid objects show up correctly.

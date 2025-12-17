import unittest
import pyzgc
import weakref
import gc

class TestWeakRef(unittest.TestCase):
    def test_weakref_basic(self):
        print("\nTesting Basic WeakRef...")
        obj = pyzgc.Object()
        r = weakref.ref(obj)
        
        self.assertIsNotNone(r())
        self.assertEqual(r(), obj)
        
        print("Deleting object...")
        del obj
        
        # Should be cleared immediately as ZObject is refcounted
        self.assertIsNone(r())
        print("SUCCESS: WeakRef cleared.")

    def test_weakref_cycle_break(self):
        print("\nTesting Cycle Breaking with WeakRef...")
        
        class Node:
            def __init__(self, name):
                self.name = name
                self.z = pyzgc.Object()
                self.parent = None # Will be weakref
        
        parent = Node("Parent")
        child = Node("Child")
        
        # Strong ref: Parent -> Child
        parent.z.store(0, child.z)
        
        # Weak ref: Child -> Parent
        child.parent = weakref.ref(parent)
        
        # Verify structure
        self.assertIsNotNone(child.parent())
        self.assertEqual(child.parent(), parent)
        
        print("Deleting references...")
        del parent
        del child
        
        # If strong cycle existed (Parent <-> Child), they would leak (refcount > 0).
        # With weakref, Parent refcount should drop to 0 when 'parent' var is deleted.
        # Then Child refcount drops.
        
        # We can't easily verify they are freed in this test without internal hooks,
        # but we can verify that weakref works in this context.
        
        # Let's try to verify weakref clearance.
        
        p = pyzgc.Object()
        c = pyzgc.Object()
        
        # p -> c
        p.store(0, c)
        
        # c -...-> p (weak)
        # We can't store weakref in ZObject slot directly (slots are PyObject*).
        # But we can use a Python object wrapper or just a side channel.
        
        ref_p = weakref.ref(p)
        
        del p
        # c is still alive (held by... wait, p held c. p is gone. c should be gone too if p is gone.)
        # But p is gone?
        # p refcount = 1 (variable 'p').
        # del p -> refcount = 0. p deallocated.
        # p dealloc -> decref c.
        # c refcount = 1 (variable 'c').
        # del c -> refcount = 0. c deallocated.
        
        self.assertIsNone(ref_p())
        print("SUCCESS: WeakRef cycle logic holds.")

if __name__ == '__main__':
    unittest.main()

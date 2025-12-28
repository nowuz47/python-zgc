import unittest
import ctypes
import sys
import os
import sysconfig

# Load the extension module using ctypes to access exported symbols
def load_pyzgc_dll():
    # Find the compiled extension file
    suffix = sysconfig.get_config_var('EXT_SUFFIX')
    if not suffix:
        suffix = ".so" # Fallback

    # Look in the current directory or build directory
    # Assuming inplace build for now
    for root, dirs, files in os.walk('.'):
        for file in files:
            if file.endswith(suffix) and "pyzgc" in file:
                path = os.path.join(root, file)
                return ctypes.CDLL(path)
    return None

class ZJIT_Context(ctypes.Structure):
    _fields_ = [
        ("good_color_ptr", ctypes.POINTER(ctypes.c_uint64)),
        ("fix_pointer_func", ctypes.c_void_p),
        ("mask_marked0", ctypes.c_uint64),
        ("mask_marked1", ctypes.c_uint64),
        ("mask_remapped", ctypes.c_uint64),
    ]

class TestJITInterface(unittest.TestCase):
    def test_jit_context_access(self):
        lib = load_pyzgc_dll()
        if not lib:
            self.skipTest("Could not find pyzgc extension DLL")

        # Define the function signature
        try:
            get_context = lib.zjit_get_context
            get_context.restype = ctypes.POINTER(ZJIT_Context)
            get_context.argtypes = []
        except AttributeError:
            self.fail("zjit_get_context symbol not found in extension")

        # Call the function
        context_ptr = get_context()
        self.assertTrue(context_ptr, "Returned context pointer is NULL")
        
        context = context_ptr.contents
        
        # Verify fields
        print(f"\n[JIT] Good Color Ptr: {context.good_color_ptr}")
        print(f"[JIT] Fix Func Ptr: {context.fix_pointer_func}")
        print(f"[JIT] Mask Marked0: {hex(context.mask_marked0)}")
        
        self.assertTrue(context.good_color_ptr, "good_color_ptr is NULL")
        self.assertTrue(context.fix_pointer_func, "fix_pointer_func is NULL")
        self.assertNotEqual(context.mask_marked0, 0, "mask_marked0 is 0")
        self.assertNotEqual(context.mask_marked1, 0, "mask_marked1 is 0")

        # Verify we can read the good color
        good_color = context.good_color_ptr.contents.value
        print(f"[JIT] Current Good Color: {hex(good_color)}")
        self.assertIn(good_color, [context.mask_marked0, context.mask_marked1], 
                      "Good color has unexpected value")

if __name__ == '__main__':
    unittest.main()

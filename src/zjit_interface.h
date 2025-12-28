#ifndef ZJIT_INTERFACE_H
#define ZJIT_INTERFACE_H

#include <stdint.h>

// Context structure for JITs to inline barriers
typedef struct {
  // Pointer to the global zgc_good_color variable
  // JIT should dereference this to get the current good color mask.
  uintptr_t *good_color_ptr;

  // Function pointer to the slow path (zbarrier_fix_pointer)
  // JIT should call this if the color check fails.
  // Signature: void zbarrier_fix_pointer(void **ptr_addr);
  void (*fix_pointer_func)(void **);

  // Constants for inlining (masks)
  uintptr_t mask_marked0;
  uintptr_t mask_marked1;
  uintptr_t mask_remapped;
} ZJIT_Context;

// Exported function to get the context
// This should be called via dlsym or equivalent by the JIT.
ZJIT_Context *zjit_get_context(void);

#endif // ZJIT_INTERFACE_H

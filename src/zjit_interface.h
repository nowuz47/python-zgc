#ifndef ZJIT_INTERFACE_H
#define ZJIT_INTERFACE_H

#include <stdbool.h>
#include <stdint.h>

// This header defines the contract for JIT compilers (e.g., PyPy, Cinder)
// to inline the ZGC Load Barrier.

// Global variable holding the current "Good Color" mask.
// JITs should read this variable directly.
extern uintptr_t zgc_good_color;

// Fast-Path Load Barrier Logic
//
// Usage:
//   void *ptr = ...; // Load pointer from object
//   if (!ZJIT_CheckBarrier(ptr)) {
//       ptr = zbarrier_fix_pointer(ptr); // Slow path call
//   }
//   // Use ptr...

#define ZPOINTER_MARKED0_BIT (1ULL << 62)
#define ZPOINTER_MARKED1_BIT (1ULL << 63)
#define ZPOINTER_COLOR_MASK (ZPOINTER_MARKED0_BIT | ZPOINTER_MARKED1_BIT)

static inline bool ZJIT_CheckBarrier(void *ptr) {
  // Check if the pointer has the current "Good Color"
  return ((uintptr_t)ptr & zgc_good_color) != 0;
}

// Prototype for the slow path (implemented in zbarrier.c)
// JITs should call this if ZJIT_CheckBarrier returns false.
void *zbarrier_fix_pointer_jit(void *ptr);

#endif // ZJIT_INTERFACE_H

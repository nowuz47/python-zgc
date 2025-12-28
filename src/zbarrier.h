#ifndef ZBARRIER_H
#define ZBARRIER_H

#include "zobject.h"
#include <Python.h>

#include "zheap.h"

// Branch prediction hints
#if defined(__GNUC__) || defined(__clang__)
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

// Fix Pointer: Ensures the ZObject's body pointer is up to date
// Defined in zbarrier.c
void zbarrier_fix_pointer(ZObject *zobj);

// Load Barrier: Ensures the object is valid to be returned to Python
// Inlined for performance (Critical Path)
static inline PyObject *zbarrier_load(PyObject *obj) {
  if (UNLIKELY(obj == NULL))
    return NULL;

  // Check if it's a ZObject
  // We use Py_TYPE(obj) which is fast.
  // ZObjectType is extern in zobject.h
  if (Py_TYPE(obj) == &ZObjectType) {
    ZObject *zobj = (ZObject *)obj;

    // Check color (Fast Path)
    // zgc_good_color is extern in zheap.h
    if (UNLIKELY(!Z_HAS_COLOR(zobj->body, zgc_good_color))) {
      zbarrier_fix_pointer(zobj);
    }
  }

  return obj;
}

// Optimized Load Barrier: Skips NULL check
// Use this when you are certain obj is not NULL
static inline PyObject *zbarrier_load_nonnull(PyObject *obj) {
  // Check if it's a ZObject
  if (Py_TYPE(obj) == &ZObjectType) {
    ZObject *zobj = (ZObject *)obj;

    // Check color (Fast Path)
    if (UNLIKELY(!Z_HAS_COLOR(zobj->body, zgc_good_color))) {
      zbarrier_fix_pointer(zobj);
    }
  }
  return obj;
}

#endif

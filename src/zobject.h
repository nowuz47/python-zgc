#ifndef ZOBJECT_H
#define ZOBJECT_H

#include <Python.h>

#define ZOBJECT_SLOTS 10

// ZBody lives in the ZHeap and contains the actual data.
// It does NOT have PyObject_HEAD because it's not a Python object itself.
typedef struct {
  PyObject *slots[ZOBJECT_SLOTS];
  // We might add a forwarding pointer here later
  // struct ZBody* forwarding_ptr;
} ZBody;

// ZObject is the Python wrapper (Handle).
// It lives in the CPython heap (managed by standard malloc/free or pool).
// It points to the ZBody in the ZHeap.
typedef struct {
  PyObject_HEAD ZBody *body;
  PyObject *weakreflist; // List of weak references to this object
} ZObject;

extern PyTypeObject ZObjectType;

#endif

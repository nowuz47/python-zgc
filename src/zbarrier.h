#ifndef ZBARRIER_H
#define ZBARRIER_H

#include "zobject.h"
#include <Python.h>

// Load Barrier: Ensures the object is valid to be returned to Python
PyObject *zbarrier_load(PyObject *obj);

// Fix Pointer: Ensures the ZObject's body pointer is up to date (correct color
// and address)
void zbarrier_fix_pointer(ZObject *zobj);

#endif

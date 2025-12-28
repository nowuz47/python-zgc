#define PY_SSIZE_T_CLEAN
#include "zbarrier.h"
#include "zgc.h"
#include "zheap.h"
#include "zobject.h"
#include <Python.h>

static PyObject *pyzgc_allocate(PyObject *self, PyObject *args) {
  Py_ssize_t size;
  if (!PyArg_ParseTuple(args, "n", &size))
    return NULL;

  void *ptr = zheap_alloc((size_t)size, ZGEN_YOUNG);
  if (!ptr) {
    return PyErr_NoMemory();
  }
  return PyLong_FromVoidPtr(ptr);
}

static PyObject *pyzgc_start_gc(PyObject *self, PyObject *args) {
  zgc_start_thread();
  Py_RETURN_NONE;
}

static PyObject *pyzgc_stop_gc(PyObject *self, PyObject *args) {
  zgc_stop_thread();
  Py_RETURN_NONE;
}

static PyObject *pyzgc_add_root(PyObject *self, PyObject *args) {
  PyObject *obj;
  if (!PyArg_ParseTuple(args, "O", &obj))
    return NULL;
  zgc_add_root(obj);
  Py_RETURN_NONE;
}

static PyObject *pyzgc_is_marked(PyObject *self, PyObject *args) {
  PyObject *obj;
  if (!PyArg_ParseTuple(args, "O", &obj))
    return NULL;

  bool marked = zgc_check_marked(obj);
  if (marked) {
    Py_RETURN_TRUE;
  } else {
    Py_RETURN_FALSE;
  }
}

static PyObject *pyzgc_gc(PyObject *self, PyObject *args) {
  zgc_run_cycle();
  Py_RETURN_NONE;
}

static PyObject *pyzgc_minor_gc(PyObject *self, PyObject *args) {
  zgc_minor_cycle();
  Py_RETURN_NONE;
}

static PyObject *pyzgc_get_body_address(PyObject *self, PyObject *args) {
  PyObject *obj;
  if (!PyArg_ParseTuple(args, "O", &obj))
    return NULL;

  if (Py_TYPE(obj) == &ZObjectType) {
    ZObject *zobj = (ZObject *)obj;
    return PyLong_FromVoidPtr(zobj->body);
  }
  Py_RETURN_NONE;
}

// Testing Helpers
extern bool zheap_is_old(void *obj);
extern bool zheap_is_young(void *obj);

static PyObject *pyzgc_is_old(PyObject *self, PyObject *obj) {
  if (Py_TYPE(obj) != &ZObjectType) {
    Py_RETURN_FALSE;
  }
  ZObject *zobj = (ZObject *)obj;
  // Barrier: Ensure we are looking at the current location
  zbarrier_fix_pointer(zobj);

  if (zobj->body && zheap_is_old(zobj->body)) {
    Py_RETURN_TRUE;
  }
  Py_RETURN_FALSE;
}

static PyObject *pyzgc_is_young(PyObject *self, PyObject *obj) {
  if (Py_TYPE(obj) != &ZObjectType) {
    Py_RETURN_FALSE;
  }
  ZObject *zobj = (ZObject *)obj;
  // Barrier: Ensure we are looking at the current location
  zbarrier_fix_pointer(zobj);

  if (zobj->body && zheap_is_young(zobj->body)) {
    Py_RETURN_TRUE;
  }
  Py_RETURN_FALSE;
}

static PyObject *pyzgc_trigger_minor_gc(PyObject *self, PyObject *args) {
  zgc_minor_cycle();
  Py_RETURN_NONE;
}

static PyObject *pyzgc_trigger_full_gc(PyObject *self, PyObject *args) {
  zgc_run_cycle();
  Py_RETURN_NONE;
}

extern void zbarrier_enable_signal_mode(void);
extern void zbarrier_disable_signal_mode(void);

static PyObject *pyzgc_enable_signal_barrier(PyObject *self, PyObject *args) {
  zbarrier_enable_signal_mode();
  Py_RETURN_NONE;
}

static PyObject *pyzgc_disable_signal_barrier(PyObject *self, PyObject *args) {
  zbarrier_disable_signal_mode();
  Py_RETURN_NONE;
}

static PyMethodDef PyZGCMethods[] = {
    {"is_old", pyzgc_is_old, METH_O, "Check if object is in Old Generation"},
    {"is_young", pyzgc_is_young, METH_O,
     "Check if object is in Young Generation"},
    {"trigger_minor_gc", pyzgc_trigger_minor_gc, METH_NOARGS,
     "Trigger Minor GC"},
    {"trigger_full_gc", pyzgc_trigger_full_gc, METH_NOARGS, "Trigger Full GC"},
    {"allocate", pyzgc_allocate, METH_VARARGS, "Allocate memory in ZGC heap."},
    {"start_gc", pyzgc_start_gc, METH_NOARGS,
     "Start the background GC thread."},
    {"stop_gc", pyzgc_stop_gc, METH_NOARGS, "Stop the background GC thread."},
    {"add_root", pyzgc_add_root, METH_VARARGS,
     "Add an object to the GC root set (for testing)."},
    {"is_marked", pyzgc_is_marked, METH_VARARGS,
     "Check if an object is marked (for testing)."},
    {"get_body_address", pyzgc_get_body_address, METH_VARARGS,
     "Get the address of the ZBody (for testing relocation)."},
    {"gc", pyzgc_gc, METH_NOARGS, "Run a synchronous Full GC cycle."},
    {"minor_gc", pyzgc_minor_gc, METH_NOARGS,
     "Run a synchronous Minor GC cycle."},
    {"enable_signal_barrier", (PyCFunction)pyzgc_enable_signal_barrier,
     METH_NOARGS, "Enable signal-based barriers (default)."},
    {"disable_signal_barrier", (PyCFunction)pyzgc_disable_signal_barrier,
     METH_NOARGS, "Disable signal-based barriers (revert to software checks)."},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef pyzgcmodule = {
    PyModuleDef_HEAD_INIT, "pyzgc", "Python ZGC Extension", -1, PyZGCMethods};

PyMODINIT_FUNC PyInit_pyzgc(void) {
  PyObject *m;

  zheap_init();

// Enable Signal-Based Barriers by default (Linux only)
#ifdef __linux__
  zbarrier_enable_signal_mode();
#endif

  if (PyType_Ready(&ZObjectType) < 0)
    return NULL;

  m = PyModule_Create(&pyzgcmodule);
  if (m == NULL)
    return NULL;

  Py_INCREF(&ZObjectType);
  if (PyModule_AddObject(m, "Object", (PyObject *)&ZObjectType) < 0) {
    Py_DECREF(&ZObjectType);
    Py_DECREF(m);
    return NULL;
  }

  return m;
}

// JIT Interface Implementation
#include "zjit_interface.h"

ZJIT_Context *zjit_get_context(void) {
  static ZJIT_Context context;
  static bool initialized = false;

  if (!initialized) {
    context.good_color_ptr = &zgc_good_color;
    context.fix_pointer_func = (void (*)(void **))zbarrier_fix_pointer;
    context.mask_marked0 = ZPOINTER_MARKED0_BIT;
    context.mask_marked1 = ZPOINTER_MARKED1_BIT;
    context.mask_remapped = ZPOINTER_REMAPPED_BIT;
    initialized = true;
  }

  return &context;
}

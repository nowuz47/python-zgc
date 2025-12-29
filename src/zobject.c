#define PY_SSIZE_T_CLEAN
#include "zobject.h"
#include "zbarrier.h"
#include "zheap.h"
#include <Python.h>
#include <pthread.h>
#include <structmember.h>

extern bool zbarrier_is_signal_mode(void);

// Global list of all ZObjects (Roots)
static ZObject *zobject_list_head = NULL;
static pthread_mutex_t zobject_list_lock = PTHREAD_MUTEX_INITIALIZER;

// Removed Freelist to support GC correctly
// static __thread ZObject *zobject_freelist[ZOBJECT_FREELIST_MAX];
// static __thread int zobject_freelist_size = 0;

static int ZObject_traverse(ZObject *self, visitproc visit, void *arg) {
  // printf("ZObject_traverse %p\n", self);
  if (self->body) {
    for (int i = 0; i < ZOBJECT_SLOTS; i++) {
      Py_VISIT(self->body->slots[i]);
    }
  }
  return 0;
}

static int ZObject_clear(ZObject *self) {
  if (self->body) {
    for (int i = 0; i < ZOBJECT_SLOTS; i++) {
      Py_CLEAR(self->body->slots[i]);
    }
  }
  return 0;
}

static void ZObject_dealloc(ZObject *self) {
  // fprintf(stderr, "ZObject_dealloc %p\n", self);
  PyObject_GC_UnTrack(self);

  // Remove from global list
  pthread_mutex_lock(&zobject_list_lock);
  if (self->prev)
    self->prev->next = self->next;
  if (self->next)
    self->next->prev = self->prev;
  if (zobject_list_head == self)
    zobject_list_head = self->next;
  pthread_mutex_unlock(&zobject_list_lock);

  if (self->weakreflist != NULL) {
    PyObject_ClearWeakRefs((PyObject *)self);
  }

  ZObject_clear(self);

  Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *ZObject_new(PyTypeObject *type, PyObject *args,
                             PyObject *kwds) {
  ZObject *self = (ZObject *)type->tp_alloc(type, 0);
  if (self == NULL) {
    return NULL;
  }

  self->weakreflist = NULL;

  // Allocate Body from ZHeap
  self->body = (ZBody *)zheap_alloc_inline(sizeof(ZBody));
  if (self->body == NULL) {
    Py_DECREF(self);
    return PyErr_NoMemory();
  }

  // Add to global list
  pthread_mutex_lock(&zobject_list_lock);
  self->next = zobject_list_head;
  self->prev = NULL;
  if (zobject_list_head)
    zobject_list_head->prev = self;
  zobject_list_head = self;
  pthread_mutex_unlock(&zobject_list_lock);

  // PyObject_GC_Track(self); // Assertion failed: already tracked?
  return (PyObject *)self;
}

// Expose list for ZGC
ZObject *zobject_get_list_head(void) { return zobject_list_head; }
void zobject_lock_list(void) { pthread_mutex_lock(&zobject_list_lock); }
void zobject_unlock_list(void) { pthread_mutex_unlock(&zobject_list_lock); }

static PyObject *ZObject_store(ZObject *self, PyObject *const *args,
                               Py_ssize_t nargs) {
  if (nargs != 2) {
    PyErr_SetString(PyExc_TypeError,
                    "store() takes exactly 2 arguments (index, value)");
    return NULL;
  }

  int index = (int)PyLong_AsLong(args[0]);
  if (index == -1 && PyErr_Occurred()) {
    return NULL;
  }

  PyObject *value = args[1];

  // Barrier: Ensure self->body is up to date (Load Barrier for self)
  if (!Z_HAS_COLOR(self->body, zgc_good_color)) {
    if (!zbarrier_is_signal_mode()) {
      zbarrier_fix_pointer(self);
    }
  }

  if (!self->body) {
    PyErr_SetString(PyExc_RuntimeError, "ZObject has no body");
    return NULL;
  }

  // Barrier: Ensure self->body is good before writing
  // OPTIMIZATION: Redundant barrier removed (checked at start of function)
  // if (!Z_HAS_COLOR(self->body, zgc_good_color)) {
  //   zbarrier_fix_pointer(self);
  // }

  if (index < 0 || index >= ZOBJECT_SLOTS) {
    PyErr_SetString(PyExc_IndexError, "Slot index out of range");
    return NULL;
  }

  // Barrier: Ensure self->body is good before writing
  // OPTIMIZATION: Redundant barrier removed (checked at start of function)
  // if (!Z_HAS_COLOR(self->body, zgc_good_color)) {
  //   zbarrier_fix_pointer(self);
  // }

  // Mask pointer before access
  ZBody *body = (ZBody *)Z_ADDRESS(self->body);

  PyObject *old = body->slots[index];
  Py_INCREF(value);
  body->slots[index] = value;
  Py_XDECREF(old);

  // Write Barrier: If self is Old and value is Young (ZObject), add to RemSet
  if (zheap_is_old(self->body) && Py_TYPE(value) == &ZObjectType) {
    ZObject *zval = (ZObject *)value;
    if (zval->body && zheap_is_young(zval->body)) {
      zremset_add(self->body);
    }
  }

  Py_RETURN_NONE;
}

static PyObject *ZObject_load(ZObject *self, PyObject *arg) {
  int index = (int)PyLong_AsLong(arg);
  if (index == -1 && PyErr_Occurred()) {
    return NULL;
  }

  if (index < 0 || index >= ZOBJECT_SLOTS) {
    PyErr_SetString(PyExc_IndexError, "Slot index out of range");
    return NULL;
  }

  // Barrier: Ensure self->body is up to date
  // We use the inline check directly for speed
  if (UNLIKELY(!Z_HAS_COLOR(self->body, zgc_good_color))) {
    if (!zbarrier_is_signal_mode()) {
      zbarrier_fix_pointer(self);
    }
  }

  // Mask pointer before access
  ZBody *body = (ZBody *)Z_ADDRESS(self->body);

  PyObject *obj = body->slots[index];
  if (obj == NULL) {
    Py_RETURN_NONE;
  }

  // The barrier might need to check if 'obj' (Handle) is valid?
  // No, 'obj' is a Handle in CPython heap. It's always valid.
  // But we might want to check if the *reference* we just loaded is good?
  // ZGC barrier is usually on the *reference* we load.
  // But here, we are loading a Handle.
  // The Handle points to a Body.
  // The *Body* might be moved.
  // But we just accessed `self->body`.
  // If `self->body` was bad (wrong color), we should have fixed it BEFORE
  // reading the slot!

  // WAIT! This is the key of ZGC.
  // The barrier is on `self->body`.
  // When we access `self->body`, we must check its color.
  // If color is bad, we fix `self->body`.
  // THEN we read the slot.

  // So `ZObject_load` should:
  // 1. Check `self->body` color.
  // 2. If bad, call slow path (Relocate/Remap) -> updates `self->body`.
  // 3. Read slot from `self->body` (now good).

  // Currently `zbarrier_load` was doing logic on the *result* object.
  // But the result object is a Handle. Handles don't move.
  // The thing that moves is `self->body`.
  // So the barrier should be on `self`.

  // Let's look at `zbarrier_load` implementation again.
  // It took `PyObject* obj` (the result).
  // It checked `obj->body`.
  // This is "Barrier on Loaded Reference".
  // This ensures that if we hold a reference to `obj`, `obj` is good.
  // But `obj` is a Handle. `obj` is always good.
  // `obj->body` might be bad.
  // By calling `zbarrier_load(obj)`, we ensure `obj->body` is fixed.
  // So when we return `obj` to the user, they can safely use it.

  // BUT, what about `self`?
  // We just used `self->body` to read `obj`.
  // If `self->body` was pointing to an evacuated page, we might be reading
  // garbage! So we need a barrier on `self` BEFORE reading `obj`.

  // So we need TWO barriers?
  // 1. Barrier on `self` to ensure we read the correct slot.
  // 2. Barrier on `obj` (result) to ensure the returned object is good?

  // Actually, if we fix `self`, we read the correct slot.
  // The slot contains `obj` (Handle).
  // `obj` (Handle) points to `obj->body`.
  // We don't access `obj->body` here. The user might later.
  // So we should barrier `obj` before returning it, so the user gets a "good"
  // object.

  // So yes, we need a barrier on `self` FIRST.
  // Let's call it `zbarrier_ensure_good(self)`.

  // For now, let's implement the "Barrier on Self" inline or helper.
  // We need to check color.

  if (!Z_HAS_COLOR(self->body, zgc_good_color)) {
    // Slow path: Fix self->body
    // We need a function for this.
    zbarrier_fix_pointer((ZObject *)self);
  }

  // Now self->body is good (or at least mapped).
  // Re-read body after fix
  body = (ZBody *)Z_ADDRESS(self->body);
  obj = body->slots[index];

  if (obj == NULL) {
    Py_RETURN_NONE;
  }

  // Now barrier on the result
  // OPTIMIZATION: We don't need to barrier the result here!
  // The result is a Handle (PyObject*). It doesn't move.
  // The only thing that moves is the Body of the object we just read from.
  // But we already fixed 'self' before reading.
  // So 'obj' is the correct Handle.
  // The user will barrier 'obj' when they access it later (if it's a ZObject).
  Py_INCREF(obj);
  return obj;
}

static PyObject *ZObject_repr(ZObject *self) {
  if (!self->body) {
    return PyUnicode_FromFormat("<pyzgc.Object at %p (freed)>", self);
  }

  // Check generation
  int gen = -1;
  if (zheap_is_young(self->body))
    gen = 0;
  else if (zheap_is_old(self->body))
    gen = 1;

  // Check forwarding (if evacuating)
  // This is tricky without locking, but for debug repr it's fine to be racy
  void *raw_body = Z_ADDRESS(self->body);
  ZPage *page = zheap_get_page(raw_body);
  char *status = "stable";

  if (page && page->is_evacuating) {
    status = "evacuating";
    // Could check if forwarded...
  }

  return PyUnicode_FromFormat("<pyzgc.Object at %p body=%p gen=%d status=%s>",
                              self, self->body, gen, status);
}

// Sequence Protocol Implementation
static PyObject *ZObject_getitem(ZObject *self, Py_ssize_t i) {
  if (i < 0 || i >= ZOBJECT_SLOTS) {
    PyErr_SetString(PyExc_IndexError, "Slot index out of range");
    return NULL;
  }

  // Barrier: Ensure self->body is up to date
  if (UNLIKELY(!Z_HAS_COLOR(self->body, zgc_good_color))) {
    zbarrier_fix_pointer(self);
  }

  ZBody *body = (ZBody *)Z_ADDRESS(self->body);
  PyObject *obj = body->slots[i];

  if (obj == NULL) {
    Py_RETURN_NONE;
  }

  // Barrier on result
  // OPTIMIZATION: Redundant barrier removed.
  Py_INCREF(obj);
  return obj;
}

static int ZObject_setitem(ZObject *self, Py_ssize_t i, PyObject *value) {
  if (i < 0 || i >= ZOBJECT_SLOTS) {
    PyErr_SetString(PyExc_IndexError, "Slot index out of range");
    return -1;
  }

  if (value == NULL) {
    PyErr_SetString(PyExc_TypeError, "Cannot delete slots");
    return -1;
  }

  // Barrier: Ensure self->body is up to date
  if (UNLIKELY(!Z_HAS_COLOR(self->body, zgc_good_color))) {
    zbarrier_fix_pointer(self);
  }

  ZBody *body = (ZBody *)Z_ADDRESS(self->body);
  PyObject *old = body->slots[i];

  Py_INCREF(value);
  body->slots[i] = value;
  Py_XDECREF(old);

  // Write Barrier
  if (zheap_is_old(self->body) && Py_TYPE(value) == &ZObjectType) {
    ZObject *zval = (ZObject *)value;
    if (zval->body && zheap_is_young(zval->body)) {
      zremset_add(self->body);
    }
  }

  return 0;
}

static PySequenceMethods ZObject_as_sequence = {
    .sq_length = 0, // Not really a sized sequence in the traditional sense, or
                    // fixed size?
    .sq_concat = 0,
    .sq_repeat = 0,
    .sq_item = (ssizeargfunc)ZObject_getitem,
    .sq_ass_item = (ssizeobjargproc)ZObject_setitem,
};

// Signal Barrier API
extern void zbarrier_enable_signal_mode(void);

static PyObject *ZObject_enable_signal_barrier(PyObject *self, PyObject *args) {
  zbarrier_enable_signal_mode();
  Py_RETURN_NONE;
}

static PyMethodDef ZObject_methods[] = {
    {"store", (PyCFunction)ZObject_store, METH_FASTCALL,
     "Store an object in a slot."},
    {"load", (PyCFunction)ZObject_load, METH_O,
     "Load an object from a slot (with barrier)."},
    {"enable_signal_barrier", (PyCFunction)ZObject_enable_signal_barrier,
     METH_NOARGS | METH_STATIC, "Enable experimental signal-based barriers"},
    {NULL}};

PyTypeObject ZObjectType = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "pyzgc.Object",
    .tp_doc = "ZGC Managed Object",
    .tp_basicsize = sizeof(ZObject),
    .tp_itemsize = 0,
    .tp_weaklistoffset = offsetof(ZObject, weakreflist),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,
    .tp_alloc = PyType_GenericAlloc,
    .tp_new = ZObject_new,
    .tp_dealloc = (destructor)ZObject_dealloc,
    .tp_traverse = (traverseproc)ZObject_traverse,
    .tp_clear = (inquiry)ZObject_clear,
    .tp_repr = (reprfunc)ZObject_repr,
    .tp_as_sequence = &ZObject_as_sequence,
    .tp_methods = ZObject_methods,
};

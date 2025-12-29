#define PY_SSIZE_T_CLEAN
#include "zgc.h"
#include "zbarrier.h"
#include "zheap.h"
#include "zmarkstack.h"
#include "zobject.h"
#include <Python.h>
#include <frameobject.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

extern bool zbarrier_is_signal_mode(void);

static pthread_t gc_thread;
static atomic_bool gc_running = false;
static ZMarkStack mark_stack;

// Testing helpers
void zgc_add_root(void *obj) {
  // Always allow adding roots, even if GC not running (for manual cycle)
  static bool mark_stack_initialized = false;
  if (!mark_stack_initialized) {
    zmarkstack_init(&mark_stack);
    mark_stack_initialized = true;
  }
  ZObject *zobj = (ZObject *)obj;
  if (zobj && zobj->body) {
    zmarkstack_push(&mark_stack, zobj->body);
  }
}

bool zgc_check_marked(void *obj) {
  ZObject *zobj = (ZObject *)obj;
  if (!zobj || !zobj->body)
    return false;

  ZPage *page = zheap_get_page(zobj->body);
  if (!page)
    return false;
  return zpage_is_marked(page, zobj->body);
}

// Helpers from zobject.c
extern ZObject *zobject_get_list_head(void);
extern void zobject_lock_list(void);
extern void zobject_unlock_list(void);

// Root Scanning
static void zgc_scan_roots(void) {
  // Iterate global list of ZObjects
  // We need GIL here to prevent race with ZObject_new
  // (which allocates body, then adds to list).
  // If we scan while ZObject_new is between alloc and add, we miss it.
  // But ZObject_new holds GIL. So if we hold GIL, we are safe.

  PyGILState_STATE gstate = PyGILState_Ensure();
  // fprintf(stderr, "[ZGC] Scanning Roots...\n");

  zobject_lock_list();
  ZObject *curr = zobject_get_list_head();
  int count = 0;
  while (curr) {
    if (curr->body) {
      zmarkstack_push(&mark_stack, curr->body);
    }
    curr = curr->next;
    count++;
  }
  zobject_unlock_list();
  fprintf(stderr, "[ZGC] Scanned %d roots.\n", count);

  PyGILState_Release(gstate);
}

static void zgc_mark(void) {
  // Scan Roots (Stack)
  fprintf(stderr, "[ZGC] Mark Stack Start Head: %p\n", mark_stack.head);
  zgc_scan_roots();

  while (!zmarkstack_is_empty(&mark_stack)) {
    ZBody *body = (ZBody *)zmarkstack_pop(&mark_stack);
    if (!body)
      continue;

    ZPage *page = zheap_get_page(body);
    if (!page) {
      continue;
    }

    if (zpage_is_marked(page, body)) {
      // printf("[DEBUG] zgc_mark: Already marked %p\n", body);
      continue;
    }

    zpage_mark_object(page, body);

    for (int i = 0; i < ZOBJECT_SLOTS; i++) {
      PyObject *child = body->slots[i];
      if (child && Py_TYPE(child) == &ZObjectType) {
        ZObject *zchild = (ZObject *)child;
        if (zchild->body) {
          // Fix pointer ONLY if it points to a relocated object (Forwarding)
          // Do NOT fix color if it's just a color mismatch, because the object
          // might move later in this cycle.
          if (!Z_HAS_COLOR(zchild->body, zgc_good_color)) {
            void *raw_body = Z_ADDRESS(zchild->body);
            ZPage *page = zheap_get_page(raw_body);
            if (page && page->is_evacuating) {
              void *new_body = zpage_resolve_forwarding(page, raw_body);
              if (new_body) {
                zchild->body = (ZBody *)Z_WITH_COLOR(new_body, zgc_good_color);
              }
            }
          }
          zmarkstack_push(&mark_stack, zchild->body);
          // printf("[ZGC] Pushed child %p\n", zchild->body);
        }
      }
    }
  }
}

static bool zgc_evacuate_page(ZPage *page, void *arg) {
  bool minor_gc = (bool)(uintptr_t)arg;

  // Skip pages allocated in the current cycle (Lazy TLAB Retirement)
  uint64_t current_cycle = zheap_get_cycle_count();
  if (page->allocation_cycle == current_cycle) {
    fprintf(stderr, "[ZGC] Skipping new page (Cycle %llu)\n",
            page->allocation_cycle);
    return false;
  } else {
    fprintf(stderr, "[ZGC] Evacuating page (Cycle %llu < %llu)\n",
            page->allocation_cycle, current_cycle);
  }

  // Minor GC: Only evacuate Young pages
  if (minor_gc && page->generation != ZGEN_YOUNG) {
    return false;
  }

  // Start evacuation
  zpage_start_evacuation(page);

  // Scan the bitmap to find live objects
  uintptr_t page_start = page->start;
  // Skip metadata
  uintptr_t obj_start = (page_start + sizeof(ZPage) + 7) & ~7;

  for (uintptr_t addr = obj_start; addr < page->top;
       addr += 8) { // Assuming 8-byte alignment
    // Check if marked
    void *obj = (void *)addr;
    if (zpage_is_marked(page, obj)) {
      // It's live! Move it.
      // 1. Allocate new space
      // If Minor GC, promote to Old Gen.
      // If Full GC, keep in same gen? Or promote?
      // For simplicity, always promote to Old Gen during relocation for now.
      size_t obj_size = sizeof(ZBody);
      void *new_addr = zheap_alloc(obj_size, ZGEN_OLD);

      if (!new_addr) {
        break;
      }

      // 2. Copy content
      memcpy(new_addr, obj, obj_size);

      // 3. Add forwarding entry
      zpage_add_forwarding(page, obj, new_addr);
    }
  }

  return true; // Page evacuated
}

static void zgc_relocate(bool minor_gc) {
  zheap_relocate_pages(zgc_evacuate_page, (void *)(uintptr_t)minor_gc);
}

void zgc_run_cycle(void) {
  fprintf(stderr, "[ZGC] Full Cycle Start.\n");
  // Full GC Cycle

  // Synchronize with Mutators (Hold GIL)
  PyGILState_STATE gstate = PyGILState_Ensure();

  // Increment Cycle Count (New pages will get this count)
  zheap_inc_cycle_count();

  // 0. Flip Good Color
  if (zgc_good_color == ZPOINTER_MARKED0_BIT) {
    zgc_good_color = ZPOINTER_MARKED1_BIT;
  } else {
    zgc_good_color = ZPOINTER_MARKED0_BIT;
  }

  PyGILState_Release(gstate);

  // printf("[ZGC] Full Cycle Start. Good Color: %s\n",
  //        (zgc_good_color == ZPOINTER_MARKED0_BIT) ? "Marked0" :
  //        "Marked1");

  // 0.5 Clear Bitmaps (from previous cycle)
  ZPage *p = zheap_get_head_page();
  while (p) {
    zpage_clear_bitmap(p);
    p = p->next;
  }

  // 1. Mark Phase
  // 1. Mark Phase
  // Clear Remembered Set (Full GC scans all roots, so we don't need
  // RemSet) Also, RemSet might contain stale pointers if we don't clear
  // it.
  while (!zremset_is_empty()) {
    zremset_pop();
  }

  zgc_mark();

  // 1.5 Free Reclaim List (from previous cycle)
  // Safe because zgc_mark has remapped all live pointers
  zheap_free_reclaim_list();

  // 2. Relocate Phase (Full)
  zgc_relocate(false);

  // 3. Remap Roots (Eagerly fix ZObject pointers)
  // This ensures tp_traverse sees valid pointers even after page
  // reclamation. fprintf(stderr, "[ZGC] Remapping Roots...\n");
  zobject_lock_list();
  ZObject *curr = zobject_get_list_head();
  int count = 0;
  while (curr) {
    if (curr->body && !Z_HAS_COLOR(curr->body, zgc_good_color)) {
      zbarrier_fix_pointer(curr);
    }
    curr = curr->next;
    count++;
  }
  zobject_unlock_list();
}

void zgc_minor_cycle(void) {
  // Minor GC Cycle

  // Synchronize with Mutators (Hold GIL)
  PyGILState_STATE gstate = PyGILState_Ensure();

  // Increment Cycle Count
  zheap_inc_cycle_count();

  // 0. Flip Good Color
  if (zgc_good_color == ZPOINTER_MARKED0_BIT) {
    zgc_good_color = ZPOINTER_MARKED1_BIT;
  } else {
    zgc_good_color = ZPOINTER_MARKED0_BIT;
  }

  PyGILState_Release(gstate);
  // printf("[ZGC] Minor Cycle Start. Good Color: %s\n",
  //        (zgc_good_color == ZPOINTER_MARKED0_BIT) ? "Marked0" :
  //        "Marked1");

  // 0.5 Clear Bitmaps (Only Young pages)
  // We only collect Young Gen, so we only need to clear Young page
  // bitmaps. Old pages retain their mark state (which might be stale,
  // but they are not collected).
  ZPage *p = zheap_get_head_page();
  while (p) {
    if (p->generation == ZGEN_YOUNG) {
      zpage_clear_bitmap(p);
    }
    p = p->next;
  }

  // 1. Mark Phase
  // Add Remembered Set to Mark Stack
  while (!zremset_is_empty()) {
    void *obj = zremset_pop();
    if (obj) {
      zmarkstack_push(&mark_stack, obj);
    }
  }

  // Mark
  zgc_mark();

  // Free Reclaim List
  zheap_free_reclaim_list();

  // 2. Relocate Phase (Minor only)
  zgc_relocate(true);

  // 3. Remap Roots
  zobject_lock_list();
  ZObject *curr = zobject_get_list_head();
  while (curr) {
    if (curr->body && !Z_HAS_COLOR(curr->body, zgc_good_color)) {
      zbarrier_fix_pointer(curr);
    }
    curr = curr->next;
  }
  zobject_unlock_list();
}

static void *zgc_thread_func(void *arg) {
  zmarkstack_init(&mark_stack);

  printf("[ZGC] Background Thread Started\n");
  while (atomic_load(&gc_running)) {
    zgc_run_cycle();

    // Sleep
    for (int i = 0; i < 1; i++) {
      if (!atomic_load(&gc_running))
        break;
      usleep(100000); // 100ms sleep
    }
  }
  printf("[ZGC] Background Thread Stopped\n");
  return NULL;
}

void zgc_start_thread(void) {
  if (atomic_load(&gc_running))
    return;
  atomic_store(&gc_running, true);
  pthread_create(&gc_thread, NULL, zgc_thread_func, NULL);
}

void zgc_stop_thread(void) {
  if (!atomic_load(&gc_running))
    return;
  atomic_store(&gc_running, false);
  pthread_join(gc_thread, NULL);
}

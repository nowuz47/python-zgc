#define PY_SSIZE_T_CLEAN
#include "zgc.h"
#include "zbarrier.h"
#include "zheap.h"
#include "zmarkstack.h"
#include "zobject.h"
#include <Python.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static pthread_t gc_thread;
static atomic_bool gc_running = false;
static ZMarkStack mark_stack;

// Testing helpers
void zgc_add_root(void *obj) {
  // Always allow adding roots, even if GC not running (for manual cycle)
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

static void zgc_mark(void) {
  while (!zmarkstack_is_empty(&mark_stack)) {
    ZBody *body = (ZBody *)zmarkstack_pop(&mark_stack);
    if (!body)
      continue;

    ZPage *page = zheap_get_page(body);
    if (!page)
      continue;

    if (zpage_is_marked(page, body)) {
      continue;
    }

    zpage_mark_object(page, body);
    // printf("[ZGC] Marked %p (Gen: %d)\n", body, page->generation);

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

static void zgc_relocate(bool minor_gc) {
  // Iterate all pages
  ZPage *page = zheap_get_head_page();
  ZPage *current_alloc_page = zheap_get_current_page();

  while (page) {
    // Skip the current allocation page
    if (page == current_alloc_page) {
      page = page->next;
      continue;
    }

    // Minor GC: Only evacuate Young pages
    if (minor_gc && page->generation != ZGEN_YOUNG) {
      page = page->next;
      continue;
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
        // (In real ZGC, we might keep in Young if it's the first survival)
        size_t obj_size = sizeof(ZBody);
        void *new_addr = zheap_alloc(obj_size, ZGEN_OLD);

        if (!new_addr) {
          break;
        }

        // 2. Copy content
        memcpy(new_addr, obj, obj_size);

        // 3. Add forwarding entry
        zpage_add_forwarding(page, obj, new_addr);
        // printf("[ZGC] Relocated %p -> %p\n", obj, new_addr);
      }
    }

    page = page->next;
  }
}

void zgc_run_cycle(void) {
  // Full GC Cycle

  // 0. Flip Good Color
  if (zgc_good_color == ZPOINTER_MARKED0_BIT) {
    zgc_good_color = ZPOINTER_MARKED1_BIT;
  } else {
    zgc_good_color = ZPOINTER_MARKED0_BIT;
  }
  // printf("[ZGC] Full Cycle Start. Good Color: %s\n",
  //        (zgc_good_color == ZPOINTER_MARKED0_BIT) ? "Marked0" : "Marked1");

  // 0.5 Clear Bitmaps (from previous cycle)
  ZPage *p = zheap_get_head_page();
  while (p) {
    zpage_clear_bitmap(p);
    p = p->next;
  }

  // 1. Mark Phase
  zgc_mark();

  // 2. Relocate Phase (Full)
  zgc_relocate(false);
}

void zgc_minor_cycle(void) {
  // Minor GC Cycle

  // 0. Flip Good Color
  if (zgc_good_color == ZPOINTER_MARKED0_BIT) {
    zgc_good_color = ZPOINTER_MARKED1_BIT;
  } else {
    zgc_good_color = ZPOINTER_MARKED0_BIT;
  }
  // printf("[ZGC] Minor Cycle Start. Good Color: %s\n",
  //        (zgc_good_color == ZPOINTER_MARKED0_BIT) ? "Marked0" : "Marked1");

  // 0.5 Clear Bitmaps (Only Young pages need clearing? Or all?)
  // We need to mark Young objects.
  // If we clear all bitmaps, we lose Old object marks.
  // But Old objects are not candidates for collection in Minor GC.
  // So we only care about marking Young objects.
  // But tracing might go through Old objects.
  // If Old objects are not marked, we might re-mark them?
  // For simplicity, let's clear all bitmaps.
  ZPage *p = zheap_get_head_page();
  while (p) {
    zpage_clear_bitmap(p);
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

  // Mark (Roots are already added by zgc_add_root or similar mechanism?
  // In this prototype, roots are added manually via zgc_add_root.
  // So we assume roots are in mark stack.)
  zgc_mark();

  // 2. Relocate Phase (Minor only)
  zgc_relocate(true);
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

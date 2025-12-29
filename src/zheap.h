#ifndef ZHEAP_H
#define ZHEAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Page size: 2MB (Small Page)
#define ZPAGE_SIZE (2 * 1024 * 1024)
// Bitmap size: 2MB / 8 bytes (min object size) / 8 bits per byte = 32KB
#define ZBITMAP_SIZE (ZPAGE_SIZE / 8 / 8)

// TLAB Size: 32KB
#define ZTLAB_SIZE (32 * 1024)

// Generations
#define ZGEN_YOUNG 0
#define ZGEN_OLD 1

// --- Colored Pointers ---
#define ZPOINTER_MARK_SHIFT 60
#define ZPOINTER_MARKED0_BIT (1ULL << 60)
#define ZPOINTER_MARKED1_BIT (1ULL << 61)
#define ZPOINTER_REMAPPED_BIT (1ULL << 62)
#define ZPOINTER_FINALIZABLE_BIT (1ULL << 63)

#define ZPOINTER_COLOR_MASK                                                    \
  (ZPOINTER_MARKED0_BIT | ZPOINTER_MARKED1_BIT | ZPOINTER_REMAPPED_BIT |       \
   ZPOINTER_FINALIZABLE_BIT)
#define ZPOINTER_ADDRESS_MASK (~ZPOINTER_COLOR_MASK)

// Helper macros
#define Z_ADDRESS(ptr) ((void *)((uintptr_t)(ptr) & ZPOINTER_ADDRESS_MASK))
#define Z_COLOR(ptr) ((uintptr_t)(ptr) & ZPOINTER_COLOR_MASK)
#define Z_HAS_COLOR(ptr, color) (Z_COLOR(ptr) == (color))
#define Z_WITH_COLOR(ptr, color) ((void *)((uintptr_t)Z_ADDRESS(ptr) | (color)))

// Global state for the "Good Color"
extern uintptr_t zgc_good_color;

// Forwarding Table Entry
typedef struct {
  uintptr_t from_offset; // Offset in page
  uintptr_t to_addr;     // New address
} ZForwardingEntry;

// Simple Forwarding Table
typedef struct {
  ZForwardingEntry *entries;
  size_t count;
  size_t capacity;
} ZForwardingTable;

typedef struct ZPage {
  struct ZPage *next;
  uintptr_t start;
  uintptr_t top;
  uintptr_t end;

  // Mark Bitmap: 1 bit per 8 bytes of memory
  uint8_t mark_bitmap[ZBITMAP_SIZE];

  // Live bytes count (for evacuation heuristics)
  size_t live_bytes;

  // Evacuation flag
  bool is_evacuating;

  // Generation (0=Young, 1=Old)
  uint8_t generation;

  // Forwarding Table (only valid if is_evacuating is true)
  ZForwardingTable forwarding_table;

  // NUMA Node ID (for NUMA-aware allocation)
  int numa_node;

  // Raw memory pointer for unmap (includes alignment padding)
  void *raw_mem;

  // Allocation Cycle (Epoch) to prevent evacuating new pages
  uint64_t allocation_cycle;
} ZPage;

// Thread-Local Allocation Buffer
typedef struct {
  uintptr_t top;
  uintptr_t end;
  uintptr_t expected_color; // To detect color change
  uint64_t cycle_count;     // To detect cycle change (even if color matches)
} ZTLAB;

// Exposed TLAB for inline allocation
extern __thread ZTLAB zheap_tlab;

// Allocator
void *zheap_alloc(size_t size, uint8_t generation);

// Inline Fast-Path Allocator
static inline void *zheap_alloc_inline(size_t size) {
  // Check for Cycle Change (Lazy TLAB Retirement)
  // We check both color and cycle count.
  // Color check is fast (bitwise). Cycle count check handles the case where
  // color flips back. But checking global variable zheap_cycle_count might be
  // slow (cache line bouncing)? For now, let's just check color here. If color
  // matches, we assume it's fine? NO! If color flips back (even number of
  // cycles), we might use a reclaimed page! So we MUST check cycle count. But
  // zheap_cycle_count is not exposed in header? It is exposed via
  // zheap_get_cycle_count(), but that's a function call. We should expose the
  // variable or just rely on zheap_alloc (slow path) to check it? If we don't
  // check it here, we might allocate from bad TLAB. So we MUST check it here.
  // Let's expose zheap_cycle_count in zheap.h?
  // Or just make zheap_alloc_inline call zheap_alloc if we want to be safe for
  // now. Let's assume zheap_alloc handles it. But zheap_alloc_inline tries to
  // allocate from TLAB directly. So we need to access zheap_cycle_count.

  // For this fix, let's disable inline allocation if we can't check cycle count
  // efficiently. Or just expose zheap_cycle_count.

  // Actually, let's just check expected_color here.
  // If we want to fix the bug, we need to ensure expected_color ALWAYS changes?
  // No, it flips.

  // Let's modify zheap.c to expose zheap_cycle_count.
  extern uint64_t zheap_cycle_count;
  if (zheap_tlab.expected_color != zgc_good_color ||
      zheap_tlab.cycle_count != zheap_cycle_count) {
    // Fallback to slow path to retire TLAB and update color/cycle
    return zheap_alloc(size, ZGEN_YOUNG);
  }

  // Align size
  size = (size + 7) & ~7;

  // Check TLAB
  if (zheap_tlab.top + size <= zheap_tlab.end) {
    void *ptr = (void *)zheap_tlab.top;
    zheap_tlab.top += size;
    return Z_WITH_COLOR(ptr, zgc_good_color);
  }

  // Slow path
  return zheap_alloc(size, ZGEN_YOUNG);
}

// Remembered Set (List of Old objects pointing to Young objects)
typedef struct {
  void **items;
  size_t count;
  size_t capacity;
} ZRememberedSet;

void zheap_init(void);
void zheap_free(void *ptr);

// GC Helpers
ZPage *zheap_get_current_page(void); // For checking if we should evacuate
ZPage *zheap_get_head_page(void);    // To iterate all pages

// Marking helpers
ZPage *zheap_get_page(void *obj);
void zpage_mark_object(ZPage *page, void *obj);
bool zpage_is_marked(ZPage *page, void *obj);
void zpage_clear_bitmap(ZPage *page);

// Relocation helpers
void zpage_start_evacuation(ZPage *page);
void zpage_add_forwarding(ZPage *page, void *from, void *to);
void *zpage_resolve_forwarding(ZPage *page, void *from);

// Generation Helpers
bool zheap_is_old(void *obj);
bool zheap_is_young(void *obj);
void zremset_add(void *obj);
void *zremset_pop(void); // For processing
bool zremset_is_empty(void);

// Page Reclamation
void zheap_free_reclaim_list(void);
// Iterate pages, evacuate using callback, and move evacuated pages to reclaim
// list
void zheap_relocate_pages(bool (*evacuate_func)(ZPage *page, void *arg),
                          void *arg);

// Cycle Management
// Cycle Management
void zheap_inc_cycle_count(void);
uint64_t zheap_get_cycle_count(void);
extern uint64_t zheap_cycle_count;

#endif

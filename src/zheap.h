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
} ZPage;

// Thread-Local Allocation Buffer
typedef struct {
  uintptr_t top;
  uintptr_t end;
} ZTLAB;

// Exposed TLAB for inline allocation
extern __thread ZTLAB zheap_tlab;

// Allocator
void *zheap_alloc(size_t size, uint8_t generation);

// Inline Fast-Path Allocator
static inline void *zheap_alloc_inline(size_t size) {
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

#endif

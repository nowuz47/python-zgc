#include "zheap.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static ZPage *current_young_page = NULL;
static ZPage *current_old_page = NULL;
static ZPage *head_page = NULL;
static pthread_mutex_t heap_lock = PTHREAD_MUTEX_INITIALIZER;

// Global Good Color (starts as Marked0)
uintptr_t zgc_good_color = ZPOINTER_MARKED0_BIT;

// NUMA Helper (Stub)
// In a real implementation, this would use getcpu() or libnuma.
int zos_get_current_numa_node(void) {
  return 0; // Default to node 0
}

// Thread-Local Allocation Buffer (Only for Young Gen)
__thread ZTLAB zheap_tlab = {0, 0};

// Remembered Set
static ZRememberedSet remset = {NULL, 0, 0};
static pthread_mutex_t remset_lock = PTHREAD_MUTEX_INITIALIZER;

static ZPage *zpage_create(uint8_t generation) {
// Try Huge Pages first (Linux specific, usually 2MB)
#ifdef MAP_HUGETLB
  void *raw_mem = mmap(NULL, 2 * ZPAGE_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
  if (raw_mem == MAP_FAILED) {
    // Fallback to standard pages
    raw_mem = mmap(NULL, 2 * ZPAGE_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  }
#else
  void *raw_mem = mmap(NULL, 2 * ZPAGE_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif

  if (raw_mem == MAP_FAILED) {
    perror("mmap failed");
    return NULL;
  }

  uintptr_t aligned_addr =
      ((uintptr_t)raw_mem + ZPAGE_SIZE - 1) & ~(ZPAGE_SIZE - 1);
  void *mem = (void *)aligned_addr;

  // Embed ZPage metadata at the start of the page
  ZPage *page = (ZPage *)mem;

  page->start = (uintptr_t)mem;
  uintptr_t meta_end = page->start + sizeof(ZPage);
  page->top = (meta_end + 7) & ~7;
  page->end = page->start + ZPAGE_SIZE;
  page->next = NULL;
  page->live_bytes = 0;
  memset(page->mark_bitmap, 0, ZBITMAP_SIZE);

  page->is_evacuating = false;
  page->generation = generation;
  page->forwarding_table.entries = NULL;
  page->forwarding_table.count = 0;
  page->forwarding_table.capacity = 0;

  page->numa_node = zos_get_current_numa_node();

  return page;
}

void zheap_init(void) {
  pthread_mutex_lock(&heap_lock);
  if (!current_young_page) {
    current_young_page = zpage_create(ZGEN_YOUNG);
    head_page = current_young_page;
  }
  pthread_mutex_unlock(&heap_lock);
}

// Refill TLAB from global heap (Young Gen)
static bool zheap_refill_tlab(size_t size) {
  pthread_mutex_lock(&heap_lock);

  if (!current_young_page) {
    current_young_page = zpage_create(ZGEN_YOUNG);
    head_page = current_young_page;
    if (!current_young_page) {
      pthread_mutex_unlock(&heap_lock);
      return false;
    }
  }

  size_t alloc_size = (size > ZTLAB_SIZE) ? size : ZTLAB_SIZE;
  alloc_size = (alloc_size + 7) & ~7;

  if (current_young_page->top + alloc_size > current_young_page->end) {
    ZPage *new_page = zpage_create(ZGEN_YOUNG);
    if (!new_page) {
      pthread_mutex_unlock(&heap_lock);
      return false;
    }
    // Append to list
    current_young_page->next = new_page;
    current_young_page = new_page;
  }

  zheap_tlab.top = current_young_page->top;
  zheap_tlab.end = current_young_page->top + alloc_size;

  current_young_page->top += alloc_size;

  pthread_mutex_unlock(&heap_lock);
  return true;
}

void *zheap_alloc(size_t size, uint8_t generation) {
  // Align size to 8 bytes
  size = (size + 7) & ~7;

  if (generation == ZGEN_YOUNG) {
    // Try TLAB first
    if (zheap_tlab.top + size <= zheap_tlab.end) {
      void *ptr = (void *)zheap_tlab.top;
      zheap_tlab.top += size;
      return Z_WITH_COLOR(ptr, zgc_good_color);
    }

    if (zheap_refill_tlab(size)) {
      if (zheap_tlab.top + size <= zheap_tlab.end) {
        void *ptr = (void *)zheap_tlab.top;
        zheap_tlab.top += size;
        return Z_WITH_COLOR(ptr, zgc_good_color);
      }
    }
    return NULL;
  } else {
    // Old Generation Allocation (Directly in Old Page)
    pthread_mutex_lock(&heap_lock);

    if (!current_old_page) {
      current_old_page = zpage_create(ZGEN_OLD);
      // Link into global list
      if (head_page) {
        // We need to insert it somewhere.
        // For simplicity, let's just append to head_page?
        // No, we need to traverse to find end?
        // Or just prepend to head_page?
        current_old_page->next = head_page;
        head_page = current_old_page;
      } else {
        head_page = current_old_page;
      }
    }

    if (current_old_page->top + size > current_old_page->end) {
      ZPage *new_page = zpage_create(ZGEN_OLD);
      if (!new_page) {
        pthread_mutex_unlock(&heap_lock);
        return NULL;
      }
      new_page->next = head_page;
      head_page = new_page;
      current_old_page = new_page;
    }

    void *ptr = (void *)current_old_page->top;
    current_old_page->top += size;

    pthread_mutex_unlock(&heap_lock);
    return Z_WITH_COLOR(ptr, zgc_good_color);
  }
}

void zheap_free(void *ptr) {
  // No-op
}

ZPage *zheap_get_current_page(void) { return current_young_page; }

ZPage *zheap_get_head_page(void) { return head_page; }

// Marking Helpers

ZPage *zheap_get_page(void *obj) {
  uintptr_t addr = (uintptr_t)Z_ADDRESS(obj);
  uintptr_t page_start = addr & ~(ZPAGE_SIZE - 1);
  return (ZPage *)page_start;
}

void zpage_mark_object(ZPage *page, void *obj) {
  uintptr_t offset = (uintptr_t)Z_ADDRESS(obj) - page->start;
  size_t bit_index = offset / 8;
  size_t byte_index = bit_index / 8;
  size_t bit_offset = bit_index % 8;

  if (byte_index < ZBITMAP_SIZE) {
    page->mark_bitmap[byte_index] |= (1 << bit_offset);
  }
}

bool zpage_is_marked(ZPage *page, void *obj) {
  uintptr_t offset = (uintptr_t)Z_ADDRESS(obj) - page->start;
  size_t bit_index = offset / 8;
  size_t byte_index = bit_index / 8;
  size_t bit_offset = bit_index % 8;

  if (byte_index < ZBITMAP_SIZE) {
    return (page->mark_bitmap[byte_index] & (1 << bit_offset)) != 0;
  }
  return false;
}

void zpage_clear_bitmap(ZPage *page) {
  memset(page->mark_bitmap, 0, ZBITMAP_SIZE);
  page->live_bytes = 0;
}

// Relocation Helpers

void zpage_start_evacuation(ZPage *page) {
  page->is_evacuating = true;
  if (page->forwarding_table.entries) {
    free(page->forwarding_table.entries);
  }
  page->forwarding_table.capacity = 128; // Initial capacity
  page->forwarding_table.entries = (ZForwardingEntry *)malloc(
      sizeof(ZForwardingEntry) * page->forwarding_table.capacity);
  page->forwarding_table.count = 0;
}

void zpage_add_forwarding(ZPage *page, void *from, void *to) {
  if (!page->is_evacuating)
    return;

  if (page->forwarding_table.count >= page->forwarding_table.capacity) {
    page->forwarding_table.capacity *= 2;
    page->forwarding_table.entries = (ZForwardingEntry *)realloc(
        page->forwarding_table.entries,
        sizeof(ZForwardingEntry) * page->forwarding_table.capacity);
  }

  // Store raw offsets
  uintptr_t offset = (uintptr_t)Z_ADDRESS(from) - page->start;
  page->forwarding_table.entries[page->forwarding_table.count].from_offset =
      offset;
  page->forwarding_table.entries[page->forwarding_table.count].to_addr =
      (uintptr_t)Z_ADDRESS(to);
  page->forwarding_table.count++;
}

void *zpage_resolve_forwarding(ZPage *page, void *from) {
  if (!page->is_evacuating)
    return NULL;

  uintptr_t offset = (uintptr_t)Z_ADDRESS(from) - page->start;

  // Linear search for prototype
  for (size_t i = 0; i < page->forwarding_table.count; i++) {
    if (page->forwarding_table.entries[i].from_offset == offset) {
      return (void *)page->forwarding_table.entries[i].to_addr;
    }
  }
  return NULL;
}

// Generation Helpers

bool zheap_is_old(void *obj) {
  ZPage *page = zheap_get_page(obj);
  if (!page)
    return false;
  return page->generation == ZGEN_OLD;
}

bool zheap_is_young(void *obj) {
  ZPage *page = zheap_get_page(obj);
  if (!page)
    return false;
  return page->generation == ZGEN_YOUNG;
}

void zremset_add(void *obj) {
  pthread_mutex_lock(&remset_lock);
  if (remset.count >= remset.capacity) {
    remset.capacity = (remset.capacity == 0) ? 128 : remset.capacity * 2;
    remset.items =
        (void **)realloc(remset.items, sizeof(void *) * remset.capacity);
  }
  // Check if already exists? (Linear scan is slow, but fine for prototype)
  // For now, just add duplicates (GC will handle it)
  remset.items[remset.count++] = obj;
  pthread_mutex_unlock(&remset_lock);
}

void *zremset_pop(void) {
  pthread_mutex_lock(&remset_lock);
  if (remset.count > 0) {
    return remset.items[--remset.count];
  }
  pthread_mutex_unlock(&remset_lock);
  return NULL;
}

bool zremset_is_empty(void) { return remset.count == 0; }

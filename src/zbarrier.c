#include "zbarrier.h"
#include "zheap.h"
#include "zobject.h"
#include <stdio.h>

// Core logic to resolve a pointer
void *zbarrier_resolve_pointer(void *ptr) {
  if (!ptr)
    return NULL;

  // 1. Strip color to get raw address
  void *raw_body = Z_ADDRESS(ptr);

  // 2. Check if page is evacuated
  ZPage *page = zheap_get_page(raw_body);
  if (page && page->is_evacuating) {
    // Resolve forwarding
    void *new_body = zpage_resolve_forwarding(page, raw_body);
    if (new_body) {
      // Found new address!
      return Z_WITH_COLOR(new_body, zgc_good_color);
    }
  }

  // 3. If not evacuated (or not found), just update color
  return Z_WITH_COLOR(raw_body, zgc_good_color);
}

void zbarrier_fix_pointer(ZObject *zobj) {
  if (!zobj || !zobj->body)
    return;

  zobj->body = (ZBody *)zbarrier_resolve_pointer(zobj->body);
}

// JIT Interface Implementation
void *zbarrier_fix_pointer_jit(void *ptr) {
  return zbarrier_resolve_pointer(ptr);
}

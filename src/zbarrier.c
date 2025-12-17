#include "zbarrier.h"
#include "zheap.h"
#include "zobject.h"
#include <stdio.h>

void zbarrier_fix_pointer(ZObject *zobj) {
  if (!zobj || !zobj->body)
    return;

  // 1. Strip color to get raw address
  void *raw_body = Z_ADDRESS(zobj->body);

  // 2. Check if page is evacuated
  ZPage *page = zheap_get_page(raw_body);
  if (page && page->is_evacuating) {
    // Resolve forwarding
    void *new_body = zpage_resolve_forwarding(page, raw_body);
    if (new_body) {
      // Found new address!
      // Update with new address AND good color
      zobj->body = (ZBody *)Z_WITH_COLOR(new_body, zgc_good_color);
      // printf("[ZBarrier] Fixed %p -> %p\n", raw_body, zobj->body);
      return;
    } else {
      // printf("[ZBarrier] Failed to resolve %p (Page Count: %zu)\n", raw_body,
      //        page->forwarding_table.count);
    }
  } else {
    if (page) {
      // printf("[ZBarrier] Page %p not evacuating (Gen: %d)\n", page,
      //        page->generation);
    } else {
      // printf("[ZBarrier] Page not found for %p\n", raw_body);
    }
  }

  // 3. If not evacuated (or not found in forwarding), just update color
  // This happens if the object is in a page that wasn't evacuated,
  // but the global color changed (e.g., next marking phase).
  // We just "heal" the color.
  zobj->body = (ZBody *)Z_WITH_COLOR(raw_body, zgc_good_color);
}

PyObject *zbarrier_load(PyObject *obj) {
  if (obj == NULL)
    return NULL;

  // Check if it's a ZObject
  if (Py_TYPE(obj) == &ZObjectType) {
    ZObject *zobj = (ZObject *)obj;

    // Check color
    if (!Z_HAS_COLOR(zobj->body, zgc_good_color)) {
      zbarrier_fix_pointer(zobj);
    }
  }

  return obj;
}

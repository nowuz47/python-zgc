#ifndef ZGC_H
#define ZGC_H

#include <stdbool.h>

void zgc_start_thread(void);
void zgc_stop_thread(void);
void zgc_add_root(void *obj);
bool zgc_check_marked(void *obj);
void zgc_run_cycle(void);   // Manual Full GC cycle
void zgc_minor_cycle(void); // Manual Minor GC cycle

#endif

#ifndef ZMARKSTACK_H
#define ZMARKSTACK_H

#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>

#define ZMARKSTACK_CHUNK_SIZE 1024

typedef struct ZMarkStackChunk {
  struct ZMarkStackChunk *next;
  void *objects[ZMARKSTACK_CHUNK_SIZE];
  size_t top;
} ZMarkStackChunk;

typedef struct {
  ZMarkStackChunk *head;
  pthread_mutex_t lock;
} ZMarkStack;

void zmarkstack_init(ZMarkStack *stack);
void zmarkstack_push(ZMarkStack *stack, void *obj);
void *zmarkstack_pop(ZMarkStack *stack);
int zmarkstack_is_empty(ZMarkStack *stack);

#endif

#include "zmarkstack.h"

void zmarkstack_init(ZMarkStack *stack) {
  stack->head = NULL;
  pthread_mutex_init(&stack->lock, NULL);
}

void zmarkstack_push(ZMarkStack *stack, void *obj) {
  pthread_mutex_lock(&stack->lock);

  if (!stack->head || stack->head->top == ZMARKSTACK_CHUNK_SIZE) {
    ZMarkStackChunk *new_chunk =
        (ZMarkStackChunk *)malloc(sizeof(ZMarkStackChunk));
    new_chunk->next = stack->head;
    new_chunk->top = 0;
    stack->head = new_chunk;
  }

  stack->head->objects[stack->head->top++] = obj;

  pthread_mutex_unlock(&stack->lock);
}

void *zmarkstack_pop(ZMarkStack *stack) {
  pthread_mutex_lock(&stack->lock);

  if (!stack->head) {
    pthread_mutex_unlock(&stack->lock);
    return NULL;
  }

  void *obj = stack->head->objects[--stack->head->top];

  if (stack->head->top == 0) {
    ZMarkStackChunk *old_chunk = stack->head;
    stack->head = old_chunk->next;
    free(old_chunk);
  }

  pthread_mutex_unlock(&stack->lock);
  return obj;
}

int zmarkstack_is_empty(ZMarkStack *stack) {
  pthread_mutex_lock(&stack->lock);
  int empty = (stack->head == NULL);
  pthread_mutex_unlock(&stack->lock);
  return empty;
}

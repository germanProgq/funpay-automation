/* FunPay Vertex core scheduler implementation. */

#include "fpv_core/fpv_scheduler.h"

#include <stdlib.h>
#include <string.h>

#include "fpv_platform.h"
#include "fpv_time.h"

typedef struct fpv_task {
  uint64_t id;
  uint64_t run_at_ms;
  fpv_task_fn fn;
  void* user_data;
} fpv_task_t;

typedef struct fpv_task_node {
  fpv_task_t task;
  struct fpv_task_node* next;
} fpv_task_node_t;

typedef struct fpv_task_heap {
  fpv_task_node_t** items;
  size_t count;
  size_t capacity;
} fpv_task_heap_t;

struct fpv_scheduler {
  fpv_mutex_t mutex;
  fpv_cond_t scheduled_cond;
  fpv_cond_t ready_cond;
  fpv_thread_t dispatcher_thread;
  fpv_thread_t* workers;
  size_t worker_count;
  fpv_task_node_t* ready_head;
  fpv_task_node_t* ready_tail;
  fpv_task_heap_t scheduled;
  uint64_t next_task_id;
  bool shutdown;
};

static bool fpv_task_less(
    const fpv_task_node_t* left,
    const fpv_task_node_t* right) {
  if (left->task.run_at_ms != right->task.run_at_ms) {
    return left->task.run_at_ms < right->task.run_at_ms;
  }
  return left->task.id < right->task.id;
}

static bool fpv_task_heap_reserve(fpv_task_heap_t* heap, size_t capacity) {
  fpv_task_node_t** items = NULL;

  if (heap->capacity >= capacity) {
    return true;
  }

  items = (fpv_task_node_t**)realloc(
      heap->items,
      capacity * sizeof(*items));
  if (!items) {
    return false;
  }

  heap->items = items;
  heap->capacity = capacity;
  return true;
}

static bool fpv_task_heap_push(fpv_task_heap_t* heap, fpv_task_node_t* node) {
  size_t index = 0;
  size_t parent = 0;

  if (!fpv_task_heap_reserve(heap, heap->count + 1)) {
    return false;
  }

  index = heap->count++;
  heap->items[index] = node;

  while (index > 0) {
    parent = (index - 1) / 2;
    if (fpv_task_less(heap->items[parent], heap->items[index])) {
      break;
    }
    fpv_task_node_t* tmp = heap->items[parent];
    heap->items[parent] = heap->items[index];
    heap->items[index] = tmp;
    index = parent;
  }

  return true;
}

static fpv_task_node_t* fpv_task_heap_peek(const fpv_task_heap_t* heap) {
  if (!heap || heap->count == 0) {
    return NULL;
  }
  return heap->items[0];
}

static fpv_task_node_t* fpv_task_heap_pop(fpv_task_heap_t* heap) {
  fpv_task_node_t* result = NULL;
  size_t index = 0;

  if (!heap || heap->count == 0) {
    return NULL;
  }

  result = heap->items[0];
  heap->count--;
  if (heap->count > 0) {
    heap->items[0] = heap->items[heap->count];
  }

  while (heap->count > 0) {
    size_t left = index * 2 + 1;
    size_t right = index * 2 + 2;
    size_t smallest = index;

    if (left < heap->count &&
        fpv_task_less(heap->items[left], heap->items[smallest])) {
      smallest = left;
    }
    if (right < heap->count &&
        fpv_task_less(heap->items[right], heap->items[smallest])) {
      smallest = right;
    }
    if (smallest == index) {
      break;
    }
    fpv_task_node_t* tmp = heap->items[index];
    heap->items[index] = heap->items[smallest];
    heap->items[smallest] = tmp;
    index = smallest;
  }

  return result;
}

static void fpv_task_heap_clear(fpv_task_heap_t* heap) {
  if (!heap) {
    return;
  }

  for (size_t i = 0; i < heap->count; i++) {
    free(heap->items[i]);
  }

  free(heap->items);
  heap->items = NULL;
  heap->count = 0;
  heap->capacity = 0;
}

static fpv_task_node_t* fpv_task_node_create(
    uint64_t id,
    uint64_t run_at_ms,
    fpv_task_fn fn,
    void* user_data) {
  fpv_task_node_t* node = (fpv_task_node_t*)calloc(1, sizeof(*node));
  if (!node) {
    return NULL;
  }
  node->task.id = id;
  node->task.run_at_ms = run_at_ms;
  node->task.fn = fn;
  node->task.user_data = user_data;
  return node;
}

static void fpv_scheduler_queue_ready(
    fpv_scheduler_t* scheduler,
    fpv_task_node_t* node) {
  node->next = NULL;
  if (scheduler->ready_tail) {
    scheduler->ready_tail->next = node;
  } else {
    scheduler->ready_head = node;
  }
  scheduler->ready_tail = node;
}

static fpv_task_node_t* fpv_scheduler_pop_ready(fpv_scheduler_t* scheduler) {
  fpv_task_node_t* node = scheduler->ready_head;
  if (!node) {
    return NULL;
  }
  scheduler->ready_head = node->next;
  if (!scheduler->ready_head) {
    scheduler->ready_tail = NULL;
  }
  node->next = NULL;
  return node;
}

static void* fpv_scheduler_worker_main(void* context) {
  fpv_scheduler_t* scheduler = (fpv_scheduler_t*)context;

  while (true) {
    fpv_task_node_t* node = NULL;

    fpv_mutex_lock(&scheduler->mutex);
    while (!scheduler->shutdown && !scheduler->ready_head) {
      fpv_cond_wait(&scheduler->ready_cond, &scheduler->mutex);
    }

    if (scheduler->shutdown && !scheduler->ready_head) {
      fpv_mutex_unlock(&scheduler->mutex);
      break;
    }

    node = fpv_scheduler_pop_ready(scheduler);
    fpv_mutex_unlock(&scheduler->mutex);

    if (node && node->task.fn) {
      node->task.fn(node->task.user_data);
    }
    free(node);
  }

  return NULL;
}

static void* fpv_scheduler_dispatcher_main(void* context) {
  fpv_scheduler_t* scheduler = (fpv_scheduler_t*)context;

  while (true) {
    uint64_t now_ms = 0;
    fpv_task_node_t* node = NULL;

    fpv_mutex_lock(&scheduler->mutex);
    while (!scheduler->shutdown && scheduler->scheduled.count == 0) {
      fpv_cond_wait(&scheduler->scheduled_cond, &scheduler->mutex);
    }

    if (scheduler->shutdown) {
      fpv_mutex_unlock(&scheduler->mutex);
      break;
    }

    now_ms = fpv_time_now_ms();
    node = fpv_task_heap_peek(&scheduler->scheduled);
    if (!node) {
      fpv_mutex_unlock(&scheduler->mutex);
      continue;
    }

    if (node->task.run_at_ms > now_ms) {
      uint64_t wait_ms = node->task.run_at_ms - now_ms;
      fpv_cond_wait_for(
          &scheduler->scheduled_cond,
          &scheduler->mutex,
          wait_ms);
      fpv_mutex_unlock(&scheduler->mutex);
      continue;
    }

    while ((node = fpv_task_heap_peek(&scheduler->scheduled)) != NULL) {
      if (node->task.run_at_ms > now_ms) {
        break;
      }
      node = fpv_task_heap_pop(&scheduler->scheduled);
      if (!node) {
        break;
      }
      fpv_scheduler_queue_ready(scheduler, node);
    }

    fpv_cond_broadcast(&scheduler->ready_cond);
    fpv_mutex_unlock(&scheduler->mutex);
  }

  return NULL;
}

fpv_scheduler_t* fpv_scheduler_create(size_t worker_count) {
  fpv_scheduler_t* scheduler = NULL;

  if (worker_count == 0) {
    worker_count = 1;
  }

  scheduler = (fpv_scheduler_t*)calloc(1, sizeof(*scheduler));
  if (!scheduler) {
    return NULL;
  }

  scheduler->workers =
      (fpv_thread_t*)calloc(worker_count, sizeof(fpv_thread_t));
  if (!scheduler->workers) {
    free(scheduler);
    return NULL;
  }

  if (!fpv_mutex_init(&scheduler->mutex)) {
    free(scheduler->workers);
    free(scheduler);
    return NULL;
  }

  if (!fpv_cond_init(&scheduler->scheduled_cond) ||
      !fpv_cond_init(&scheduler->ready_cond)) {
    fpv_mutex_destroy(&scheduler->mutex);
    free(scheduler->workers);
    free(scheduler);
    return NULL;
  }

  scheduler->worker_count = worker_count;

  if (!fpv_thread_create(
          &scheduler->dispatcher_thread,
          fpv_scheduler_dispatcher_main,
          scheduler)) {
    fpv_cond_destroy(&scheduler->ready_cond);
    fpv_cond_destroy(&scheduler->scheduled_cond);
    fpv_mutex_destroy(&scheduler->mutex);
    free(scheduler->workers);
    free(scheduler);
    return NULL;
  }

  for (size_t i = 0; i < worker_count; i++) {
    if (!fpv_thread_create(
            &scheduler->workers[i],
            fpv_scheduler_worker_main,
            scheduler)) {
      scheduler->shutdown = true;
      fpv_cond_broadcast(&scheduler->scheduled_cond);
      fpv_cond_broadcast(&scheduler->ready_cond);
      fpv_thread_join(&scheduler->dispatcher_thread);
      for (size_t j = 0; j < i; j++) {
        fpv_thread_join(&scheduler->workers[j]);
      }
      fpv_cond_destroy(&scheduler->ready_cond);
      fpv_cond_destroy(&scheduler->scheduled_cond);
      fpv_mutex_destroy(&scheduler->mutex);
      free(scheduler->workers);
      free(scheduler);
      return NULL;
    }
  }

  return scheduler;
}

void fpv_scheduler_destroy(fpv_scheduler_t* scheduler) {
  if (!scheduler) {
    return;
  }

  fpv_mutex_lock(&scheduler->mutex);
  scheduler->shutdown = true;
  fpv_cond_broadcast(&scheduler->scheduled_cond);
  fpv_cond_broadcast(&scheduler->ready_cond);
  fpv_mutex_unlock(&scheduler->mutex);

  fpv_thread_join(&scheduler->dispatcher_thread);
  for (size_t i = 0; i < scheduler->worker_count; i++) {
    fpv_thread_join(&scheduler->workers[i]);
  }

  fpv_task_heap_clear(&scheduler->scheduled);

  while (scheduler->ready_head) {
    fpv_task_node_t* node = scheduler->ready_head;
    scheduler->ready_head = node->next;
    free(node);
  }

  fpv_cond_destroy(&scheduler->ready_cond);
  fpv_cond_destroy(&scheduler->scheduled_cond);
  fpv_mutex_destroy(&scheduler->mutex);

  free(scheduler->workers);
  free(scheduler);
}

fpv_result_t fpv_scheduler_enqueue(
    fpv_scheduler_t* scheduler,
    fpv_task_fn fn,
    void* user_data) {
  fpv_task_node_t* node = NULL;
  uint64_t task_id = 0;

  if (!scheduler || !fn) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  fpv_mutex_lock(&scheduler->mutex);
  task_id = ++scheduler->next_task_id;
  fpv_mutex_unlock(&scheduler->mutex);

  node = fpv_task_node_create(task_id, fpv_time_now_ms(), fn, user_data);
  if (!node) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_mutex_lock(&scheduler->mutex);
  fpv_scheduler_queue_ready(scheduler, node);
  fpv_cond_signal(&scheduler->ready_cond);
  fpv_mutex_unlock(&scheduler->mutex);

  return FPV_OK;
}

fpv_result_t fpv_scheduler_schedule(
    fpv_scheduler_t* scheduler,
    uint64_t run_at_ms,
    fpv_task_fn fn,
    void* user_data) {
  fpv_task_node_t* node = NULL;
  uint64_t now_ms = 0;
  uint64_t task_id = 0;

  if (!scheduler || !fn) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  now_ms = fpv_time_now_ms();
  if (run_at_ms <= now_ms) {
    return fpv_scheduler_enqueue(scheduler, fn, user_data);
  }

  fpv_mutex_lock(&scheduler->mutex);
  task_id = ++scheduler->next_task_id;
  fpv_mutex_unlock(&scheduler->mutex);

  node = fpv_task_node_create(task_id, run_at_ms, fn, user_data);
  if (!node) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_mutex_lock(&scheduler->mutex);
  if (!fpv_task_heap_push(&scheduler->scheduled, node)) {
    fpv_mutex_unlock(&scheduler->mutex);
    free(node);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_cond_signal(&scheduler->scheduled_cond);
  fpv_mutex_unlock(&scheduler->mutex);

  return FPV_OK;
}

fpv_result_t fpv_scheduler_schedule_delay(
    fpv_scheduler_t* scheduler,
    uint64_t delay_ms,
    fpv_task_fn fn,
    void* user_data) {
  uint64_t run_at_ms = 0;

  if (!scheduler || !fn) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  run_at_ms = fpv_time_now_ms() + delay_ms;
  return fpv_scheduler_schedule(scheduler, run_at_ms, fn, user_data);
}

size_t fpv_scheduler_worker_count(const fpv_scheduler_t* scheduler) {
  if (!scheduler) {
    return 0;
  }
  return scheduler->worker_count;
}

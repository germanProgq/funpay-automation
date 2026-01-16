/* FunPay Vertex core platform synchronization. */

#ifndef FPV_PLATFORM_H
#define FPV_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>

typedef struct fpv_mutex {
  CRITICAL_SECTION handle;
  bool initialized;
} fpv_mutex_t;

static inline bool fpv_mutex_init(fpv_mutex_t* mutex) {
  if (!mutex) {
    return false;
  }
  InitializeCriticalSection(&mutex->handle);
  mutex->initialized = true;
  return true;
}

static inline void fpv_mutex_lock(fpv_mutex_t* mutex) {
  EnterCriticalSection(&mutex->handle);
}

static inline void fpv_mutex_unlock(fpv_mutex_t* mutex) {
  LeaveCriticalSection(&mutex->handle);
}

static inline void fpv_mutex_destroy(fpv_mutex_t* mutex) {
  if (mutex && mutex->initialized) {
    DeleteCriticalSection(&mutex->handle);
    mutex->initialized = false;
  }
}

typedef struct fpv_cond {
  CONDITION_VARIABLE handle;
  bool initialized;
} fpv_cond_t;

static inline bool fpv_cond_init(fpv_cond_t* cond) {
  if (!cond) {
    return false;
  }
  InitializeConditionVariable(&cond->handle);
  cond->initialized = true;
  return true;
}

static inline void fpv_cond_wait(fpv_cond_t* cond, fpv_mutex_t* mutex) {
  SleepConditionVariableCS(&cond->handle, &mutex->handle, INFINITE);
}

static inline bool fpv_cond_wait_for(
    fpv_cond_t* cond,
    fpv_mutex_t* mutex,
    uint64_t timeout_ms) {
  BOOL result = SleepConditionVariableCS(
      &cond->handle,
      &mutex->handle,
      (DWORD)timeout_ms);
  if (result) {
    return true;
  }
  return GetLastError() != ERROR_TIMEOUT;
}

static inline void fpv_cond_signal(fpv_cond_t* cond) {
  WakeConditionVariable(&cond->handle);
}

static inline void fpv_cond_broadcast(fpv_cond_t* cond) {
  WakeAllConditionVariable(&cond->handle);
}

static inline void fpv_cond_destroy(fpv_cond_t* cond) {
  if (cond) {
    cond->initialized = false;
  }
}

typedef void* (*fpv_thread_fn)(void* context);

typedef struct fpv_thread {
  HANDLE handle;
  bool started;
} fpv_thread_t;

typedef struct fpv_thread_start {
  fpv_thread_fn fn;
  void* context;
} fpv_thread_start_t;

static DWORD WINAPI fpv_thread_entry(LPVOID param) {
  fpv_thread_start_t* start = (fpv_thread_start_t*)param;
  if (start && start->fn) {
    start->fn(start->context);
  }
  free(start);
  return 0;
}

static inline bool fpv_thread_create(
    fpv_thread_t* thread,
    fpv_thread_fn fn,
    void* context) {
  fpv_thread_start_t* start = NULL;
  if (!thread || !fn) {
    return false;
  }
  start = (fpv_thread_start_t*)malloc(sizeof(*start));
  if (!start) {
    return false;
  }
  start->fn = fn;
  start->context = context;
  thread->handle = CreateThread(
      NULL,
      0,
      fpv_thread_entry,
      start,
      0,
      NULL);
  if (!thread->handle) {
    free(start);
    return false;
  }
  thread->started = true;
  return true;
}

static inline void fpv_thread_join(fpv_thread_t* thread) {
  if (!thread || !thread->started || !thread->handle) {
    return;
  }
  WaitForSingleObject(thread->handle, INFINITE);
  CloseHandle(thread->handle);
  thread->handle = NULL;
  thread->started = false;
}

static inline void fpv_thread_detach(fpv_thread_t* thread) {
  if (!thread || !thread->started || !thread->handle) {
    return;
  }
  CloseHandle(thread->handle);
  thread->handle = NULL;
  thread->started = false;
}
#else
#include <pthread.h>

typedef struct fpv_mutex {
  pthread_mutex_t handle;
  bool initialized;
} fpv_mutex_t;

static inline bool fpv_mutex_init(fpv_mutex_t* mutex) {
  if (!mutex) {
    return false;
  }
  if (pthread_mutex_init(&mutex->handle, NULL) != 0) {
    return false;
  }
  mutex->initialized = true;
  return true;
}

static inline void fpv_mutex_lock(fpv_mutex_t* mutex) {
  pthread_mutex_lock(&mutex->handle);
}

static inline void fpv_mutex_unlock(fpv_mutex_t* mutex) {
  pthread_mutex_unlock(&mutex->handle);
}

static inline void fpv_mutex_destroy(fpv_mutex_t* mutex) {
  if (mutex && mutex->initialized) {
    pthread_mutex_destroy(&mutex->handle);
    mutex->initialized = false;
  }
}

typedef struct fpv_cond {
  pthread_cond_t handle;
  bool initialized;
} fpv_cond_t;

static inline bool fpv_cond_init(fpv_cond_t* cond) {
  if (!cond) {
    return false;
  }
  if (pthread_cond_init(&cond->handle, NULL) != 0) {
    return false;
  }
  cond->initialized = true;
  return true;
}

static inline void fpv_cond_wait(fpv_cond_t* cond, fpv_mutex_t* mutex) {
  pthread_cond_wait(&cond->handle, &mutex->handle);
}

static inline bool fpv_cond_wait_for(
    fpv_cond_t* cond,
    fpv_mutex_t* mutex,
    uint64_t timeout_ms) {
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    pthread_cond_wait(&cond->handle, &mutex->handle);
    return true;
  }
  ts.tv_sec += (time_t)(timeout_ms / 1000ULL);
  ts.tv_nsec += (long)((timeout_ms % 1000ULL) * 1000000ULL);
  if (ts.tv_nsec >= 1000000000L) {
    ts.tv_sec += 1;
    ts.tv_nsec -= 1000000000L;
  }
  return pthread_cond_timedwait(&cond->handle, &mutex->handle, &ts) == 0;
}

static inline void fpv_cond_signal(fpv_cond_t* cond) {
  pthread_cond_signal(&cond->handle);
}

static inline void fpv_cond_broadcast(fpv_cond_t* cond) {
  pthread_cond_broadcast(&cond->handle);
}

static inline void fpv_cond_destroy(fpv_cond_t* cond) {
  if (cond && cond->initialized) {
    pthread_cond_destroy(&cond->handle);
    cond->initialized = false;
  }
}

typedef void* (*fpv_thread_fn)(void* context);

typedef struct fpv_thread {
  pthread_t handle;
  bool started;
} fpv_thread_t;

static inline bool fpv_thread_create(
    fpv_thread_t* thread,
    fpv_thread_fn fn,
    void* context) {
  if (!thread || !fn) {
    return false;
  }
  if (pthread_create(&thread->handle, NULL, fn, context) != 0) {
    return false;
  }
  thread->started = true;
  return true;
}

static inline void fpv_thread_join(fpv_thread_t* thread) {
  if (!thread || !thread->started) {
    return;
  }
  pthread_join(thread->handle, NULL);
  thread->started = false;
}

static inline void fpv_thread_detach(fpv_thread_t* thread) {
  if (!thread || !thread->started) {
    return;
  }
  pthread_detach(thread->handle);
  thread->started = false;
}
#endif

#endif

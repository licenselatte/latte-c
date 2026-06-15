#ifndef LATTE_THREAD_H
#define LATTE_THREAD_H

/* Thin pthreads / Win32 wrapper for the renewal background thread. */

#if defined(_WIN32)
#  include <windows.h>
   typedef HANDLE ll_thread;
   typedef CRITICAL_SECTION ll_mutex;
#else
#  include <pthread.h>
   typedef pthread_t ll_mutex_thread;
   typedef struct { pthread_t handle; int valid; } ll_thread;
   typedef pthread_mutex_t ll_mutex;
#endif

void ll_mutex_init(ll_mutex *m);
void ll_mutex_destroy(ll_mutex *m);
void ll_mutex_lock(ll_mutex *m);
void ll_mutex_unlock(ll_mutex *m);

/* Spawn a detached thread; the thread function receives arg and returns void*. */
int ll_thread_spawn(ll_thread *t, void *(*fn)(void *), void *arg);

/* Join (wait for) a previously spawned thread. */
void ll_thread_join(ll_thread *t);

int ll_thread_valid(const ll_thread *t);

#endif /* LATTE_THREAD_H */

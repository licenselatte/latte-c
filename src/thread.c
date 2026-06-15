#include "thread.h"
#include <string.h>

#if defined(_WIN32)

void ll_mutex_init(ll_mutex *m)    { InitializeCriticalSection(m); }
void ll_mutex_destroy(ll_mutex *m) { DeleteCriticalSection(m); }
void ll_mutex_lock(ll_mutex *m)    { EnterCriticalSection(m); }
void ll_mutex_unlock(ll_mutex *m)  { LeaveCriticalSection(m); }

typedef struct { void *(*fn)(void *); void *arg; } win_thread_arg;

static DWORD WINAPI win_thread_start(LPVOID param)
{
    win_thread_arg *a = (win_thread_arg *)param;
    void *(*fn)(void *) = a->fn;
    void *arg = a->arg;
    free(a);
    fn(arg);
    return 0;
}

int ll_thread_spawn(ll_thread *t, void *(*fn)(void *), void *arg)
{
    win_thread_arg *a = malloc(sizeof(win_thread_arg));
    if (!a) return -1;
    a->fn = fn; a->arg = arg;
    *t = CreateThread(NULL, 0, win_thread_start, a, 0, NULL);
    return *t ? 0 : -1;
}

void ll_thread_join(ll_thread *t)
{
    if (*t) { WaitForSingleObject(*t, INFINITE); CloseHandle(*t); *t = NULL; }
}

int ll_thread_valid(const ll_thread *t) { return *t != NULL; }

#else /* POSIX */

void ll_mutex_init(ll_mutex *m)    { pthread_mutex_init(m, NULL); }
void ll_mutex_destroy(ll_mutex *m) { pthread_mutex_destroy(m); }
void ll_mutex_lock(ll_mutex *m)    { pthread_mutex_lock(m); }
void ll_mutex_unlock(ll_mutex *m)  { pthread_mutex_unlock(m); }

int ll_thread_spawn(ll_thread *t, void *(*fn)(void *), void *arg)
{
    t->valid = 0;
    int rc = pthread_create(&t->handle, NULL, fn, arg);
    if (rc != 0) return -1;
    t->valid = 1;
    return 0;
}

void ll_thread_join(ll_thread *t)
{
    if (t->valid) {
        pthread_join(t->handle, NULL);
        t->valid = 0;
    }
}

int ll_thread_valid(const ll_thread *t) { return t->valid; }

#endif

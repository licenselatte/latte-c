/*
 * ll_file_save_token replaces the cache file atomically: while two threads save
 * records of very different sizes, a concurrent load sees one whole record
 * (never empty, truncated or interleaved) and no temp files are left behind.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#  include <windows.h>
#  include <direct.h>
#else
#  include <dirent.h>
#  include <unistd.h>
#endif

#include "../src/storage/file.h"
#include "../src/domain.h"
#include "../src/thread.h"

static int pass = 0, fail = 0;

#define CHECK(expr, name) \
    do { \
        if (expr) { printf("  PASS  %s\n", name); pass++; } \
        else      { printf("  FAIL  %s\n", name); fail++; } \
    } while(0)

#define ROUNDS 2000
#define BIG    20000

static char path[512];
static char *payload[2];
static volatile int writers_done;
static int save_failures, bad_loads, loads;
static ll_mutex mu;

static void *writer(void *arg)
{
    int which = (int)(size_t)arg;
    ll_cert_chain chain = { payload[which], payload[which], payload[which] };
    int failed = 0;
    for (int i = 0; i < ROUNDS; i++)
        if (ll_file_save_token(path, payload[which], &chain) != 0) failed++;
    ll_mutex_lock(&mu);
    save_failures += failed;
    writers_done++;
    ll_mutex_unlock(&mu);
    return NULL;
}

static void *reader(void *arg)
{
    (void)arg;
    int bad = 0, n = 0;
    while (writers_done < 2) {
        char *tok = NULL;
        ll_cert_chain *chain = NULL;
        int ok = ll_file_load_token(path, &tok, &chain) == 0 && tok && chain
              && (strcmp(tok, payload[0]) == 0 || strcmp(tok, payload[1]) == 0)
              && chain->submaster && strcmp(chain->submaster, tok) == 0
              && chain->project   && strcmp(chain->project, tok) == 0
              && chain->daily     && strcmp(chain->daily, tok) == 0;
        if (!ok) bad++;
        n++;
        free(tok);
        ll_cert_chain_free(chain);
    }
    ll_mutex_lock(&mu);
    bad_loads = bad;
    loads = n;
    ll_mutex_unlock(&mu);
    return NULL;
}

int main(void)
{
    printf("=== test_storage_atomic ===\n");

#if defined(_WIN32)
    char dir[MAX_PATH + 32];
    DWORD dn = GetTempPathA(MAX_PATH, dir);
    snprintf(dir + dn, sizeof(dir) - dn, "latte_atomic_%lu", (unsigned long)GetCurrentProcessId());
    if (_mkdir(dir) != 0) { perror("_mkdir"); return 1; }
    snprintf(path, sizeof(path), "%s\\cache.latte", dir);
#else
    char dir[] = "/tmp/latte_atomicXXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    snprintf(path, sizeof(path), "%s/cache.latte", dir);
#endif

    payload[0] = strdup("small");
    payload[1] = malloc(BIG + 1);
    memset(payload[1], 'B', BIG);
    payload[1][BIG] = '\0';

    ll_cert_chain seed = { payload[0], payload[0], payload[0] };
    int seeded = ll_file_save_token(path, payload[0], &seed);
    CHECK(seeded == 0, "initial save returns 0");

    ll_mutex_init(&mu);
    ll_thread t[3];
    ll_thread_spawn(&t[0], writer, (void *)(size_t)0);
    ll_thread_spawn(&t[1], writer, (void *)(size_t)1);
    ll_thread_spawn(&t[2], reader, NULL);
    for (int i = 0; i < 3; i++) ll_thread_join(&t[i]);
    ll_mutex_destroy(&mu);

    CHECK(save_failures == 0, "every concurrent save returns 0");
    CHECK(loads > 0, "reader completed loads");
    CHECK(bad_loads == 0, "every load yields a whole record");

    char *tok = NULL;
    ll_cert_chain *chain = NULL;
    int r = ll_file_load_token(path, &tok, &chain);
    CHECK(r == 0 && tok && (strcmp(tok, payload[0]) == 0 || strcmp(tok, payload[1]) == 0),
          "final file holds one whole record");
    free(tok);
    ll_cert_chain_free(chain);

    int leftovers = 0;
#if defined(_WIN32)
    char pattern[MAX_PATH + 64];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE d = FindFirstFileA(pattern, &fd);
    for (BOOL more = d != INVALID_HANDLE_VALUE; more; more = FindNextFileA(d, &fd))
        if (strcmp(fd.cFileName, ".") && strcmp(fd.cFileName, "..") && strcmp(fd.cFileName, "cache.latte"))
            leftovers++;
    if (d != INVALID_HANDLE_VALUE) FindClose(d);
#else
    DIR *d = opendir(dir);
    for (struct dirent *e = d ? readdir(d) : NULL; e; e = readdir(d))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..") && strcmp(e->d_name, "cache.latte"))
            leftovers++;
    if (d) closedir(d);
#endif
    CHECK(leftovers == 0, "no temp files left behind");

    remove(path);
#if defined(_WIN32)
    _rmdir(dir);
#else
    rmdir(dir);
#endif
    free(payload[0]);
    free(payload[1]);

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}

// lru_cache.h
#ifndef LRU_CACHE_H
#define LRU_CACHE_H

#define _GNU_SOURCE
#include <stddef.h>
#include <pthread.h>

typedef struct lru_cache_t lru_cache_t;

/* Initialize an LRU cache instance.
 * - cache: pointer to an allocated lru_cache_t (can be on stack or heap)
 * - max_bytes: maximum total bytes the cache may hold (per worker)
 * - max_file_size: maximum single-file size to cache
 * Returns 0 on success, -1 on failure.
 */
int lru_cache_init(lru_cache_t *cache, int max_bytes, int max_file_size);

/* Destroy cache and free internal memory. */
void lru_cache_destroy(lru_cache_t *cache);

/* Allocate and initialize a new cache instance (returns NULL on failure).
 * Use `lru_cache_destroy` to free it.
 */
lru_cache_t* lru_cache_create(int max_bytes, int max_file_size);

/* Free a cache instance previously returned by lru_cache_create.
 * This is equivalent to calling lru_cache_destroy and free().
 */
void lru_cache_free(lru_cache_t *cache);

/* Lookup a key in the cache.
 * - if found returns 1 and sets *data and *len to point to the internal buffer (read-only).
 * - if not found returns 0.
 * - on error returns -1.
 * The returned data pointer is owned by the cache and must not be modified or freed by the caller.
 */
int lru_cache_get(lru_cache_t *cache, const char *key, const void **data, size_t *len);

/* Put a key/data pair into the cache. Copies key and data into the cache.
 * - returns 0 on success, 1 if not cached due to size limits, -1 on error.
 */
int lru_cache_put(lru_cache_t *cache, const char *key, const void *data, size_t len);

/* Safe getter that returns a newly-allocated copy of the cached data.
 * - on hit returns 1 and sets *outbuf (caller must free) and *outlen
 * - on miss returns 0
 * - on error returns -1
 */
int lru_cache_get_copy(lru_cache_t *cache, const char *key, void **outbuf, size_t *outlen);

#endif

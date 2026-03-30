// lru_cache.c
#define _GNU_SOURCE
#include "lru_cache.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

/* Simple LRU cache implementation:
 * - Hash table (separate chaining) for lookups
 * - Doubly-linked list to maintain recency (head = most recent)
 */

#define LRU_DEFAULT_BUCKETS 1024

typedef struct lru_entry {
    char *key;
    size_t key_len;
    void *data;
    size_t data_len;
    struct lru_entry *prev;
    struct lru_entry *next;
    struct lru_entry *hnext; /* next in hash bucket */
} lru_entry_t;

struct lru_cache_t {
    pthread_rwlock_t lock;
    int max_bytes;
    int max_file_size;
    int cur_bytes;
    size_t nbuckets;
    lru_entry_t **buckets;
    lru_entry_t *head;
    lru_entry_t *tail;
};

static unsigned long hash_key(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = (unsigned char)*str++)) hash = ((hash << 5) + hash) + c;
    return hash;
}

int lru_cache_init(lru_cache_t *cache, int max_bytes, int max_file_size) {
    if (!cache || max_bytes <= 0 || max_file_size <= 0) return -1;
    cache->max_bytes = max_bytes;
    cache->max_file_size = max_file_size;
    cache->cur_bytes = 0;
    cache->nbuckets = LRU_DEFAULT_BUCKETS;
    cache->buckets = (lru_entry_t**)calloc(cache->nbuckets, sizeof(lru_entry_t*));
    if (!cache->buckets) return -1;
    cache->head = cache->tail = NULL;
    if (pthread_rwlock_init(&cache->lock, NULL) != 0) {
        free(cache->buckets);
        return -1;
    }
    return 0;
}

lru_cache_t* lru_cache_create(int max_bytes, int max_file_size) {
    lru_cache_t *c = (lru_cache_t*)malloc(sizeof(lru_cache_t));
    if (!c) return NULL;
    if (lru_cache_init(c, max_bytes, max_file_size) != 0) {
        free(c);
        return NULL;
    }
    return c;
}

static void unlink_entry(lru_cache_t *cache, lru_entry_t *e) {
    if (!e) return;
    if (e->prev) e->prev->next = e->next; else cache->head = e->next;
    if (e->next) e->next->prev = e->prev; else cache->tail = e->prev;
    e->prev = e->next = NULL;
}

static void link_front(lru_cache_t *cache, lru_entry_t *e) {
    e->prev = NULL;
    e->next = cache->head;
    if (cache->head) cache->head->prev = e; else cache->tail = e;
    cache->head = e;
}

static void evict_one(lru_cache_t *cache) {
    /* evict tail (least recently used) */
    lru_entry_t *e = cache->tail;
    if (!e) return;
    /* remove from hash bucket */
    unsigned long h = hash_key(e->key) % cache->nbuckets;
    lru_entry_t *prev = NULL;
    lru_entry_t *cur = cache->buckets[h];
    while (cur) {
        if (cur == e) {
            if (prev) prev->hnext = cur->hnext; else cache->buckets[h] = cur->hnext;
            break;
        }
        prev = cur;
        cur = cur->hnext;
    }
    unlink_entry(cache, e);
    cache->cur_bytes -= (int)e->data_len;
    free(e->data);
    free(e->key);
    free(e);
}

void lru_cache_destroy(lru_cache_t *cache) {
    if (!cache) return;
    pthread_rwlock_wrlock(&cache->lock);
    for (size_t i = 0; i < cache->nbuckets; i++) {
        lru_entry_t *e = cache->buckets[i];
        while (e) {
            lru_entry_t *n = e->hnext;
            free(e->data);
            free(e->key);
            free(e);
            e = n;
        }
    }
    free(cache->buckets);
    cache->buckets = NULL;
    cache->head = cache->tail = NULL;
    cache->cur_bytes = 0;
    pthread_rwlock_unlock(&cache->lock);
    pthread_rwlock_destroy(&cache->lock);
}

void lru_cache_free(lru_cache_t *cache) {
    if (!cache) return;
    lru_cache_destroy(cache);
    free(cache);
}

int lru_cache_get(lru_cache_t *cache, const char *key, const void **data, size_t *len) {
    if (!cache || !key || !data || !len) return -1;
    pthread_rwlock_wrlock(&cache->lock);
    unsigned long h = hash_key(key) % cache->nbuckets;
    lru_entry_t *e = cache->buckets[h];
    while (e) {
        if (strcmp(e->key, key) == 0) {
            /* move to front */
            unlink_entry(cache, e);
            link_front(cache, e);
            *data = e->data;
            *len = e->data_len;
            pthread_rwlock_unlock(&cache->lock);
            return 1; /* hit */
        }
        e = e->hnext;
    }
    pthread_rwlock_unlock(&cache->lock);
    return 0; /* miss */
}

/* THREAD-SAFE CACHE LOOKUP WITH READ-WRITER LOCK: multiple reader threads can lookup simultaneously.
   uses reader-writer lock (rwlock) which allows many readers concurrently but exclusive writer access.
   this is much more efficient than mutex for read-heavy workloads (most cache accesses are hits).
   
   the key optimization: lru_cache_get_copy() acquires READ lock (rdlock) instead of write lock.
   with 10 threads per worker hitting cache: all 10 can execute simultaneously if just reading.
   only eviction (put) or updates need the WRITE lock, which blocks both readers and writers.
   this explains why cache reads are so cheap in the critical path. */
int lru_cache_get_copy(lru_cache_t *cache, const char *key, void **outbuf, size_t *outlen) {
    if (!cache || !key || !outbuf || !outlen) return -1;
    
    /* ACQUIRE READ LOCK: allows concurrent readers, blocks only if writer active.
       this is the key to achieving high throughput on cache hits. */
    if (pthread_rwlock_rdlock(&cache->lock) != 0) return -1;
    
    /* HASH TABLE LOOKUP: O(1) average case via hash(key) modulo bucket count */
    unsigned long h = hash_key(key) % cache->nbuckets;
    lru_entry_t *e = cache->buckets[h];  /* bucket head in collision chain */
    
    /* LINEAR SEARCH IN COLLISION CHAIN: find entry with matching key */
    while (e) {
        if (strcmp(e->key, key) == 0) {
            /* KEY FOUND (cache hit) */
            size_t len = e->data_len;
            void *buf = malloc(len);  /* allocate buffer for caller */
            if (!buf) { pthread_rwlock_unlock(&cache->lock); return -1; }
            memcpy(buf, e->data, len);  /* copy data while read-lock held */
            *outbuf = buf;
            *outlen = len;
            pthread_rwlock_unlock(&cache->lock);
            return 1; /* signal cache hit to caller */
        }
        e = e->hnext;  /* follow collision chain to next entry */
    }
    
    pthread_rwlock_unlock(&cache->lock);
    return 0; /* cache miss: key not found */
}

int lru_cache_put(lru_cache_t *cache, const char *key, const void *data, size_t len) {
    if (!cache || !key || !data) return -1;
    if ((int)len > cache->max_file_size) return 1; /* too large to cache */
    if ((int)len > cache->max_bytes) return 1; /* larger than entire cache */

    pthread_rwlock_wrlock(&cache->lock);
    /* If key already exists, update it (move to front and replace data) */
    unsigned long h = hash_key(key) % cache->nbuckets;
    lru_entry_t *e = cache->buckets[h];
    while (e) {
        if (strcmp(e->key, key) == 0) {
            /* replace data buffer */
            void *newbuf = malloc(len);
            if (!newbuf) { pthread_rwlock_unlock(&cache->lock); return -1; }
            memcpy(newbuf, data, len);
            cache->cur_bytes -= (int)e->data_len;
            free(e->data);
            e->data = newbuf;
            e->data_len = len;
            cache->cur_bytes += (int)len;
            /* move to front */
            unlink_entry(cache, e);
            link_front(cache, e);
            /* evict if needed */
            while (cache->cur_bytes > cache->max_bytes) evict_one(cache);
            pthread_rwlock_unlock(&cache->lock);
            return 0;
        }
        e = e->hnext;
    }

    /* create new entry */
    lru_entry_t *ne = (lru_entry_t*)malloc(sizeof(lru_entry_t));
    if (!ne) { pthread_rwlock_unlock(&cache->lock); return -1; }
    ne->key = strdup(key);
    if (!ne->key) { free(ne); pthread_rwlock_unlock(&cache->lock); return -1; }
    ne->key_len = strlen(ne->key);
    ne->data = malloc(len);
    if (!ne->data) { free(ne->key); free(ne); pthread_rwlock_unlock(&cache->lock); return -1; }
    memcpy(ne->data, data, len);
    ne->data_len = len;
    ne->prev = ne->next = ne->hnext = NULL;

    /* attach to hash bucket */
    ne->hnext = cache->buckets[h];
    cache->buckets[h] = ne;

    /* insert at front */
    link_front(cache, ne);
    cache->cur_bytes += (int)len;

    /* evict if needed */
    while (cache->cur_bytes > cache->max_bytes) evict_one(cache);

    pthread_rwlock_unlock(&cache->lock);
    return 0;
}

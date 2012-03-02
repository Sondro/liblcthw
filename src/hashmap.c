#undef NDEBUG
#include "Hashmap.h"
#include "dbg.h"
#include <bstrlib.h>
#include <stdint.h>
#include <radixmap.h>

static int default_compare(void *a, void *b)
{
    return bstrcmp((bstring)a, (bstring)b);
}

/** 
 * Simple Bob Jenkin's hash algorithm taken from the
 * wikipedia description.
 */
static uint32_t default_hash(void *a)
{
    size_t len = blength((bstring)a);
    char *key = bdata((bstring)a);
    uint32_t hash = 0;
    uint32_t i = 0;

    for(hash = i = 0; i < len; ++i)
    {
        hash += key[i];
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }

    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);

    return hash;
}


Hashmap *Hashmap_create(Hashmap_compare compare, Hashmap_hash hash)
{
    Hashmap *map = calloc(1, sizeof(Hashmap));
    check_mem(map);

    map->compare = compare == NULL ? default_compare : compare;
    map->hash = hash == NULL ? default_hash : hash;
    map->buckets = darray_create(sizeof(darray_t *), DEFAULT_NUMBER_OF_BUCKETS);
    map->buckets->end = map->buckets->max; // fake out expanding it
    check_mem(map->buckets);

    return map;

error:
    if(map) {
        Hashmap_destroy(map);
    }

    return NULL;
}


void Hashmap_destroy(Hashmap *map)
{
    int i = 0;
    int j = 0;

    // TODO: pack up this double-for-loop pattern into a macro somehow
    if(map) {
        if(map->buckets) {
            for(i = 0; i < darray_count(map->buckets); i++) {
                darray_t *bucket = darray_get(map->buckets, i);
                if(bucket) {
                    for(j = 0; j < darray_count(bucket); j++) {
                        free(darray_get(bucket, j));
                    }
                    darray_destroy(bucket);
                }
            }
            darray_destroy(map->buckets);
        }

        free(map);
    }
}

static inline HashmapNode *Hashmap_node_create(int hash, void *key, void *data)
{
    HashmapNode *node = calloc(1, sizeof(HashmapNode));
    check_mem(node);

    node->key = key;
    node->data = data;
    node->hash = hash;

    return node;
error:
    return NULL;
}


static inline darray_t *Hashmap_find_bucket(Hashmap *map, void *key,
        int create, uint32_t *hash_out)
{
    uint32_t hash = map->hash(key);
    int bucket_n = hash % DEFAULT_NUMBER_OF_BUCKETS;
    check(bucket_n >= 0, "Invalid bucket found: %d", bucket_n);
    *hash_out = hash; // store it for the return so the caller can use it


    darray_t *bucket = darray_get(map->buckets, bucket_n);

    if(!bucket && create) {
        // new bucket, set it up
        bucket = darray_create(sizeof(void *), DEFAULT_NUMBER_OF_BUCKETS);
        check_mem(bucket);
        darray_set(map->buckets, bucket_n, bucket);
    }

    return bucket;

error:
    return NULL;
}


int Hashmap_set(Hashmap *map, void *key, void *data)
{
    uint32_t hash = 0;
    darray_t *bucket = Hashmap_find_bucket(map, key, 1, &hash);
    check(bucket, "Error can't create bucket.");

    HashmapNode *node = Hashmap_node_create(hash, key, data);
    check_mem(node);

    // TODO: do sorting on the darray to make finding faster by the hash+key
    darray_push(bucket, node);

    return 0;

error:
    return -1;
}

static inline int Hashmap_get_node(Hashmap *map, uint32_t hash, darray_t *bucket, void *key)
{
    int i = 0;

    for(i = 0; i < darray_end(bucket); i++) {
        debug("TRY: %d", i);
        HashmapNode *node = darray_get(bucket, i);
        if(node->hash == hash && map->compare(node->key, key) == 0) {
            return i;
        }
    }

    return -1;
}

void *Hashmap_get(Hashmap *map, void *key)
{
    uint32_t hash = 0;
    darray_t *bucket = Hashmap_find_bucket(map, key, 0, &hash);
    if(!bucket) return NULL;

    int i = Hashmap_get_node(map, hash, bucket, key);
    if(i == -1) return NULL;

    HashmapNode *node = darray_get(bucket, i);
    check(node != NULL, "Failed to get node from bucket when it should exist.");

    return node->data;

error: // fallthrough
    return NULL;
}


int Hashmap_traverse(Hashmap *map, Hashmap_traverse_cb traverse_cb) 
{
    int i = 0;
    int j = 0;
    int rc = 0;

    for(i = 0; i < darray_count(map->buckets); i++) {
        darray_t *bucket = darray_get(map->buckets, i);
        if(bucket) {
            for(j = 0; j < darray_count(bucket); j++) {
                HashmapNode *node = darray_get(bucket, j);
                rc = traverse_cb(node);
                if(rc != 0) return rc;
            }
        }
    }

    return 0;
}

void *Hashmap_delete(Hashmap *map, void *key)
{
    uint32_t hash = 0;
    darray_t *bucket = Hashmap_find_bucket(map, key, 0, &hash);
    if(!bucket) return NULL;

    int i = Hashmap_get_node(map, hash, bucket, key);
    if(i == -1) return NULL;

    HashmapNode *node = darray_get(bucket, i);
    void *data = node->data;
    free(node);

    HashmapNode *ending = darray_pop(bucket);

    if(ending != node) {
        // alright looks like it's not the last one, swap it
        darray_set(bucket, i, ending);
    }

    return data; 
}

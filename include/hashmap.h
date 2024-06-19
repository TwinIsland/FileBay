#include <stdlib.h>
#include <string.h>

typedef struct
{
    unsigned int key;
    void *value;
} HashmapEntry;

/*
 * general implement for hasmap
 *
 * Warning! the hashmap implement cannot deal with hash collision!
 */
typedef struct
{
    HashmapEntry **entries;
    int size;
} Hashmap;

unsigned int hash(unsigned int key, int hashmapSize);
Hashmap *createHashmap(int size);
void insert(Hashmap *hashmap, unsigned int key, void *value);
void *search(Hashmap *hashmap, unsigned int key);
void delete(Hashmap *hashmap, unsigned int key);
void freeHashmap(Hashmap *hashmap);

#ifdef HASHMAP_IMPLEMENTATION
unsigned int hash(unsigned int key, int hashmapSize)
{
    key = ((key >> 16) ^ key) * 0x45d9f3b;
    key = ((key >> 16) ^ key) * 0x45d9f3b;
    key = (key >> 16) ^ key;

    return key % hashmapSize;
}

Hashmap *createHashmap(int size)
{
    Hashmap *hashmap = malloc(sizeof(Hashmap));
    hashmap->size = size;
    hashmap->entries = malloc(sizeof(HashmapEntry *) * size);

    for (int i = 0; i < size; ++i)
    {
        hashmap->entries[i] = NULL;
    }

    return hashmap;
}

// Insert an entry into the hashmap, return if collide, not insert if collide
int hashmap_insert(Hashmap *hashmap, unsigned int key, void *value)
{
    int index = hash(key, hashmap->size);

    if (hashmap->entries[index] != NULL)
        return 1;

    HashmapEntry *entry = malloc(sizeof(HashmapEntry));
    entry->key = key;
    entry->value = value;

    hashmap->entries[index] = entry;
    return 0;
}

// Search for an entry in the hashmap.
void *hashmap_search(Hashmap *hashmap, unsigned int key)
{
    int index = hash(key, hashmap->size);
    if (hashmap->entries[index] != NULL && hashmap->entries[index]->key == key)
    {
        return hashmap->entries[index]->value;
    }

    return NULL;
}

// Delete an entry from the hashmap.
void hashmap_delete(Hashmap *hashmap, unsigned int key)
{
    int index = hash(key, hashmap->size);
    if (hashmap->entries[index] != NULL)
    {
        free(hashmap->entries[index]);
        hashmap->entries[index] = NULL;
    }
}

// Free the hashmap.
void freeHashmap(Hashmap *hashmap)
{
    for (int i = 0; i < hashmap->size; i++)
    {
        if (hashmap->entries[i] != NULL)
        {
            free(hashmap->entries[i]);
        }
    }

    free(hashmap->entries);
    free(hashmap);
}
#endif
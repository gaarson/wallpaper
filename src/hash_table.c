// hash_table.c
#include "common.h"
#include "hash_table.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h> // For strcmp, strdup
#include <stdint.h> // For uint32_t if needed for hash

// --- Simple Hash Function (djb2) ---
static size_t hash_function(const char* key, size_t table_size) {
    unsigned long hash = 5381;
    int c;
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash % table_size;
}

// --- Create ---
HashTable* ht_create(size_t size) {
    if (size == 0) {
        fprintf(stderr, "Hash Table Error: Cannot create table with size 0.\n");
        return NULL;
    }
    HashTable* table = malloc(sizeof(HashTable));
    if (!table) {
        perror("ht_create: malloc HashTable");
        return NULL;
    }
    // Use calloc for buckets to initialize all pointers to NULL
    table->buckets = calloc(size, sizeof(HashTableNode*));
    if (!table->buckets) {
        perror("ht_create: calloc buckets");
        free(table);
        return NULL;
    }
    table->size = size;
    table->count = 0;
    log_debug("Hash Table: Created with %zu buckets.\n", size);
    return table;
}

// --- Destroy ---
void ht_destroy(HashTable* table) {
    if (!table) return;
    log_debug("Hash Table: Destroying...\n");
    for (size_t i = 0; i < table->size; ++i) {
        HashTableNode* current = table->buckets[i];
        while (current != NULL) {
            HashTableNode* next = current->next;
            log_debug("  -> Freeing node for key: %s\n", current->key);
            free(current->key); // Free the copied key
            free(current);      // Free the node itself
            current = next;
        }
    }
    free(table->buckets);
    free(table);
     log_debug("Hash Table: Destroyed.\n");
}

// --- Insert ---
bool ht_insert(HashTable* table, const char* key, const RenderModeInterface* value) {
    if (!table || !key || !value) return false;

    size_t index = hash_function(key, table->size);

    // Check if key already exists (optional, simple version just adds)
    // HashTableNode* existing = table->buckets[index];
    // while (existing != NULL) {
    //     if (strcmp(existing->key, key) == 0) {
    //         fprintf(stderr, "Hash Table Warning: Key '%s' already exists. Not inserting.\n", key);
    //         return false; // Or update value if desired
    //     }
    //     existing = existing->next;
    // }

    // Create new node
    HashTableNode* new_node = malloc(sizeof(HashTableNode));
    if (!new_node) {
        perror("ht_insert: malloc node");
        return false;
    }
    new_node->key = strdup(key); // Duplicate the key string
    if (!new_node->key) {
        perror("ht_insert: strdup key");
        free(new_node);
        return false;
    }
    new_node->value = value;

    // Insert at the beginning of the chain (simplest)
    new_node->next = table->buckets[index];
    table->buckets[index] = new_node;
    table->count++;

    log_debug("Hash Table: Inserted key '%s' at index %zu.\n", key, index);
    return true;
}

// --- Lookup ---
const RenderModeInterface* ht_lookup(HashTable* table, const char* key) {
    if (!table || !key) return NULL;

    size_t index = hash_function(key, table->size);
    HashTableNode* current = table->buckets[index];

    while (current != NULL) {
        if (strcmp(current->key, key) == 0) {
            return current->value; // Found
        }
        current = current->next;
    }

    return NULL; // Not found
}

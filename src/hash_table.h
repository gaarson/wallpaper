// hash_table.h
#ifndef HASH_TABLE_H
#define HASH_TABLE_H

#include <stddef.h> // For size_t
#include "renderer/mode.h" // Need definition of RenderModeInterface

// --- Node Structure ---
typedef struct HashTableNode {
    char* key;                        // Key (mode name string) - owned by the node
    const RenderModeInterface* value; // Value (pointer to the interface) - not owned
    struct HashTableNode* next;       // Pointer for chaining
} HashTableNode;

// --- Hash Table Structure ---
typedef struct {
    HashTableNode** buckets; // Array of pointers to nodes (bucket heads)
    size_t size;             // Number of buckets
    size_t count;            // Number of elements stored
} HashTable;

// --- Function Prototypes ---

/**
 * @brief Creates a new hash table.
 * @param size The number of buckets in the table. Should ideally be a prime number.
 * @return Pointer to the newly created hash table, or NULL on allocation failure.
 */
HashTable* ht_create(size_t size);

/**
 * @brief Destroys a hash table and frees associated memory.
 * Does NOT free the RenderModeInterface pointers stored as values.
 * @param table Pointer to the hash table to destroy.
 */
void ht_destroy(HashTable* table);

/**
 * @brief Inserts a key-value pair into the hash table.
 * If the key already exists, the behavior is undefined (simple implementation doesn't update).
 * Makes a copy of the key string.
 * @param table Pointer to the hash table.
 * @param key The key string (mode name).
 * @param value Pointer to the RenderModeInterface.
 * @return true on success, false on allocation failure.
 */
bool ht_insert(HashTable* table, const char* key, const RenderModeInterface* value);

/**
 * @brief Looks up a value associated with a key.
 * @param table Pointer to the hash table.
 * @param key The key string to search for.
 * @return Pointer to the RenderModeInterface if found, otherwise NULL.
 */
const RenderModeInterface* ht_lookup(HashTable* table, const char* key);

#endif // HASH_TABLE_H

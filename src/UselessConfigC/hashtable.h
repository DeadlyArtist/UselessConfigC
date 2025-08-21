#ifndef USEC_HASHTABLE_H
#define USEC_HASHTABLE_H

#include "parser.h"
#include "usec.h"
#include <stddef.h>

// Node (linked list for collision)
typedef struct Usec_HashNode {
	char* key;
	USEC_Value* value;
	struct Usec_HashNode* next;
} Usec_HashNode;

// Hashtable structure
typedef struct Usec_Hashtable {
    size_t capacity;
    size_t size;
    Usec_HashNode** buckets;
} Usec_Hashtable;

Usec_Hashtable* usec_ht_create(size_t capacity);
void usec_ht_set(Usec_Hashtable* ht, const char* key, USEC_Value* value);
USEC_Value* usec_ht_get(Usec_Hashtable* ht, const char* key);
void usec_ht_free(Usec_Hashtable* ht);
Usec_Hashtable* usec_ht_from(const Usec_Hashtable* source);

#endif
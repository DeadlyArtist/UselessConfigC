#include "hashtable.h"
#include "usec.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Simple hash function (djb2)
static unsigned long djb2(const char* str) {
	unsigned long hash = 5381;
	int c;
	while ((c = *(str++)))
		hash = ((hash << 5) + hash) + c;
	return hash;
}

Usec_Hashtable* usec_ht_create(size_t capacity) {
	Usec_Hashtable* ht = malloc(sizeof(Usec_Hashtable));
	ht->capacity = capacity;
	ht->size = 0;
	ht->buckets = calloc(capacity, sizeof(Usec_HashNode*));
	return ht;
}

void usec_ht_set(Usec_Hashtable* ht, const char* key, USEC_Value* value) {
	unsigned long hash = djb2(key) % ht->capacity;
	Usec_HashNode* node = ht->buckets[hash];

	while (node) {
		if (strcmp(node->key, key) == 0) {
			// Replace existing value
			if (node->value) usec_parser_free_value(node->value);
			node->value = value;
			return;
		}
		node = node->next;
	}

	// New entry
	node = malloc(sizeof(Usec_HashNode));
	node->key = strdup(key);
	node->value = value;
	node->next = ht->buckets[hash];
	ht->buckets[hash] = node;
	ht->size++;
}

USEC_Value* usec_ht_get(Usec_Hashtable* ht, const char* key) {
	unsigned long hash = djb2(key) % ht->capacity;
	Usec_HashNode* node = ht->buckets[hash];

	while (node) {
		if (strcmp(node->key, key) == 0)
			return node->value;
		node = node->next;
	}
	return NULL;
}

void usec_ht_free(Usec_Hashtable* ht) {
	for (size_t i = 0; i < ht->capacity; ++i) {
		Usec_HashNode* node = ht->buckets[i];
		while (node) {
			Usec_HashNode* next = node->next;
			free(node->key);
			usec_parser_free_value(node->value);
			free(node);
			node = next;
		}
	}
	free(ht->buckets);
	free(ht);
}

// Others
Usec_Hashtable* usec_ht_from(const Usec_Hashtable* source) {
	if (!source) return NULL;

	Usec_Hashtable* dest = usec_ht_create(source->capacity);

	for (size_t i = 0; i < source->capacity; ++i) {
		Usec_HashNode* node = source->buckets[i];
		while (node) {
			USEC_Value* copy = usec_clone_value(node->value);
			usec_ht_set(dest, node->key, copy);
			node = node->next;
		}
	}

	return dest;
}

#ifndef USEC_H
#define USEC_H

#ifdef __cplusplus
extern "C" {
#endif

	typedef struct USEC_Value USEC_Value;
	typedef struct Usec_Hashtable Usec_Hashtable;
	typedef struct Usec_HashNode Usec_HashNode;

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

	// ==============================
	//      USEC Data Structures
	// ==============================


	// Generic representation of values
	typedef enum {
		VALUE_NULL,
		VALUE_BOOL,
		VALUE_UINT,
		VALUE_INT,
		VALUE_DOUBLE,
		VALUE_STRING,
		VALUE_CHAR,
		VALUE_ARRAY,
		VALUE_OBJECT,

		// Formatting support types:
		VALUE_FORMAT,
		VALUE_COMMENT,
		VALUE_MULTILINE_COMMENT,
		VALUE_NEWLINE
	} USEC_ValueType;

	typedef struct USEC_FormatNode USEC_FormatNode;

	// Parsed value node
	struct USEC_Value {
		USEC_ValueType type;
		union {
			bool boolValue;
			double doubleValue;
			uint64_t uint64Value;
			int64_t int64Value;
			char* stringValue;
			char charValue;
			struct {
				USEC_Value** items;
				size_t count;
			} arrayValue;
			Usec_Hashtable* objectValue;

			// Formatting
			USEC_FormatNode* formatNode;
			char* commentText;
			int newline_count;
		};
	};

	typedef struct USEC_FormatNode {
		struct USEC_Value* node;
		struct USEC_Value** before;
		size_t before_count;
		struct USEC_Value** after;
		size_t after_count;
	} USEC_FormatNode;

	USEC_Value* usec_create_comment(const char* text);
	USEC_Value* usec_create_multiline(const char* text);
	USEC_Value* usec_create_newline(int count);
	USEC_Value* usec_create_format(USEC_Value* main_node, USEC_Value** before, size_t before_count, USEC_Value** after, size_t after_count);

	// ==============================
	//      Configuration Struct
	// ==============================

	typedef struct {
		bool pedantic;
		bool keepVariables;
		bool debugTokens;
		bool debugParser;
		Usec_Hashtable* variables; // Note: The contents will be modified by the parser. To avoid, use usec_ht_from.
	} USEC_ParseOptions;

	typedef struct {
		bool readable;
		bool enable_variables;
	} USEC_ToStringOptions;

	/**
	 * Returns properly initialized default parse options.
	 *
	 * @return A USEC_ParseOptions struct with default values.
	 */
	USEC_ParseOptions usec_get_default_parse_options(void);

	/**
	 * Returns default options for usec_to_string.
	 *
	 * @return A USEC_ToStringOptions struct with sane default values.
	 */
	USEC_ToStringOptions usec_get_default_tostring_options(void);

	// ==============================
	//      Public API Functions
	// ==============================

	/**
	 * Deep clones a USEC_Value object
	 */
	USEC_Value* usec_clone(const USEC_Value* val);

	/**
	 * Parse a USEC string into a fully dynamic object.
	 *
	 * @param input Null-terminated USEC string
	 * @param options Optional; pass NULL for defaults
	 * @return Pointer to parsed USEC_Value tree, or NULL on error
	 */
	USEC_Value* usec_parse(const char* input, const USEC_ParseOptions* options);

	/**
	 * Convert a USEC_Value tree back to a full file string.
	 *
	 * @param root The data structure to convert
	 * @param readable Whether to format with readable indentation
	 * @param enable_variables Enable variable injection
	 * @return Dynamically allocated string (caller must free)
	 */
	char* usec_to_string(const USEC_Value* root, const USEC_ToStringOptions* options);

	/**
	 * Convert a USEC_Value tree back to a string.
	 *
	 * @param root The data structure to convert
	 * @param readable Whether to format with readable indentation
	 * @param enable_variables Enable variable injection
	 * @return Dynamically allocated string (caller must free)
	 */
	char* usec_to_value_string(const USEC_Value* root, const USEC_ToStringOptions* options);

	/**
	 * Free a USEC_Value and its contents recursively.
	 *
	 * @param root Pointer returned by usec_parse
	 */
	void usec_free(USEC_Value* root);

	/**
	 * Compare two USEC_Value trees for deep equality.
	 *
	 * @param a First tree
	 * @param b Second tree
	 * @return true if structurally equal
	 */
	bool usec_equals(const USEC_Value* a, const USEC_Value* b);

	// Represents a key-value pair node used internally by the Usec_Hashtable. Linked as a chain to handle hash collisions.
	struct Usec_HashNode {
		char* key;
		USEC_Value* value;
		struct Usec_HashNode* next;
		struct Usec_HashNode* order_prev;
		struct Usec_HashNode* order_next;
	};


    // A simple hash table for storing key-value pairs representing USEC object members. Preserves order.
	struct Usec_Hashtable {
		size_t capacity;
		size_t size;
		Usec_HashNode** buckets;
		Usec_HashNode* order_head;
		Usec_HashNode* order_tail;
	};

	Usec_Hashtable* usec_ht_create(size_t capacity);
	void usec_ht_set(Usec_Hashtable* ht, const char* key, USEC_Value* value);
	USEC_Value* usec_ht_get(Usec_Hashtable* ht, const char* key);
	void usec_ht_free(Usec_Hashtable* ht);
	void usec_ht_foreach(Usec_Hashtable* ht, void (*fn)(const char* key, USEC_Value* value));
	Usec_Hashtable* usec_ht_from(const Usec_Hashtable* source);


#ifdef __cplusplus
}
#endif

#endif // USEC_H
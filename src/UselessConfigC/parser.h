#ifndef USEC_PARSER_H
#define USEC_PARSER_H

#include "tokenizer.h"
#include "hashtable.h"
#include "usec.h"
#include <stdbool.h>
#include <stdint.h>

// Parser configuration and context
#define USEC_VAR_STACK_MAX 32

typedef struct {
	USEC_Token* tokens;
	size_t token_count;
	size_t index;
	bool pedantic;
	bool keep_variables;
	bool compact;
	bool debug;

	// Variables + stack of scopes
	Usec_Hashtable* variables; // top-level/global
	Usec_Hashtable* var_stack[USEC_VAR_STACK_MAX];
	size_t var_stack_size;
} USEC_Parser;

typedef enum {
	STATEMENT_ASSIGNMENT,
	STATEMENT_DECLARATION
} USEC_StatementType;

typedef struct {
	USEC_StatementType type;
	char* key;
	USEC_Value* value;
} USEC_Statement;

// === Functions ===

void usec_parser_init(USEC_Parser* parser, USEC_Token* tokens, size_t token_count, Usec_Hashtable* variables);
USEC_Value* usec_parser_parse(USEC_Parser* parser);
void usec_parser_free_value(USEC_Value* value);
void usec_parser_free(USEC_Parser* parser);

#endif
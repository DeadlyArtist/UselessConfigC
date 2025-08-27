#ifndef USEC_TOKENIZER_H
#define USEC_TOKENIZER_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
	TOK_NEWLINE,
	TOK_SPACE,
	TOK_IDENTIFIER,
	TOK_KEYWORD,
	TOK_STRING,
	TOK_STRING_START,
	TOK_STRING_END,
	TOK_INTERP_OPENER,
	TOK_INTERP_CLOSER,
	TOK_EXCLAMATION,
	TOK_COLON,
	TOK_EQUALS,
	TOK_ARRAY_OPEN,
	TOK_ARRAY_CLOSE,
	TOK_BRACE_OPEN,
	TOK_BRACE_CLOSE,
	TOK_PATH,
	TOK_DOT,
	TOK_WAVE,
	TOK_DOLLAR,
	TOK_CHAR,
	TOK_NUMBER,
	TOK_INVALID,
	TOK_COMMENT
} USEC_TokenType;

typedef struct USEC_Token {
	USEC_TokenType type;
	char* value;
	int line;
	int col;
} USEC_Token;

typedef struct USEC_Tokenizer {
	const char* input;
	size_t length;
	size_t index;
	int line;
	int col;
	bool compact;
	bool pedantic;
	bool debug;

	USEC_Token* tokens;
	size_t token_count;
	size_t token_capacity;

	USEC_Token* opener_stack;
	size_t opener_stack_size;
	size_t opener_stack_capacity;

	bool has_error;
} USEC_Tokenizer;

void usec_tokenizer_init(USEC_Tokenizer* t, const char* input, bool compact, bool pedantic, bool debug);
void usec_tokenizer_tokenize(USEC_Tokenizer* t);
void usec_tokenizer_destroy(USEC_Tokenizer* t);

#endif
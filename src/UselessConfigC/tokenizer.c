#include "tokenizer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "utils.h"


void usec_tokenizer_init(USEC_Tokenizer* t, const char* input, bool compact, bool pedantic, bool debug) {
	t->input = input;
	t->index = 0;
	t->line = 1;
	t->col = 1;
	t->compact = compact;
	t->pedantic = pedantic;
	t->debug = debug;
	t->has_error = false;
	t->token_count = 0;
	t->token_capacity = 0;
	t->tokens = NULL;
	t->opener_stack = NULL;
	t->opener_stack_size = 0;
	t->opener_stack_capacity = 0;
}

void usec_tokenizer_destroy(USEC_Tokenizer* t) {
	for (size_t i = 0; i < t->token_count; ++i) {
		free(t->tokens[i].value);
	}
	free(t->tokens);
	free(t->opener_stack);
}

static void grow_token_array(USEC_Tokenizer* t) {
	if (t->token_count >= t->token_capacity) {
		t->token_capacity = (t->token_capacity == 0) ? 64 : t->token_capacity * 2;
		t->tokens = realloc(t->tokens, sizeof(USEC_Token) * t->token_capacity);
	}
}

static void grow_stack(USEC_Tokenizer* t) {
	if (t->opener_stack_size >= t->opener_stack_capacity) {
		t->opener_stack_capacity = (t->opener_stack_capacity == 0) ? 8 : t->opener_stack_capacity * 2;
		t->opener_stack = realloc(t->opener_stack, sizeof(USEC_Token) * t->opener_stack_capacity);
	}
}

static char current(USEC_Tokenizer* t) {
	return (t->index >= t->length) ? '\0' : t->input[t->index];
}

static char peek(USEC_Tokenizer* t, int offset) {
	size_t p = t->index + offset;
	return (p >= t->length) ? '\0' : t->input[p];
}

static void next(USEC_Tokenizer* t) {
	if (current(t) == '\n') {
		t->line++;
		t->col = 1;
	} else {
		t->col++;
	}
	t->index++;
}

static void add_token(USEC_Tokenizer* t, USEC_TokenType type, const char* value, size_t len) {
	grow_token_array(t);

	char* copy = malloc(len + 1);
	memcpy(copy, value, len);
	copy[len] = 0;

	t->tokens[t->token_count++] = (USEC_Token){
		.type = type,
		.value = copy,
		.line = t->line,
		.col = t->col
	};

	if (t->debug) {
		printf("[Token] %d:%d %d '%s'\n", t->line, t->col, type, copy);
	}
}

static void push_opener(USEC_Tokenizer* t, USEC_Token* token) {
	grow_stack(t);
	t->opener_stack[t->opener_stack_size++] = *token;
}

static const char* opener_for_closer(char close) {
	switch (close) {
	case '}': return "{";
	case ']': return "[";
	case ')': return "$(";
	case '"': return "\"";
	case '`': return "`";
	default: return NULL;
	}
}

static void error(USEC_Tokenizer* t, const char* message) {
	fprintf(stderr, "[USEC] [%d:%d] Error: %s\n", t->line, t->col, message);
	if (t->pedantic) {
		exit(1);
	} else {
		t->has_error = true;
	}
}

static void error_t(USEC_Tokenizer* t, const char* message, USEC_Token* token) {
	fprintf(stderr, "[USEC] [%d:%d] Error: %s\n", token->line, token->col, message);
	if (t->pedantic) {
		exit(1);
	} else {
		t->has_error = true;
	}
}

static bool is_start_identifier(char ch) {
	return isalpha(ch) || ch == '_';
}

static bool is_identifier_char(char ch) {
	return isalnum(ch) || ch == '_';
}

static const char* escape_char(char ch) {
	switch (ch) {
	case 'n': return "\n";
	case 'r': return "\r";
	case 't': return "\t";
	case '"': return "\"";
	case '\'': return "\'";
	case '\\': return "\\";
	default: {
		static char fallback[2];
		fallback[0] = ch;
		fallback[1] = '\0';
		return fallback;
	}
	}
}

// READ

static void read_identifier(USEC_Tokenizer* t) {
	size_t start = t->index;
	while (is_identifier_char(current(t))) next(t);

	size_t len = t->index - start;
	const char* text = t->input + start;

	if ((len == 4 && strncmp(text, "null", 4) == 0) ||
		(len == 4 && strncmp(text, "true", 4) == 0) ||
		(len == 5 && strncmp(text, "false", 5) == 0)) {
		add_token(t, TOK_KEYWORD, text, len);
	} else {
		add_token(t, TOK_IDENTIFIER, text, len);
	}
}

static void read_number(USEC_Tokenizer* t) {
	size_t start = t->index;

	// Optional leading minus
	if (current(t) == '-') next(t);

	// Integer part
	if (current(t) == '0') {
		next(t); // Leading zero allowed ONLY if zero, no more digits
	} else if (isdigit(current(t))) {
		while (isdigit(current(t))) next(t);
	} else {
		error(t, "Invalid number: expected digit");
		return;
	}

	// Fraction part (optional)
	if (current(t) == '.') {
		next(t);
		if (!isdigit(current(t))) {
			error(t, "Invalid number: expected digit after decimal point");
			return;
		}
		while (isdigit(current(t))) next(t);
	}

	// Exponent part (optional): e or E
	if (current(t) == 'e' || current(t) == 'E') {
		next(t);
		if (current(t) == '+' || current(t) == '-') next(t);
		if (!isdigit(current(t))) {
			error(t, "Invalid number: expected digit after exponent");
			return;
		}
		while (isdigit(current(t))) next(t);
	}

	size_t len = t->index - start;
	add_token(t, TOK_NUMBER, t->input + start, len);
}

static void read_char(USEC_Tokenizer* t) {
	next(t); // skip opening quote

	char buff[2] = { 0 };
	if (current(t) == '\\') {
		next(t);
		const char* esc = escape_char(current(t));
		buff[0] = esc[0];
	} else {
		buff[0] = current(t);
	}
	next(t);

	if (current(t) != '\'') {
		error(t, "Expected closing single quote");
		return;
	}
	next(t); // skip closing quote
	add_token(t, TOK_CHAR, buff, 1);
}

static void read_comment(USEC_Tokenizer* t) {
	const size_t start = t->index + 1;
	while (current(t) && current(t) != '\n') next(t);
	size_t len = t->index - start;
	//add_token(t, TOK_COMMENT, t->input + start, len); // comments are simply ignored
}

static void read_multiline_comment(USEC_Tokenizer* t) {
	size_t start = t->index;
	next(t); // %
	next(t); // %

	while (current(t) && !(current(t) == '%' && peek(t, 1) == '%')) {
		next(t);
	}

	size_t len = t->index - start;
	//add_token(t, TOK_COMMENT, t->input + start, len); // comments are simply ignored

	if (current(t) == '%' && peek(t, 1) == '%') {
		next(t); next(t); // skip closing %%
	}
}

static void read_interpolation(USEC_Tokenizer* t) {
	int start_col = t->col;
	next(t); // skip $
	next(t); // skip (

	if (!is_start_identifier(current(t))) {
		error(t, "Invalid interpolation character (expected identifier)");
		return;
	}

	size_t start = t->index;
	while (is_identifier_char(current(t))) next(t);

	size_t len = t->index - start;
	if (current(t) != ')') {
		error(t, "Unclosed interpolation");
		return;
	}

	add_token(t, TOK_IDENTIFIER, t->input + start, len);
	next(t); // skip closing ')'
}

static void read_string(USEC_Tokenizer* t) {
	// Push string opener
	add_token(t, TOK_STRING_START, "\"", 1);
	next(t); // skip opening "

	size_t start_col = t->col;
	int start_line = t->line;
	StringBuilder sb = sb_create();

	while (!current(t) == '\0') {
		char ch = current(t);

		if (ch == '"') {
			if (sb.length > 0) {
				add_token(t, TOK_STRING, sb.buffer, sb.length);
				sb.buffer = NULL;
			}
			add_token(t, TOK_STRING_END, "\"", 1);
			next(t);
			break;
		} else if (ch == '$' && peek(t, 1) == '(') {
			if (sb.length > 0) {
				add_token(t, TOK_STRING, sb.buffer, sb.length);
				sb.buffer = NULL;
			}
			read_interpolation(t);
		} else if (ch == '\\') {
			next(t);
			sb_append_str(&sb, escape_char(current(t)));
		} else if (ch == '\n') {
			if (sb.length > 0) {
				add_token(t, TOK_STRING, sb.buffer, sb.length);
				sb.buffer = NULL;
			}
			error(t, "Unclosed string");
			next(t);
			break;
		} else {
			sb_append_char(&sb, ch);
		}

		next(t);
	}

	sb_free(&sb);
}

static void read_multiline_string(USEC_Tokenizer* t) {
	add_token(t, TOK_STRING_START, "`", 1);
	next(t); // skip `

	size_t start_col = t->col;
	int start_line = t->line;
	StringBuilder sb = sb_create();

	// Skip first newline if exists right after opening `
	if (current(t) == '\n') next(t);

	while (!current(t) == '\0') {
		char ch = current(t);
		char pk = peek(t, 1);

		if (ch == '`') {
			if (sb.length > 0) {
				add_token(t, TOK_STRING, sb.buffer, sb.length);
				sb.buffer = NULL;
			}
			add_token(t, TOK_STRING_END, "`", 1);
			next(t);
			break;
		} else if (ch == '$' && pk == '(') {
			if (sb.length > 0) {
				add_token(t, TOK_STRING, sb.buffer, sb.length);
				sb.buffer = NULL;
			}
			read_interpolation(t);
		} else if (ch == '\n' && pk == '`') {
			next(t); // skip newline before closing backtick
			continue;
		} else if (ch == '\\') {
			next(t);
			sb_append_str(&sb, escape_char(current(t)));
		} else {
			sb_append_char(&sb, ch);
		}

		next(t);
	}

	sb_free(&sb);
}

static void read_statement(USEC_Tokenizer* t) {
	char ch = current(t);
	char pk = peek(t, 1);

	USEC_Token* last = t->token_count > 0 ? &t->tokens[t->token_count - 1] : NULL;
	USEC_Token* lo = t->opener_stack_size > 0 ? &t->opener_stack[t->opener_stack_size - 1] : NULL;

	// Identifiers
	if (is_start_identifier(ch)) {
		read_identifier(t);
	}

	// Comments
	else if (ch == '#') {
		if (t->compact) error(t, "Comments are not allowed in compact mode");
		read_comment(t);
	} else if (ch == '%' && pk == '%') {
		if (t->compact) error(t, "Comments are not allowed in compact mode");
		read_multiline_comment(t);
	}

	// Operators
	else if (ch == '!') {
		add_token(t, TOK_EXCLAMATION, "!", 1);
		next(t);
	} else if (ch == ':') {
		add_token(t, TOK_COLON, ":", 1);
		next(t);
	} else if (ch == '=') {
		add_token(t, TOK_EQUALS, "=", 1);
		next(t);
	}

	// Closers
	else if (ch == '[') {
		add_token(t, TOK_ARRAY_OPEN, "[", 1);
		USEC_Token o = t->tokens[t->token_count - 1];
		push_opener(t, &o);
		next(t);
	} else if (ch == ']') {
		add_token(t, TOK_ARRAY_CLOSE, "]", 1);
		if (t->opener_stack_size > 0 &&
			strcmp(t->opener_stack[t->opener_stack_size - 1].value, "[") == 0) {
			t->opener_stack_size--;
		} else {
			error(t, "Unopened closer ']'");
		}
		next(t);
	}

	else if (ch == '{') {
		add_token(t, TOK_BRACE_OPEN, "{", 1);
		USEC_Token o = t->tokens[t->token_count - 1];
		push_opener(t, &o);
		next(t);
	} else if (ch == '}') {
		add_token(t, TOK_BRACE_CLOSE, "}", 1);
		if (t->opener_stack_size > 0 &&
			strcmp(t->opener_stack[t->opener_stack_size - 1].value, "{") == 0) {
			t->opener_stack_size--;
		} else {
			error(t, "Unopened closer '}'");
		}
		next(t);
	}

	// Characters
	else if (ch == '\'') {
		USEC_Token tok = read_char(t);
		add_token(t, &tok);
	}

	// Quoted string
	else if (ch == '"') {
		read_string(t);
	}

	// Multiline string
	else if (ch == '`') {
		read_multiline_string(t);
	}

	// Numbers
	else if (isdigit(ch) || ch == '-') {
		read_number(t);
	}

	// Space
	else if (ch == ' ') {
		if (last && last->type != TOK_SPACE && last->type != TOK_NEWLINE)
			add_token(t, TOK_SPACE, " ", 1);
		else if (t->compact) error(t, "Unnecessary space");
		next(t);
	}

	// Commas treated as newlines
	else if (ch == ',') {
		if (!t->compact &&
			pk != '\0' && pk != ' ' && pk != '\n' && pk != '\r') {
			error(t, "Missing whitespace after comma");
		}
		if (!last || last->type == TOK_SPACE || last->type == TOK_NEWLINE)
			error(t, "Invalid comma");

		add_token(t, TOK_NEWLINE, ",", 1);
		next(t);
	}

	// Line endings
	else if (ch == '\n') {
		if (last && last->type == TOK_SPACE) {
			if (t->compact) error(t, "Unnecessary space");
			// replace space with newline
			t->tokens[t->token_count - 1].type = TOK_NEWLINE;
			t->tokens[t->token_count - 1].value[0] = '\n';
		} else if (last && last->type == TOK_NEWLINE) {
			if (t->compact) error(t, "Unnecessary newline");
		} else {
			add_token(t, TOK_NEWLINE, "\n", 1);
		}
		next(t);
	} else if (ch == '\r' && pk == '\n') {
		add_token(t, TOK_NEWLINE, "\r\n", 2);
		next(t);
		next(t);
	}

	// Unknown characters
	else {
		char msg[64];
		snprintf(msg, sizeof(msg), "Unexpected character '%c'", ch);
		error(t, msg);
		next(t);
	}
}

static void read_statement(USEC_Tokenizer* t) {
	char ch = current(t);
	char pk = peek(t, 1);
	USEC_Token* last = t->token_count > 0 ? &t->tokens[t->token_count - 1] : NULL;

	// Multiline comment %%
	if (ch == '%' && pk == '%') {
		if (t->compact) error(t, "Comments not allowed in compact mode");
		read_multiline_comment(t);
		return;
	}

	// Line comment #
	if (ch == '#') {
		if (t->compact) error(t, "Comments not allowed in compact mode");
		read_comment(t);
		return;
	}

	// Identifier / keyword
	if (is_start_identifier(ch)) {
		read_identifier(t);
		return;
	}

	// Character literal
	if (ch == '\'') {
		USEC_Token tok = read_char(t);
		add_token(t, &tok);
		return;
	}

	// String literal
	if (ch == '"') {
		add_string(t);
		return;
	}

	// Multiline string literal
	if (ch == '`') {
		add_multiline_string(t);
		return;
	}

	// Numbers
	if (isdigit(ch) || ch == '-') {
		read_number(t);
		return;
	}

	// Symbols / operators
	switch (ch) {
	case '!': add_token(t, TOK_EXCLAMATION, "!", 1); next(t); break;
	case ':': add_token(t, TOK_COLON, ":", 1); next(t); break;
	case '=': add_token(t, TOK_EQUALS, "=", 1); next(t); break;
	case '[':
		add_token(t, TOK_ARRAY_OPEN, "[", 1);
		push_opener(t, &t->tokens[t->token_count - 1]);
		next(t);
		break;
	case ']':
		add_token(t, TOK_ARRAY_CLOSE, "]", 1);
		if (t->opener_stack_size > 0 &&
			strcmp(t->opener_stack[t->opener_stack_size - 1].value, "[") == 0) {
			t->opener_stack_size--;
		} else {
			error(t, "Unopened closer ']'");
		}
		next(t);
		break;
	case '{':
		add_token(t, TOK_BRACE_OPEN, "{", 1);
		push_opener(t, &t->tokens[t->token_count - 1]);
		next(t);
		break;
	case '}':
		add_token(t, TOK_BRACE_CLOSE, "}", 1);
		if (t->opener_stack_size > 0 &&
			strcmp(t->opener_stack[t->opener_stack_size - 1].value, "{") == 0) {
			t->opener_stack_size--;
		} else {
			error(t, "Unopened closer '}'");
		}
		next(t);
		break;
	case ' ':
		if (last && last->type != TOK_SPACE && last->type != TOK_NEWLINE) {
			add_token(t, TOK_SPACE, " ", 1);
		} else if (t->compact) {
			error(t, "Unnecessary space");
		}
		next(t);
		break;
	case ',':
		if (!t->compact && pk != '\0' && pk != ' ' && pk != '\n' && pk != '\r') {
			error(t, "Missing whitespace after comma");
		}
		if (!last || last->type == TOK_SPACE || last->type == TOK_NEWLINE)
			error(t, "Invalid comma");
		add_token(t, TOK_NEWLINE, ",", 1);
		next(t);
		break;
	case '\n':
		if (last && last->type == TOK_SPACE) {
			if (t->compact) error(t, "Unnecessary space");
			last->type = TOK_NEWLINE;
			last->value[0] = '\n';
		} else if (last && last->type == TOK_NEWLINE) {
			if (t->compact) error(t, "Unnecessary newline");
		} else {
			add_token(t, TOK_NEWLINE, "\n", 1);
		}
		next(t);
		break;
	case '\r':
		if (pk == '\n') {
			add_token(t, TOK_NEWLINE, "\r\n", 2);
			next(t); next(t);
		}
		break;
	default: {
		char msg[64];
		snprintf(msg, sizeof(msg), "Unexpected character '%c'", ch);
		error(t, msg);
		next(t);
		break;
	}
	}
}

void usec_tokenizer_tokenize(USEC_Tokenizer* t) {
	if (!t->input) return;
	t->length = strlen(t->input);

	if (current(t) == '%') {
		t->compact = true;
		next(t);
	}

	add_token(t, TOK_NEWLINE, "sof", 3);
	bool early_end = (current(t) == '\0');

	while (current(t)) {
		read_statement(t);
	}

	// Trailing space/newline cleanup
	if (!early_end && t->token_count > 0) {
		USEC_Token* last = &t->tokens[t->token_count - 1];
		if (last->type == TOK_SPACE || last->type == TOK_NEWLINE) {
			t->token_count--;
		}
	}

	add_token(t, TOK_NEWLINE, "eof", 3);

	// Unclosed openers
	if (t->opener_stack_size > 0) {
		for (size_t i = 0; i < t->opener_stack_size; ++i) {
			error_t(t, "Unclosed opener", t->opener_stack[i]);
		}
	}
}
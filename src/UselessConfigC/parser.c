#include "parser.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>

#define SCOPE_MIN_CAPACITY 4

// Helper for errors
static void parser_error(USEC_Parser* p, USEC_Token* token, const char* message) {
	fprintf(stderr, "[USEC PARSER] [%d:%d] Error: %s\n", token->line, token->col, message);
	if (p->pedantic) exit(2);
}

// Number parsing helpers
// Parses a NAN or INF-safe double
static double safe_strtod(const char* str, bool* is_valid) {
	errno = 0;
	char* endptr = NULL;
	double result = strtod(str, &endptr);
	if (is_valid) *is_valid = (errno == 0 && endptr != str && isfinite(result));
	return result;
}

// Parses 64-bit signed int safely
static bool try_parse_int64(const char* str, int64_t* out) {
	errno = 0;
	char* endptr = NULL;
	long long v = strtoll(str, &endptr, 10);
	if (errno != 0 || endptr == str) return false;
	*out = v;
	return true;
}

// Parses 64-bit unsigned int safely
static bool try_parse_uint64(const char* str, uint64_t* out) {
	errno = 0;
	char* endptr = NULL;
	unsigned long long v = strtoull(str, &endptr, 10);
	if (errno != 0 || endptr == str) return false;
	*out = v;
	return true;
}

// String builder helpers
static bool sb_append_value_repr(SB* sb, USEC_Value* val) {
	if (!val) return false;
	char buf[64];
	switch (val->type) {
	case VALUE_STRING: sb_append_str(sb, val->stringValue); break;
	case VALUE_INT: snprintf(buf, sizeof(buf), "%" PRId64, val->int64Value); sb_append_str(sb, buf); break;
	case VALUE_UINT: snprintf(buf, sizeof(buf), "%" PRIu64, val->uint64Value); sb_append_str(sb, buf); break;
	case VALUE_DOUBLE: snprintf(buf, sizeof(buf), "%f", val->doubleValue); sb_append_str(sb, buf); break;
	case VALUE_BOOL: sb_append_str(sb, val->boolValue ? "true" : "false"); break;
	case VALUE_NULL: sb_append_str(sb, "null"); break;
	default: return false;
	}

	return true;
}

// Helper for variable scopes
static void scope_push(USEC_Parser* p, Usec_Hashtable* vars) {
	if (p->var_stack_size >= USEC_VAR_STACK_MAX) {
		fprintf(stderr, "Exceeded variable scope stack\n");
		exit(3);
	}
	p->var_stack[p->var_stack_size++] = vars;
}

static void scope_pop(USEC_Parser* p) {
	if (p->var_stack_size > 0) {
		p->var_stack_size--;
	}
}

static const char* get_variable(USEC_Parser* p, USEC_Token* tok) {
	const char* name = tok->value;
	const char* result = NULL;

	// Search stack from top (n-1) to index 1 for local scopes
	if (p->var_stack_size > 1) {
		for (ssize_t i = (ssize_t)p->var_stack_size - 1; i >= 1; --i) {
			result = usec_ht_get(p->var_stack[i], name);
			if (result) return result;
		}
	}

	// Fallback to global (index 0)
	result = usec_ht_get(p->var_stack[0], name);

	if (!result) {
		parser_error(p, tok, "Undefined variable");
		return NULL;
	}

	return result;
}

static void define_variable(USEC_Parser* p, const char* name, const char* value) {
	if (p->var_stack_size > 0) {
		usec_ht_set(p->var_stack[p->var_stack_size - 1], name, value);
	}
}

// Current token
static USEC_Token* current(USEC_Parser* p) {
	return &p->tokens[p->index];
}

static USEC_Token* peek(USEC_Parser* p) {
	if (p->index + 1 >= p->token_count) return NULL;
	return &p->tokens[p->index + 1];
}

static bool eof(USEC_Parser* p) {
	return p->index >= p->token_count;
}

static USEC_Token* next(USEC_Parser* p) {
	if (!eof(p)) return &p->tokens[++p->index];
	return NULL;
}

static bool check(USEC_Parser* p, USEC_TokenType type) {
	return !eof(p) && p->tokens[p->index].type == type;
}

static bool optional(USEC_Parser* p, USEC_TokenType type) {
	if (check(p, type)) {
		next(p);
		return true;
	}
	return false;
}

static bool expect(USEC_Parser* p, USEC_TokenType type) {
	if (!check(p, type)) {
		parser_error(p, current(p), "Unexpected token");
		return false;
	}
	next(p);
	return true;
}

static bool cons_ret(USEC_TokenType type) {
	if (expect(type)) next();
	else if (check(TOK_NEWLINE)) return true;
	return false;
}

// === Value Construction ===

static USEC_Value* make_value(USEC_ValueType type) {
	USEC_Value* val = calloc(1, sizeof(USEC_Value));
	val->type = type;
	return val;
}

static USEC_Value* parse_value(USEC_Parser* p);

static USEC_Value* parse_number(USEC_Parser* p) {
	const char* raw = current(p)->value;
	next(p);

	// Check if it's clearly floating point syntax
	bool is_float = strchr(raw, '.') || strchr(raw, 'e') || strchr(raw, 'E');

	// CASE: clearly a float -> parse as double
	if (is_float) {
		bool ok = false;
		double val = safe_strtod(raw, &ok);
		if (!ok) {
			parser_error(p, current(p), "Invalid floating-point number");
			return NULL;
		}
		USEC_Value* v = make_value(VALUE_DOUBLE);
		v->doubleValue = val;
		return v;
	}

	// Try parsing as signed/unsigned int (if no dot/e/E seen)
	int64_t s_val = 0;
	uint64_t u_val = 0;

	if (raw[0] == '-') {
		if (try_parse_int64(raw, &s_val)) {
			USEC_Value* v = make_value(VALUE_INT);
			v->int64Value = s_val;
			return v;
		}
	} else {
		if (try_parse_uint64(raw, &u_val)) {
			USEC_Value* v = make_value(VALUE_UINT);
			v->uint64Value = u_val;
			return v;
		}
	}

	// Fallback: can't fit into int/uint64, so parse as double
	bool ok = false;
	double fallback = safe_strtod(raw, &ok);
	if (!ok) {
		parser_error(p, current(p), "Invalid fallback double parse");
		return NULL;
	}

	USEC_Value* v = make_value(VALUE_DOUBLE);
	v->doubleValue = fallback;
	return v;
}

static USEC_Value* parse_string(USEC_Parser* p) {
	expect(p, TOK_STRING_START);

	SB builder = sb_create();

	while (!eof(p)) {
		USEC_Token* tok = current(p);

		if (tok->type == TOK_STRING) {
			// Add literal string content
			sb_append_str(&builder, tok->value);
			next(p);
		} else if (tok->type == TOK_IDENTIFIER) {
			// Variable interpolation
			if (p->keep_variables) {
				sb_append_str(&builder, "$(");
				sb_append_str(&builder, tok->value);
				sb_append_char(&builder, ')');
			} else {
				// Resolve interpolated value from scope
				USEC_Value* resolved = get_variable(p, tok->value);
				if (resolved && resolved->type == VALUE_STRING) {
					sb_append_str(&builder, resolved->stringValue);
				} else if (resolved) {
					if (!sb_append_value_repr(sb, resolved)) parser_error(p, tok, "Unsupported string interpolation");
				} else {
					parser_error(p, tok, "Undefined variable");
				}
			}
			next(p);
		} else if (tok->type == TOK_STRING_END) {
			next(p);
			break;
		} else {
			parser_error(p, tok, "Unexpected token in string");
			next(p);
		}
	}

	char* final = sb_build(&builder);
	USEC_Value* val = make_value(VALUE_STRING);
	val->stringValue = final ? final : strdup("");
	return val;
}

static USEC_Value* parse_char(USEC_Parser* p) {
	if (!check(p, TOK_CHAR))
		parser_error(p, current(p), "Expected character literal");

	USEC_Value* val = make_value(VALUE_CHAR);
	val->charValue = current(p)->value[0];
	next(p);
	return val;
}

static USEC_Value* parse_identifier(USEC_Parser* p) {
	const char* name = current(p)->value;

	if (p->keep_variables) {
		// Return string: $($name)
		SB sb = sb_create();
		sb_append_str(&sb, "$($");
		sb_append_str(&sb, name);
		sb_append_char(&sb, ')');

		USEC_Value* val = make_value(VALUE_STRING);
		val->stringValue = sb_build(&sb);
		next(p);
		return val;
	} else {
		// Lookup variable value from scope
		USEC_Value* resolved = get_variable(p, current(p));
		if (!resolved) {
			parser_error(p, current(p), "Undefined variable");
			return NULL;
		}
		next(p);

		// Use string builder to serialize any primitive into a VALUE_STRING
		SB sb = sb_create();
		if (!sb_append_value_repr(&sb, resolved)) {
			parser_error(p, current(p), "Unsupported string interpolation");
			sb_free(&sb);
			return NULL;
		}

		USEC_Value* val = make_value(VALUE_STRING);
		val->stringValue = sb_build(&sb);
		return val;
	}
}

static USEC_Value* parse_statement(USEC_Parser* p);

static void define_variable(USEC_Parser* p, const char* name, USEC_Value* val) {
	usec_ht_set(p->var_stack[p->var_stack_size - 1], name, val);
}

static USEC_Statement* parse_declaration(USEC_Parser* p, USEC_Value* obj) {
	next(p);  // consume ':'

	if (!cons_ret(p, TOK_IDENTIFIER)) return NULL;

	const char* key = current(p)->value;
	next(p);

	if (!p->compact && cons_ret(p, TOK_SPACE)) return NULL;
	if (cons_ret(p, TOK_EQUALS)) return NULL;
	if (!p->compact && cons_ret(p, TOK_SPACE)) return NULL;

	USEC_Value* val = parse_value(p);
	if (!val) return NULL;

	USEC_Statement* stmt = malloc(sizeof(USEC_Statement));
	stmt->type = STATEMENT_DECLARATION;
	stmt->key = key;
	stmt->value = value;
	return stmt;
}

static USEC_Statement* parse_assignment(USEC_Parser* p) {
	char* key = NULL;

	if (check(p, TOK_IDENTIFIER)) {
		key = strdup(current(p)->value);
		next(p);
	} else if (check(p, TOK_STRING_START)) {
		USEC_Value* sval = parse_string(p);
		if (!sval || sval->type != VALUE_STRING) {
			parser_error(p, current(p), "String key parse error");
			return NULL;
		}
		key = strdup(sval->stringValue);
		usec_parser_free_value(sval);
	} else {
		if (check(p, TOK_NEWLINE)) return NULL;
		parser_error(p, current(p), "Expected identifier or string key in assignment");
		return NULL;
	}

	if (!p->compact && cons_ret(p, TOK_SPACE)) return NULL;
	if (cons_ret(p, TOK_EQUALS)) return NULL;
	if (!p->compact && cons_ret(p, TOK_SPACE)) return NULL;

	USEC_Value* val = parse_value(p);
	if (!val) return NULL;

	USEC_Statement* stmt = malloc(sizeof(USEC_Statement));
	stmt->type = STATEMENT_ASSIGNMENT;
	stmt->key = key;
	stmt->value = val;
	return stmt;
}

static USEC_Statement* parse_statement(USEC_Parser* p) {
	if (check(p, TOK_COLON)) {
		return parse_declaration(p);
	} else {
		return parse_assignment(p);
	}
}

static USEC_Value* parse_array(USEC_Parser* p) {
	expect(p, TOK_ARRAY_OPEN);

	USEC_Value* arr = make_value(VALUE_ARRAY);
	arr->arrayValue.items = NULL;
	arr->arrayValue.count = 0;
	size_t capacity = 0;

	if (check(p, TOK_NEWLINE)) {
		if (p->compact) parser_error(p, current(p), "Unnecessary newline");
		next(p);
	}

	while (!check(p, TOK_ARRAY_CLOSE)) {
		USEC_Value* item = parse_value(p);
		if (item) {
			if (arr->arrayValue.count >= capacity) {
				capacity = capacity ? (capacity * 2) : 4;
				arr->arrayValue.items = realloc(arr->arrayValue.items, sizeof(USEC_Value*) * capacity);
			}
			arr->arrayValue.items[arr->arrayValue.count++] = item;
		}

		if (check(TOK_NEWLINE)) {
			if (peek()->type == TOK_ARRAY_CLOSE && p->compact) parser_error(p, current(p), "Unnecessary newline");
			next();
		} else expect(TOK_ARRAY_CLOSE);
	}

	// Shrink to fit
	if (arr->arrayValue.count < capacity) {
		arr->arrayValue.items = realloc(arr->arrayValue.items, sizeof(USEC_Value*) * arr->arrayValue.count);
	}

	return arr;
}

static USEC_Value* parse_object(USEC_Parser* p) {
	expect(p, TOK_BRACE_OPEN);

	USEC_Value* obj = malloc(sizeof(USEC_Value));
	obj->type = VALUE_OBJECT;
	obj->objectValue = usec_ht_create(8);

	// Local scope
	Usec_Hashtable* local = NULL;

	if (check(p, TOK_NEWLINE)) {
		if (p->compact) parser_error(p, current(p), "Unnecessary newline");
		next(p);
	}

	while (!check(p, TOK_BRACE_CLOSE)) {
		size_t line = current(p)->line;
		size_t col = current(p)->col;

		USEC_Statement* stmt = parse_statement(p);
		if (stmt && stmt->key) {
			if (stmt->type == STATEMENT_DECLARATION) {
				if (!local) {
					local = usec_ht_create(8);
					scope_push(p, local);
				}

				// Store in scope
				usec_ht_set(local, stmt->key, stmt->value);

				if (p->keep_variables) {
					char* out_key = NULL;
					asprintf(&out_key, "$%s", stmt->key);
					usec_ht_set(obj->objectValue, out_key, stmt->value);
					free(out_key);
				}
			} else if (stmt->type == STATEMENT_ASSIGNMENT) {
				// Store in object
				usec_ht_set(obj->objectValue, stmt->key, stmt->value);
			}
		}

		if (check(TOK_NEWLINE)) {
			if (peek()->type == TOK_BRACE_CLOSE && p->compact) parser_error(p, current(p), "Unnecessary newline");
			next();
		} else expect(TOK_BRACE_CLOSE);

		// cleanup
		if (stmt) free(stmt->key), free(stmt);
	}

	if (local) {
		usec_ht_free(local);
		scope_pop(p);
	}

	return obj;
}

static USEC_Value* parse_file(USEC_Parser* p) {
	expect(p, TOK_BRACE_OPEN);

	USEC_Value* obj = malloc(sizeof(USEC_Value));
	obj->type = VALUE_OBJECT;
	obj->objectValue = usec_ht_create(8);

	while (!eof(p)) {
		size_t line = current(p)->line;
		size_t col = current(p)->col;

		USEC_Statement* stmt = parse_statement(p);
		if (stmt && stmt->key) {
			if (stmt->type == STATEMENT_DECLARATION) {
				// Store in scope
				usec_ht_set(p->variables, stmt->key, stmt->value);

				if (p->keep_variables) {
					char* out_key = NULL;
					asprintf(&out_key, "$%s", stmt->key);
					usec_ht_set(obj->objectValue, out_key, stmt->value);
					free(out_key);
				}
			} else if (stmt->type == STATEMENT_ASSIGNMENT) {
				// Store in object
				usec_ht_set(obj->objectValue, stmt->key, stmt->value);
			}
		}

		if (!eof(p)) expect(p, TOK_NEWLINE);
		next(p);

		// cleanup
		if (stmt) free(stmt->key), free(stmt);
	}

	return obj;
}

static USEC_Value* parse_value(USEC_Parser* p) {
	if (eof(p)) return NULL;
	USEC_Token* tok = current(p);
	switch (tok->type) {
	case TOK_KEYWORD:
		if (strcmp(tok->value, "true") == 0) {
			next(p);
			USEC_Value* val = make_value(VALUE_BOOL);
			val->boolValue = true;
			return val;
		} else if (strcmp(tok->value, "false") == 0) {
			next(p);
			USEC_Value* val = make_value(VALUE_BOOL);
			val->boolValue = false;
			return val;
		} else if (strcmp(tok->value, "null") == 0) {
			next(p);
			return make_value(VALUE_NULL);
		}
		break;

	case TOK_NUMBER:
		return parse_number(p);
	case TOK_CHAR:
		return parse_char(p);
	case TOK_STRING_START:
		return parse_string(p);

	case TOK_IDENTIFIER:
		return parse_identifier(p);

	case TOK_ARRAY_OPEN:
		return parse_array(p);

	case TOK_BRACE_OPEN:
		return parse_object(p);

	default:
		parser_error(p, tok, "Unexpected token in value");
		return NULL;
	}
	return NULL;
}

// === Entry point ===
USEC_Value* usec_parser_parse(USEC_Parser* p) {
	if (check(p, TOK_EXCLAMATION)) {
		next(p);
		return parse_value(p);
	}
	return parse_object(p);
}

void usec_parser_init(USEC_Parser* p, USEC_Token* tokens, size_t token_count, Usec_Hashtable* variables) {
	p->tokens = tokens;
	p->token_count = token_count;
	p->index = 1;
	p->pedantic = true;
	p->compact = false;
	p->keep_variables = false;
	p->debug = false;

	p->variables = variables ? variables : usec_ht_create(SCOPE_MIN_CAPACITY);
	p->var_stack_size = 0;
	scope_push(p, p->variables); // push global scope
}

// === Cleanup ===

void usec_parser_free_value(USEC_Value* val) {
	if (!val) return;
	switch (val->type) {
	case VALUE_STRING: free(val->stringValue); break;
	case VALUE_ARRAY:
		for (size_t i = 0; i < val->arrayValue.count; ++i)
			usec_parser_free_value(val->arrayValue.items[i]);
		free(val->arrayValue.items);
		break;
	case VALUE_OBJECT:
		usec_ht_free(val->objectValue);
		break;
	case VALUE_FORMAT: {
		USEC_FormatNode* fmt = val->formatNode;
		usec_parser_free_value(fmt->node);
		for (size_t i = 0; i < fmt->before_count; ++i)
			usec_parser_free_value(fmt->before[i]);
		for (size_t i = 0; i < fmt->after_count; ++i)
			usec_parser_free_value(fmt->after[i]);
		free(fmt->before);
		free(fmt->after);
		free(fmt);
		break;
	}
	default: break;
	}
	free(val);
}

void usec_parser_free(USEC_Parser* p) {
	usec_ht_free(p->variables);
}
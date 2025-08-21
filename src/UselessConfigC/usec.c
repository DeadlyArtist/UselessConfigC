#include "usec.h"
#include "parser.h"
#include "tokenizer.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>


// Helpers
static void append_escaped_string(SB* sb, const char* raw) {
	sb_append_char(sb, '"');
	for (const char* p = raw; *p; ++p) {
		switch (*p) {
		case '\\': sb_append_str(sb, "\\\\"); break;
		case '"':  sb_append_str(sb, "\\\""); break;
		case '\n': sb_append_str(sb, "\\n"); break;
		case '\t': sb_append_str(sb, "\\t"); break;
		case '\r': sb_append_str(sb, "\\r"); break;
		default:
			sb_append_char(sb, *p);
			break;
		}
	}
	sb_append_char(sb, '"');
}

static void indent_level(SB* sb, int level) {
	for (int i = 0; i < level; ++i) {
		sb_append_str(sb, "  ");
	}
}

USEC_Value* usec_create_format(USEC_Value* main_node, USEC_Value** before, size_t before_count, USEC_Value** after, size_t after_count) {
	USEC_Value* val = calloc(1, sizeof(USEC_Value));
	val->type = VALUE_FORMAT;

	USEC_FormatNode* fmt = malloc(sizeof(USEC_FormatNode));
	fmt->node = main_node;

	fmt->before_count = before_count;
	fmt->before = malloc(sizeof(USEC_Value*) * before_count);
	for (size_t i = 0; i < before_count; ++i) fmt->before[i] = before[i];

	fmt->after_count = after_count;
	fmt->after = malloc(sizeof(USEC_Value*) * after_count);
	for (size_t i = 0; i < after_count; ++i) fmt->after[i] = after[i];

	val->formatNode = fmt;
	return val;
}

USEC_Value* usec_create_comment(const char* text) {
	if (!text) return NULL;
	USEC_Value* val = calloc(1, sizeof(USEC_Value));
	val->type = VALUE_COMMENT;
	val->commentText = strdup(text);
	return val;
}

USEC_Value* usec_create_multiline(const char* text) {
	if (!text) return NULL;
	USEC_Value* val = calloc(1, sizeof(USEC_Value));
	val->type = VALUE_MULTILINE_COMMENT;
	val->commentText = strdup(text);
	return val;
}

USEC_Value* usec_create_newline(int count) {
	if (count <= 0) return NULL;
	USEC_Value* val = calloc(1, sizeof(USEC_Value));
	val->type = VALUE_NEWLINE;
	val->newline_count = count;
	return val;
}

// ==============================
//        Public Functions
// ==============================

USEC_ParseOptions usec_get_default_parse_options(void) {
	USEC_ParseOptions opts;
	opts.pedantic = true;
	opts.keepVariables = false;
	opts.debugTokens = false;
	opts.debugParser = false;
	opts.variable_keys = NULL;
	opts.variable_values = NULL;
	opts.variable_count = 0;
	return opts;
}

USEC_Value* usec_clone_value(const USEC_Value* val) {
	if (!val) return NULL;

	USEC_Value* out = malloc(sizeof(USEC_Value));
	out->type = val->type;

	switch (val->type) {
	case VALUE_STRING:
		out->stringValue = strdup(val->stringValue);
		break;

	case VALUE_BOOL:
		out->boolValue = val->boolValue;
		break;

	case VALUE_INT:
		out->int64Value = val->int64Value;
		break;

	case VALUE_UINT:
		out->uint64Value = val->uint64Value;
		break;

	case VALUE_DOUBLE:
		out->doubleValue = val->doubleValue;
		break;

	case VALUE_CHAR:
		out->charValue = val->charValue;
		break;

	case VALUE_NULL:
		// nothing to copy
		break;

	case VALUE_ARRAY: {
		out->arrayValue.count = val->arrayValue.count;
		if (val->arrayValue.count == 0) {
			out->arrayValue.items = NULL;
			break;
		}
		out->arrayValue.items = malloc(sizeof(USEC_Value*) * val->arrayValue.count);
		for (size_t i = 0; i < val->arrayValue.count; ++i) {
			out->arrayValue.items[i] = usec_clone_value(val->arrayValue.items[i]);
		}
		break;
	}

	case VALUE_OBJECT: {
		out->objectValue = usec_ht_create(val->objectValue->capacity);
		for (size_t i = 0; i < val->objectValue->capacity; ++i) {
			Usec_HashNode* cur = val->objectValue->buckets[i];
			while (cur) {
				usec_ht_set(out->objectValue, cur->key, usec_clone_value(cur->value));
				cur = cur->next;
			}
		}
		break;
	}

	// Formatting
	case VALUE_COMMENT:
	case VALUE_MULTILINE_COMMENT:
		out->commentText = strdup(val->commentText);
		break;
	case VALUE_NEWLINE:
		out->newline_count = val->newline_count;
		break;
	case VALUE_FORMAT: {
		USEC_FormatNode* out_fmt = malloc(sizeof(USEC_FormatNode));
		out_fmt->node = usec_clone_value(val->formatNode->node);

		out_fmt->before_count = val->formatNode->before_count;
		out_fmt->before = malloc(sizeof(USEC_Value*) * out_fmt->before_count);
		for (size_t i = 0; i < out_fmt->before_count; ++i)
			out_fmt->before[i] = usec_clone_value(val->formatNode->before[i]);

		out_fmt->after_count = val->formatNode->after_count;
		out_fmt->after = malloc(sizeof(USEC_Value*) * out_fmt->after_count);
		for (size_t i = 0; i < out_fmt->after_count; ++i)
			out_fmt->after[i] = usec_clone_value(val->formatNode->after[i]);

		out->formatNode = out_fmt;
		break;
	}
	}

	return out;
}

USEC_Value* usec_parse(const char* input, const USEC_ParseOptions* options) {
	if (!input) return NULL;

	if (!options) options = usec_get_default_parse_options();

	// Tokenize
	USEC_Tokenizer tokenizer;
	usec_tokenizer_init(&tokenizer, input, false, options->pedantic, options->debugTokens);
	usec_tokenizer_tokenize(&tokenizer);

	if (tokenizer.has_error) {
		usec_tokenizer_destroy(&tokenizer);
		return NULL;
	}

	// Parse
	USEC_Parser parser;
	usec_parser_init(&parser, tokenizer.tokens, tokenizer.token_count);
	parser.pedantic = options->pedantic;
	parser.keep_variables = options->keepVariables;
	parser.compact = tokenizer.compact;
	parser.debug = options->debugParser;

	USEC_Value* result = usec_parser_parse(&parser);

	usec_parser_free(&parser);
	usec_tokenizer_destroy(&tokenizer);

	return result;
}

void usec_free(USEC_Value* root) {
	usec_parser_free_value(root);
}

bool usec_equals(const USEC_Value* a, const USEC_Value* b) {
	if (!a || !b) return a == b;
	if (a->type != b->type) return false;

	switch (a->type) {
	case VALUE_NULL: return true;
	case VALUE_BOOL: return a->boolValue == b->boolValue;
	case VALUE_INT: return a->int64Value == b->int64Value;
	case VALUE_UINT: return a->uint64Value == b->uint64Value;
	case VALUE_DOUBLE: return a->doubleValue == b->doubleValue;
	case VALUE_CHAR: return a->charValue == b->charValue;

	case VALUE_STRING:
		return strcmp(a->stringValue, b->stringValue) == 0;

	case VALUE_ARRAY: {
		if (a->arrayValue.count != b->arrayValue.count) return false;
		for (size_t i = 0; i < a->arrayValue.count; ++i) {
			if (!usec_equals(a->arrayValue.items[i], b->arrayValue.items[i])) return false;
		}
		return true;
	}

	case VALUE_OBJECT: {
		if (a->objectValue->size != b->objectValue->size) return false;

		for (size_t i = 0; i < a->objectValue->capacity; ++i) {
			Usec_HashNode* nodeA = a->objectValue->buckets[i];
			while (nodeA) {
				USEC_Value* valB = usec_ht_get(b->objectValue, nodeA->key);
				if (!valB || !usec_equals(nodeA->value, valB)) return false;
				nodeA = nodeA->next;
			}
		}
		return true;
	}

	default:
		return false;
	}
}

// ==============================
//      Stringification
// ==============================

static void to_string_internal(const USEC_Value* val, SB* sb, bool readable, bool enable_vars, int level);

static void to_object_string(const USEC_Value* object, SB* sb, bool readable, bool enable_vars, int level) {
	if (!object || object->type != VALUE_OBJECT || !object->objectValue) {
		sb_append_str(sb, "{}");
		return;
	}

	Usec_Hashtable* ht = object->objectValue;
	size_t count = 0;

	sb_append_char(sb, '{');
	if (readable && ht->size > 0) sb_append_char(sb, '\n');

	for (size_t i = 0; i < ht->capacity; ++i) {
		for (Usec_HashNode* node = ht->buckets[i]; node; node = node->next) {
			if (count++ > 0) {
				if (readable) sb_append_char(sb, '\n');
				else sb_append_char(sb, ',');
			}

			if (readable) indent_level(sb, level + 1);

			const char* key = node->key;
			if (enable_vars && key[0] == '$') {
				sb_append_char(sb, ':');
				sb_append_str(sb, key + 1);
			} else {
				append_escaped_string(sb, key);
			}

			sb_append_str(sb, readable ? " = " : "=");
			to_string_internal(node->value, sb, readable, enable_vars, level + 1);
		}
	}

	if (ht->size > 0 && readable) {
		sb_append_char(sb, '\n');
		indent_level(sb, level);
	}

	sb_append_char(sb, '}');
}

static void to_array_string(const USEC_Value* array, SB* sb, bool readable, bool enable_vars, int level) {
	if (!array || array->type != VALUE_ARRAY) {
		sb_append_str(sb, "[]");
		return;
	}

	sb_append_char(sb, '[');
	if (readable && array->arrayValue.count > 0) sb_append_char(sb, '\n');

	for (size_t i = 0; i < array->arrayValue.count; ++i) {
		if (i > 0) sb_append_str(sb, readable ? "\n" : ",");

		if (readable) indent_level(sb, level + 1);
		to_string_internal(array->arrayValue.items[i], sb, readable, enable_vars, level + 1);
	}

	if (array->arrayValue.count > 0 && readable) {
		sb_append_char(sb, '\n');
		indent_level(sb, level);
	}

	sb_append_char(sb, ']');
}

static void to_string_internal(const USEC_Value* val, SB* sb, bool readable, bool enable_vars, int level) {
	if (!val) {
		sb_append_str(sb, "null");
		return;
	}

	switch (val->type) {
	case VALUE_NULL:
		sb_append_str(sb, "null");
		break;

	case VALUE_BOOL:
		sb_append_str(sb, val->boolValue ? "true" : "false");
		break;

	case VALUE_INT: {
		char buf[32];
		snprintf(buf, sizeof(buf), "%lld", (long long)val->int64Value);
		sb_append_str(sb, buf);
		break;
	}

	case VALUE_UINT: {
		char buf[32];
		snprintf(buf, sizeof(buf), "%llu", (unsigned long long)val->uint64Value);
		sb_append_str(sb, buf);
		break;
	}

	case VALUE_DOUBLE: {
		char buf[64];
		snprintf(buf, sizeof(buf), "%g", val->doubleValue);
		sb_append_str(sb, buf);
		break;
	}

	case VALUE_CHAR: {
		char buf[4] = { '\'', val->charValue, '\'', '\0' };
		sb_append_str(sb, buf);
		break;
	}

	case VALUE_STRING:
		if (enable_vars && strncmp(val->stringValue, "$($", 3) == 0) {
			size_t len = strlen(val->stringValue);
			if (len > 4 && val->stringValue[len - 1] == ')') {
				sb_append_str(sb, val->stringValue + 3);
				sb->length--;  // remove trailing ')'
				sb->buffer[sb->length] = '\0';
				break;
			}
		}
		append_escaped_string(sb, val->stringValue);
		break;

	case VALUE_ARRAY:
		to_array_string(val, sb, readable, enable_vars, level);
		break;

	case VALUE_OBJECT:
		to_object_string(val, sb, readable, enable_vars, level);
		break;
	}

	// Formatting
	case VALUE_COMMENT:
		if (readable) {
			sb_append_str(sb, "# ");
			sb_append_str(sb, val->commentText);
		}
		break;

	case VALUE_MULTILINE_COMMENT:
		if (readable) {
			sb_append_str(sb, "%%\n");
			sb_append_str(sb, val->commentText);
			sb_append_str(sb, "\n%%");
		}
		break;

	case VALUE_NEWLINE:
		if (readable) {
			for (int i = 0; i < val->newline_count; ++i)
				sb_append_char(sb, '\n');
		}
		break;
	case VALUE_FORMAT: {
		if (readable && val->formatNode) {
			for (size_t i = 0; i < val->formatNode->before_count; ++i) {
				if (val->formatNode->before[i]->type != VALUE_NEWLINE) indent_level(sb, level);
				to_string_internal(val->formatNode->before[i], sb, readable, enable_vars, level);
				sb_append_char(sb, '\n');
			}

			to_string_internal(val->formatNode->node, sb, readable, enable_vars, level);

			for (size_t i = 0; i < val->formatNode->after_count; ++i) {
				if (val->formatNode->after[i]->type != VALUE_NEWLINE) indent_level(sb, level);
				sb_append_char(sb, '\n');
				to_string_internal(val->formatNode->after[i], sb, readable, enable_vars, level);
			}
		} else if (val->formatNode) {
			to_string_internal(val->formatNode->node, sb, readable, enable_vars, level);
		}
		break;
	}
}

char* usec_to_string(const USEC_Value* root, bool readable, bool enable_variables) {
	if (!root) {
		return strdup(readable ? "!" : "%!");
	}

	SB sb = sb_create();

	if (!readable) {
		sb_append_char(&sb, '%');
	}
	if (root->type != VALUE_OBJECT) {
		sb_append_char(&sb, '!');
	}

	to_string_internal(root, &sb, readable, enable_variables, 0);

	return sb_build(&sb);
}
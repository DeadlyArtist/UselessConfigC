#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define SB_INITIAL_CAPACITY 64

// Portable asprintf fallback
int asprintf(char** str, const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);

	// First, get the size needed
	int size = vsnprintf(NULL, 0, fmt, args);
	va_end(args);

	if (size < 0) {
		*str = NULL;
		return -1;
	}

	// Allocate memory (+1 for null terminator)
	*str = (char*)malloc((size_t)size + 1);
	if (!*str) return -1;

	// Now actually do the formatting
	va_start(args, fmt);
	int written = vsnprintf(*str, (size_t)size + 1, fmt, args);
	va_end(args);

	if (written < 0) {
		free(*str);
		*str = NULL;
	}

	return written;
}

// Stringbuilder

SB sb_create(void) {
	SB sb;
	sb.length = 0;
	sb.capacity = SB_INITIAL_CAPACITY;
	sb.buffer = (char*)malloc(sb.capacity);
	if (sb.buffer) sb.buffer[0] = '\0';
	return sb;
}

void sb_append_char(SB* sb, char ch) {
	if (!sb->buffer) return;
	if (sb->length + 2 > sb->capacity) {
		sb->capacity *= 2;
		sb->buffer = (char*)realloc(sb->buffer, sb->capacity);
	}
	sb->buffer[sb->length++] = ch;
	sb->buffer[sb->length] = '\0';
}

void sb_append_str(SB* sb, const char* str) {
	size_t len = strlen(str);
	sb_append_data(sb, str, len);
}

void sb_append_data(SB* sb, const char* data, size_t len) {
	if (!sb->buffer || !data) return;

	if (sb->length + len + 1 > sb->capacity) {
		while (sb->length + len + 1 > sb->capacity) {
			sb->capacity *= 2;
		}
		sb->buffer = (char*)realloc(sb->buffer, sb->capacity);
	}
	memcpy(sb->buffer + sb->length, data, len);
	sb->length += len;
	sb->buffer[sb->length] = '\0';
}

char* sb_build(SB* sb) {
	if (!sb->buffer) return NULL;
	// Add a null-terminated copy
	char* copy = (char*)malloc(sb->length + 1);
	if (copy) memcpy(copy, sb->buffer, sb->length + 1);
	sb_free(sb);  // Optional: auto-clears here
	return copy;
}

void sb_clear(SB* sb) {
	if (sb->buffer) {
		free(sb->buffer);
		sb->buffer = NULL;
	}
	sb->length = 0;
	sb->capacity = 0;
}

void sb_free(SB* sb) {
	if (sb->buffer) {
		free(sb->buffer);
		sb->buffer = NULL;
	}
	sb->length = 0;
	sb->capacity = 0;
}
#ifndef USEC_UTILS_H
#define USEC_UTILS_H

#include <stddef.h>  // for size_t

#ifdef __cplusplus
extern "C" {
#endif

	// Portable asprintf fallback
	int asprintf(char** str, const char* fmt, ...);

	// ======================
	// Dynamic String Builder
	// ======================

	typedef struct {
		char* buffer;     // dynamically allocated buffer
		size_t length;    // current length (excluding null)
		size_t capacity;  // total allocated size
	} SB;

	// Create and initialize a new string builder
	SB sb_create(void);
	void sb_init(SB* sb);

	// Append a single char (e.g., 'a')
	void sb_append_char(SB* sb, char ch);

	// Append null-terminated string (e.g., "hello")
	void sb_append_str(SB* sb, const char* str);

	// Append arbitrary bytes (e.g., binary strings)
	void sb_append_data(SB* sb, const char* data, size_t len);

	// Reset the builder (reinit)
	void sb_clear(SB* sb);

	// Finalize and get the underlying string
	// Returns malloc'd string caller must free
	char* sb_build(SB* sb);

	// Free memory and reset
	void sb_free(SB* sb);

#ifdef __cplusplus
}
#endif

#endif // USEC_UTILS_H
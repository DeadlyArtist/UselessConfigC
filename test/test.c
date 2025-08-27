#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <usec/usec.h>

char* read_file_to_string(const char* path) {
	FILE* fp = fopen(path, "rb");
	if (!fp) {
		perror("File open failed");
		return NULL;
	}

	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	rewind(fp);

	char* buffer = malloc(size + 1);
	if (!buffer) {
		fclose(fp);
		return NULL;
	}

	fread(buffer, 1, size, fp);
	buffer[size] = '\0';
	fclose(fp);
	return buffer;
}

int main(int argc, char** argv) {
	const char* filename = "test.usec";  // Default file
	if (argc > 1) filename = argv[1];

	printf("USEC test starting...\n");
	printf("Opening file: %s\n", filename);

	char* input = read_file_to_string(filename);
	if (!input) {
		fprintf(stderr, "Could not read file.\n");
		return 1;
	}

	printf("Parsing file...\n");
	USEC_ParseOptions options = usec_get_default_parse_options();
	//options.debugTokens = true;
	//options.debugParser = true;
	USEC_Value* val = usec_parse(input, &options);
	free(input);
	printf("Finished parsing\n\n");

	if (val) {
		char* repr = usec_to_string(val, NULL);
		printf("Parsed content:\n%s\n", repr);
		free(repr);
		usec_free(val);
		return 0;
	} else {
		fprintf(stderr, "Parse error!\n");
		return 1;
	}
}
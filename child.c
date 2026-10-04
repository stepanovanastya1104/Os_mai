#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>

bool is_vowel(char c) {
	return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' ||
		   c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U';
}

int main() {
	char buf[4096];
	char out[4096];
	ssize_t bytes;
	while ((bytes = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
		int len = 0;
		for (int i = 0; i < bytes; ++i) {
			if (!is_vowel(buf[i])) {
				out[len++] = buf[i];
			}
		}
		int written = write(STDOUT_FILENO, out, len);
		if (written != len) {
			const char msg[] = "error: failed to write to file\n";
			write(STDERR_FILENO, msg, sizeof(msg) - 1);
			exit(EXIT_FAILURE);
		}
	}
	if (bytes < 0) {
		const char msg[] = "error: failed to read from stdin\n";
		write(STDERR_FILENO, msg, sizeof(msg) - 1);
		exit(EXIT_FAILURE);
	}
}
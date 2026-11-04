#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>

#define SHM_SIZE 4096

static void print_err(const char *msg) {
	size_t n = 0;
	while (msg[n])
		++n;
	ssize_t w = write(STDERR_FILENO, msg, n);
	(void)w;
}

static bool is_vowel(char c) {
	return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' ||
	       c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U';
}

int main(int argc, char **argv) {
	if (argc != 4) {
		print_err("usage: client shm_name ready_sem empty_sem\n");
		_exit(1);
	}
	int shm = shm_open(argv[1], O_RDWR, 0);
	if (shm == -1) {
		print_err("error: failed to open SHM\n");
		_exit(1);
	}
	char *shm_buf = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm, 0);
	if (shm_buf == MAP_FAILED) {
		print_err("error: failed to map SHM\n");
		_exit(1);
	}
	sem_t *ready = sem_open(argv[2], O_RDWR);
	sem_t *empty = sem_open(argv[3], O_RDWR);
	if (ready == SEM_FAILED || empty == SEM_FAILED) {
		print_err("error: failed to open semaphore\n");
		_exit(1);
	}
	bool running = true;
	while (running) {
		if (sem_wait(ready) == -1) {
			print_err("error: sem_wait failed\n");
			_exit(1);
		}
		uint32_t *length = (uint32_t *)shm_buf;
		char *text = shm_buf + sizeof(uint32_t);
		if (*length == UINT32_MAX) {
			running = false;
		} else if (*length <= SHM_SIZE - sizeof(uint32_t)) {
			char out[SHM_SIZE];
			uint32_t o = 0;
			for (uint32_t i = 0; i < *length; ++i) {
				if (!is_vowel(text[i]))
					out[o++] = text[i];
			}
			if (o > 0 && write(STDOUT_FILENO, out, o) != (ssize_t)o) {
				print_err("error: failed to write to file\n");
				_exit(1);
			}
		}
		if (sem_post(empty) == -1) {
			print_err("error: sem_post failed\n");
			_exit(1);
		}
	}
	sem_close(ready);
	sem_close(empty);
	munmap(shm_buf, SHM_SIZE);
	close(shm);
}
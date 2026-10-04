#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>

static char CLIENT_PROGRAM_NAME[] = "child";

int read_line(char *buf, int max) {
	int i = 0;
	char c;
	while (i < max - 1 && read(STDIN_FILENO, &c, 1) > 0) {
		buf[i++] = c;
		if (c == '\n') {
			break;
		}
	}
	buf[i] = '\0';
	return i;
}

int main() {
	char progpath[1024];
	ssize_t len = readlink("/proc/self/exe", progpath, sizeof(progpath) - 1);
	if (len == -1) {
		const char msg[] = "error: failed to read full program path\n";
		write(STDERR_FILENO, msg, sizeof(msg) - 1);
		exit(EXIT_FAILURE);
	}
	while (progpath[len] != '/')
		--len;
	progpath[len] = '\0';
	char name1[256];
	char name2[256];
	int len1 = read_line(name1, sizeof(name1));
	int len2 = read_line(name2, sizeof(name2));
	if (len1 <= 1 || len2 <= 1) {
		const char msg[] = "error: need two file names\n";
		write(STDERR_FILENO, msg, sizeof(msg) - 1);
		exit(EXIT_FAILURE);
	}
	name1[len1 - 1] = '\0';
	name2[len2 - 1] = '\0';
	int file1 = open(name1, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	int file2 = open(name2, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (file1 == -1 || file2 == -1) {
		const char msg[] = "error: failed to open file\n";
		write(STDERR_FILENO, msg, sizeof(msg) - 1);
		exit(EXIT_FAILURE);
	}
	int pipe1[2];
	if (pipe(pipe1) == -1) {
		const char msg[] = "error: failed to create pipe\n";
		write(STDERR_FILENO, msg, sizeof(msg) - 1);
		exit(EXIT_FAILURE);
	}
	int pipe2[2];
	if (pipe(pipe2) == -1) {
		const char msg[] = "error: failed to create pipe\n";
		write(STDERR_FILENO, msg, sizeof(msg) - 1);
		exit(EXIT_FAILURE);
	}
	const pid_t child1 = fork();
	if (child1 == -1) {
		const char msg[] = "error: failed to spawn new process\n";
		write(STDERR_FILENO, msg, sizeof(msg) - 1);
		exit(EXIT_FAILURE);
	}
	if (child1 == 0) {
		close(pipe1[1]);
		close(pipe2[0]);
		close(pipe2[1]);
		close(file2);
		dup2(pipe1[0], STDIN_FILENO);
		close(pipe1[0]);
		dup2(file1, STDOUT_FILENO);
		close(file1);
		char path[1024];
		snprintf(path, sizeof(path) - 1, "%s/%s", progpath, CLIENT_PROGRAM_NAME);
		char *const args[] = {CLIENT_PROGRAM_NAME, NULL};
		execv(path, args);
		const char msg[] = "error: failed to exec into new exectuable image\n";
		write(STDERR_FILENO, msg, sizeof(msg) - 1);
		exit(EXIT_FAILURE);
	}
	const pid_t child2 = fork();
	if (child2 == -1) {
		const char msg[] = "error: failed to spawn new process\n";
		write(STDERR_FILENO, msg, sizeof(msg) - 1);
		exit(EXIT_FAILURE);
	}
	if (child2 == 0) {
		close(pipe2[1]);
		close(pipe1[0]);
		close(pipe1[1]);
		close(file1);
		dup2(pipe2[0], STDIN_FILENO);
		close(pipe2[0]);
		dup2(file2, STDOUT_FILENO);
		close(file2);
		char path[1024];
		snprintf(path, sizeof(path) - 1, "%s/%s", progpath, CLIENT_PROGRAM_NAME);
		char *const args[] = {CLIENT_PROGRAM_NAME, NULL};
		execv(path, args);
		const char msg[] = "error: failed to exec into new exectuable image\n";
		write(STDERR_FILENO, msg, sizeof(msg) - 1);
		exit(EXIT_FAILURE);
	}
	close(pipe1[0]);
	close(pipe2[0]);
	close(file1);
	close(file2);
	int rnd = open("/dev/urandom", O_RDONLY);
	char buf[4096];
	int bytes;
	while ((bytes = read_line(buf, sizeof(buf))) > 0) {
		unsigned char r;
		read(rnd, &r, 1);
		if (r % 100 < 80) {
			write(pipe1[1], buf, bytes);
		} else {
			write(pipe2[1], buf, bytes);
		}
	}
	close(rnd);
	close(pipe1[1]);
	close(pipe2[1]);
	wait(NULL);
	wait(NULL);
}
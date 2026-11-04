#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <stdio.h>

#define SHM_SIZE 4096
#define LINE_MAX_LEN 4092

static char CHILD_PROGRAM_NAME[] = "client";

typedef struct {
	char shm_name[64];
	char ready_name[64];
	char empty_name[64];
	int shm;
	char *buf;
	sem_t *ready;
	sem_t *empty;
	pid_t pid;
} channel;

static channel ch[2];
static int created = 0;

static void print_err(const char *msg) {
	size_t n = 0;
	while (msg[n])
		++n;
	ssize_t w = write(STDERR_FILENO, msg, n);
	(void)w;
}

static void cleanup(void) {
	for (int i = 0; i < created; ++i) {
		sem_unlink(ch[i].ready_name);
		sem_unlink(ch[i].empty_name);
		shm_unlink(ch[i].shm_name);
	}
}

static void die(const char *msg) {
	print_err(msg);
	cleanup();
	_exit(1);
}

static int read_line(char *buf, int max) {
	int i = 0;
	char c;
	ssize_t r;
	while (i < max - 1 && (r = read(STDIN_FILENO, &c, 1)) > 0) {
		buf[i++] = c;
		if (c == '\n')
			break;
	}
	if (i == 0 && r < 0)
		die("error: failed to read from stdin\n");
	buf[i] = '\0';
	return i;
}

static void wait_sem(sem_t *sem) {
	if (sem_wait(sem) == -1)
		die("error: sem_wait failed\n");
}

static void post_sem(sem_t *sem) {
	if (sem_post(sem) == -1)
		die("error: sem_post failed\n");
}

static void create_channel(channel *c, int index, int parent_pid) {
	snprintf(c->shm_name, sizeof(c->shm_name), "/lab3-shm%d-%d", index, parent_pid);
	snprintf(c->ready_name, sizeof(c->ready_name), "/lab3-ready%d-%d", index, parent_pid);
	snprintf(c->empty_name, sizeof(c->empty_name), "/lab3-empty%d-%d", index, parent_pid);
	created = index + 1;
	c->shm = shm_open(c->shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
	if (c->shm == -1)
		die("error: failed to create SHM\n");
	if (ftruncate(c->shm, SHM_SIZE) == -1)
		die("error: failed to resize SHM\n");
	c->buf = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, c->shm, 0);
	if (c->buf == MAP_FAILED)
		die("error: failed to map SHM\n");
	c->ready = sem_open(c->ready_name, O_RDWR | O_CREAT | O_EXCL, 0600, 0);
	if (c->ready == SEM_FAILED)
		die("error: failed to create semaphore\n");
	c->empty = sem_open(c->empty_name, O_RDWR | O_CREAT | O_EXCL, 0600, 1);
	if (c->empty == SEM_FAILED)
		die("error: failed to create semaphore\n");
}

static void send_line(channel *c, const char *data, uint32_t len) {
	wait_sem(c->empty);
	uint32_t *length = (uint32_t *)c->buf;
	char *text = c->buf + sizeof(uint32_t);
	for (uint32_t i = 0; i < len; ++i)
		text[i] = data[i];
	*length = len;
	post_sem(c->ready);
}

static void send_stop(channel *c) {
	wait_sem(c->empty);
	uint32_t *length = (uint32_t *)c->buf;
	*length = UINT32_MAX;
	post_sem(c->ready);
}

static void finish(int started) {
	for (int i = 0; i < started; ++i)
		send_stop(&ch[i]);
	for (int i = 0; i < started; ++i)
		waitpid(ch[i].pid, NULL, 0);
	for (int i = 0; i < created; ++i) {
		sem_close(ch[i].ready);
		sem_close(ch[i].empty);
		munmap(ch[i].buf, SHM_SIZE);
		close(ch[i].shm);
	}
	cleanup();
}

int main() {
	char progpath[1024]; {
		ssize_t len = readlink("/proc/self/exe", progpath, sizeof(progpath) - 1);
		if (len == -1)
			die("error: failed to read full program path\n");
		while (progpath[len] != '/')
			--len;
		progpath[len] = '\0';
	}
	char name1[256];
	char name2[256];
	int len1 = read_line(name1, sizeof(name1));
	int len2 = read_line(name2, sizeof(name2));
	if (len1 <= 1 || len2 <= 1)
		die("error: need two file names\n");
	name1[len1 - 1] = '\0';
	name2[len2 - 1] = '\0';
	int files[2];
	files[0] = open(name1, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	files[1] = open(name2, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (files[0] == -1 || files[1] == -1)
		die("error: failed to open file\n");
	int rnd = open("/dev/urandom", O_RDONLY);
	if (rnd == -1)
		die("error: failed to open /dev/urandom\n");
	int parent_pid = (int)getpid();
	for (int i = 0; i < 2; ++i)
		create_channel(&ch[i], i + 1, parent_pid);
	int started = 0;
	for (int i = 0; i < 2; ++i) {
		const pid_t child = fork();
		if (child == -1) {
			print_err("error: failed to spawn new process\n");
			finish(started);
			return 1;
		}
		if (child == 0) {
			dup2(files[i], STDOUT_FILENO);
			close(files[0]);
			close(files[1]);
			close(rnd);
			close(ch[0].shm);
			close(ch[1].shm);
			char path[1100];
			snprintf(path, sizeof(path) - 1, "%s/%s", progpath, CHILD_PROGRAM_NAME);
			char *const args[] = {CHILD_PROGRAM_NAME, ch[i].shm_name, ch[i].ready_name, ch[i].empty_name, NULL};
			execv(path, args);
			print_err("error: failed to exec into new exectuable image\n");
			_exit(1);
		}
		ch[i].pid = child;
		++started;
	}
	close(files[0]);
	close(files[1]);
	char buf[LINE_MAX_LEN];
	int bytes;
	while ((bytes = read_line(buf, sizeof(buf))) > 0) {
		uint32_t r = 0;
		if (read(rnd, &r, sizeof(r)) != sizeof(r))
			die("error: failed to read /dev/urandom\n");
		if (r % 100 < 80)
			send_line(&ch[0], buf, bytes);
		else
			send_line(&ch[1], buf, bytes);
	}
	close(rnd);
	finish(started);
	return 0;
}
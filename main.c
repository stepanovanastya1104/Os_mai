#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>

#define MAX_POINTS  4000000
#define MAX_K       32
#define MAX_THREADS 1024
#define MAX_ITER    100

static double px[MAX_POINTS];
static double py[MAX_POINTS];
static int labels[MAX_POINTS];

static int n_points;
static int n_clusters;

static double cx[MAX_K];
static double cy[MAX_K];

static double sum_x[MAX_THREADS][MAX_K];
static double sum_y[MAX_THREADS][MAX_K];
static int cnt[MAX_THREADS][MAX_K];
static int changed[MAX_THREADS];

static int iterations_done;
static uint32_t rng_state = 12345;

static uint32_t rng_next(void) {
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

static double now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static int parse_int(const char *s) {
	int r = 0;
	if (s[0] == '\0')
		return -1;
	for (size_t i = 0; s[i] != '\0'; ++i) {
		if (s[i] < '0' || s[i] > '9')
			return -1;
		r = r * 10 + (s[i] - '0');
	}
	return r;
}

static void generate_points(void) {
	double bx[8], by[8];
	for (int i = 0; i < 8; ++i) {
		bx[i] = rng_next() % 10000;
		by[i] = rng_next() % 10000;
	}
	for (int i = 0; i < n_points; ++i) {
		int b = rng_next() % 8;
		px[i] = bx[b] + (int)(rng_next() % 3000) - 1500;
		py[i] = by[b] + (int)(rng_next() % 3000) - 1500;
		labels[i] = -1;
	}
}

static void init_centroids(void) {
	for (int k = 0; k < n_clusters; ++k) {
		cx[k] = px[k];
		cy[k] = py[k];
	}
	for (int i = 0; i < n_points; ++i)
		labels[i] = -1;
}

static void assign_range(int t, int lo, int hi) {
	for (int k = 0; k < n_clusters; ++k) {
		sum_x[t][k] = 0;
		sum_y[t][k] = 0;
		cnt[t][k] = 0;
	}
	int ch = 0;
	for (int i = lo; i < hi; ++i) {
		int best = 0;
		double best_d = 1e300;
		for (int k = 0; k < n_clusters; ++k) {
			double dx = px[i] - cx[k];
			double dy = py[i] - cy[k];
			double d = dx * dx + dy * dy;
			if (d < best_d) {
				best_d = d;
				best = k;
			}
		}
		if (labels[i] != best) {
			labels[i] = best;
			++ch;
		}
		sum_x[t][best] += px[i];
		sum_y[t][best] += py[i];
		++cnt[t][best];
	}
	changed[t] = ch;
}

static bool update_centroids(int slots) {
	int total_changed = 0;
	for (int t = 0; t < slots; ++t)
		total_changed += changed[t];
	for (int k = 0; k < n_clusters; ++k) {
		double sx = 0, sy = 0;
		int c = 0;
		for (int t = 0; t < slots; ++t) {
			sx += sum_x[t][k];
			sy += sum_y[t][k];
			c += cnt[t][k];
		}
		if (c > 0) {
			cx[k] = sx / c;
			cy[k] = sy / c;
		}
	}
	return total_changed > 0;
}

static void kmeans_sequential(void) {
	init_centroids();
	iterations_done = 0;
	while (iterations_done < MAX_ITER) {
		assign_range(0, 0, n_points);
		++iterations_done;
		if (!update_centroids(1))
			break;
	}
}

static pthread_barrier_t barrier;
static int thread_count;
static volatile bool stop_flag;
static int pause_sec;

typedef struct {
	int id;
	int lo;
	int hi;
} worker_arg;

static void *worker(void *p) {
	worker_arg *a = (worker_arg *)p;
	if (a->id == 0 && pause_sec > 0)
		sleep(pause_sec);
	while (1) {
		assign_range(a->id, a->lo, a->hi);
		pthread_barrier_wait(&barrier);
		if (a->id == 0) {
			++iterations_done;
			bool go = update_centroids(thread_count);
			stop_flag = !go || iterations_done >= MAX_ITER;
		}
		pthread_barrier_wait(&barrier);
		if (stop_flag)
			break;
	}
	return NULL;
}

static bool kmeans_parallel(int threads) {
	init_centroids();
	iterations_done = 0;
	stop_flag = false;
	thread_count = threads;
	static pthread_t tid[MAX_THREADS];
	static worker_arg args[MAX_THREADS];
	if (pthread_barrier_init(&barrier, NULL, threads) != 0)
		return false;
	int chunk = n_points / threads;
	int rest = n_points % threads;
	int pos = 0;
	for (int t = 0; t < threads; ++t) {
		args[t].id = t;
		args[t].lo = pos;
		pos += chunk + (t < rest ? 1 : 0);
		args[t].hi = pos;
		if (pthread_create(&tid[t], NULL, worker, &args[t]) != 0) {
			const char msg[] = "error: failed to create thread\n";
			write(STDERR_FILENO, msg, sizeof(msg) - 1);
			return false;
		}
	}
	for (int t = 0; t < threads; ++t)
		pthread_join(tid[t], NULL);
	pthread_barrier_destroy(&barrier);
	return true;
}

int main(int argc, char **argv) {
	if (argc < 4) {
		printf("usage: %s threads points clusters [pause_sec]\n", argv[0]);
		return 0;
	}
	int threads = parse_int(argv[1]);
	n_points = parse_int(argv[2]);
	n_clusters = parse_int(argv[3]);
	pause_sec = argc > 4 ? parse_int(argv[4]) : 0;
	if (threads < 1 || threads > MAX_THREADS || n_points < 1 || n_points > MAX_POINTS ||
	    n_clusters < 1 || n_clusters > MAX_K || n_clusters > n_points || pause_sec < 0) {
		const char msg[] = "error: bad arguments\n";
		write(STDERR_FILENO, msg, sizeof(msg) - 1);
		return 1;
	}
	generate_points();
	double t0 = now_ms();
	kmeans_sequential();
	double t_seq = now_ms() - t0;
	int seq_iters = iterations_done;
	double seq_cx[MAX_K], seq_cy[MAX_K];
	for (int k = 0; k < n_clusters; ++k) {
		seq_cx[k] = cx[k];
		seq_cy[k] = cy[k];
	}
	t0 = now_ms();
	if (!kmeans_parallel(threads))
		return 1;
	double t_par = now_ms() - t0;
	bool same = (seq_iters == iterations_done);
	for (int k = 0; k < n_clusters; ++k) {
		if (seq_cx[k] != cx[k] || seq_cy[k] != cy[k])
			same = false;
	}
	double speedup = t_seq / t_par;
	double efficiency = speedup / threads;
	printf("points=%d clusters=%d threads=%d iterations=%d\n",
	       n_points, n_clusters, threads, iterations_done);
	printf("sequential: %.3f ms\n", t_seq);
	printf("parallel:   %.3f ms\n", t_par);
	printf("speedup:    %.3f\n", speedup);
	printf("efficiency: %.3f\n", efficiency);
	printf("results %s\n", same ? "match" : "DIFFER");
	for (int k = 0; k < n_clusters; ++k)
		printf("centroid %d: (%.2f, %.2f)\n", k, cx[k], cy[k]);
	return 0;
}
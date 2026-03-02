#include "monitor.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

volatile int server_running = 1;

/* ============================= */
/* Helper: Current time (ms)     */
/* ============================= */
static long long current_time_ms() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

/* ============================= */
/* CPU Usage Calculation         */
/* ============================= */
static double get_cpu_usage() {
  static long long last_proc_time = 0;
  static long long last_wall_time = 0;

  FILE *f = fopen("/proc/self/stat", "r");
  if (!f)
    return 0.0;

  long long utime, stime;
  char buffer[2048];

  fgets(buffer, sizeof(buffer), f);
  fclose(f);

  /* Extract utime and stime (14th & 15th fields) */
  sscanf(buffer,
         "%*d %*s %*c %*d %*d %*d %*d %*d "
         "%*u %*u %*u %*u %*u "
         "%lld %lld",
         &utime, &stime);

  long long proc_time = utime + stime;
  long long wall_time = current_time_ms();

  double cpu_percent = 0.0;

  if (last_wall_time != 0) {
    long long delta_proc = proc_time - last_proc_time;
    long long delta_wall = wall_time - last_wall_time;

    if (delta_wall > 0) {
      cpu_percent = (double)delta_proc / delta_wall * 100.0;
    }
  }

  last_proc_time = proc_time;
  last_wall_time = wall_time;

  return cpu_percent;
}

/* ============================= */
/* Memory Usage (VmRSS)          */
/* ============================= */
static long get_vmrss() {
  FILE *f = fopen("/proc/self/status", "r");
  if (!f)
    return 0;

  char line[256];
  long vmrss = 0;

  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, "VmRSS:", 6) == 0) {
      sscanf(line, "VmRSS: %ld kB", &vmrss);
      break;
    }
  }

  fclose(f);
  return vmrss;
}

/* ============================= */
/* Monitoring Thread             */
/* ============================= */
static void *monitor_function(void *arg) {
  const char *filename = (const char *)arg;

  FILE *f = fopen(filename, "a");
  if (!f) {
    perror("Failed to open metrics file");
    return NULL;
  }

  while (server_running) {
    long long timestamp = current_time_ms();
    double cpu = get_cpu_usage();
    long vmrss = get_vmrss();

    fprintf(f, "%lld,%.2f,%ld\n", timestamp, cpu, vmrss);
    fflush(f);

    sleep(5);
  }

  fclose(f);
  return NULL;
}

/* ============================= */
/* Public API                    */
/* ============================= */
void start_monitor(const char *filename) {
  pthread_t tid;
  pthread_create(&tid, NULL, monitor_function, (void *)filename);
  pthread_detach(tid);
}

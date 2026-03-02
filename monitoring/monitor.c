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

  int pid;
  char comm[256];
  char state;

  // Read pid, command name, and state
  if (fscanf(f, "%d %255s %c", &pid, comm, &state) != 3) {
    fclose(f);
    return 0.0;
  }

  // Skip next 11 fields
  for (int i = 0; i < 11; i++)
    fscanf(f, "%*s");

  long long utime, stime;
  if (fscanf(f, "%lld %lld", &utime, &stime) != 2) {
    fclose(f);
    return 0.0;
  }

  fclose(f);

  long long proc_time = utime + stime;     // ticks
  long long wall_time = current_time_ms(); // ms

  double cpu_percent = 0.0;

  if (last_wall_time != 0) {
    long long delta_proc = proc_time - last_proc_time;
    long long delta_wall = wall_time - last_wall_time;

    long hz = sysconf(_SC_CLK_TCK);                  // ticks/sec
    double delta_proc_sec = (double)delta_proc / hz; // seconds of CPU
    double delta_wall_sec =
        (double)delta_wall / 1000.0; // seconds of wall clock

    if (delta_wall_sec > 0)
      cpu_percent = (delta_proc_sec / delta_wall_sec) * 100.0;
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

    sleep(1);
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

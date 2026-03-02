#ifndef MONITOR_H
#define MONITOR_H

#include <pthread.h>

extern volatile int server_running;

/* Start monitoring thread and log to given file */
void start_monitor(const char *filename);

#endif

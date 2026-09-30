#ifndef LOGGING_HELPER_H
#define LOGGING_HELPER_H

#include <stdio.h>
#include <time.h>

int log_open(const char *path);
void log_close(void); /* FIX: (void) for strict C prototype */
void die(const char* mssg);
void msg(const char* mssg);
void note(const char* mssg);

#endif
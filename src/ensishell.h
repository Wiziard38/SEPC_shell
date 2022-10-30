#ifndef __ENSISHELL_H
#define __ENSISHELL_H

#include "readcmd.h"

typedef struct linked_process {
	pid_t process_id;
	char *process_cmd;
	struct timeval start_time;
	struct linked_process *next_process;
} linked_process;

void add_bg_process(pid_t pid, struct timeval start_time);

void remove_bg_process(pid_t pid);

void execute_command(struct cmdline *l);

int count_pipes(struct cmdline *l);

void sig_handler(int sig, siginfo_t *info, void *secret);

#endif
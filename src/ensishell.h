#ifndef __ENSISHELL_H
#define __ENSISHELL_H

#include "readcmd.h"

typedef struct process {
	pid_t process_id;
	char *process_cmd;
	struct process *next_process;
} process;

void add_bg_process(pid_t pid);

void remove_bg_process(pid_t pid);

void execute_command(struct cmdline *l);

int count_pipes(struct cmdline *l);

#endif
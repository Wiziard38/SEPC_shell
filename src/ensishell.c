/*****************************************************
 * Copyright Grégory Mounié 2008-2015                *
 *           Simon Nieuviarts 2002-2009              *
 * This code is distributed under the GLPv3 licence. *
 * Ce code est distribué sous la licence GPLv3+.     *
 *****************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <fcntl.h>


#include "variante.h"
#include "readcmd.h"

#ifndef VARIANTE
#error "Variante non défini !!"
#endif

typedef struct process {
	pid_t process_id;
	char* process_cmd;
	struct process *next_process;
} process;

struct process *bg_process_list = NULL;

/* Guile (1.8 and 2.0) is auto-detected by cmake */
/* To disable Scheme interpreter (Guile support), comment the
 * following lines.  You may also have to comment related pkg-config
 * lines in CMakeLists.txt.
 */

#if USE_GUILE == 1
#include <libguile.h>

int question6_executer(char *line)
{
	/* Question 6: Insert your code to execute the command line
	 * identically to the standard execution scheme:
	 * parsecmd, then fork+execvp, for a single command.
	 * pipe and i/o redirection are not required.
	 */
	printf("Not implemented yet: can not execute %s\n", line);

	/* Remove this line when using parsecmd as it will free it */
	free(line);
	
	return 0;
}

SCM executer_wrapper(SCM x)
{
        return scm_from_int(question6_executer(scm_to_locale_stringn(x, 0)));
}
#endif


void terminate(char *line) {
#if USE_GNU_READLINE == 1
	/* rl_clear_history() does not exist yet in centOS 6 */
	clear_history();
#endif
	if (line)
	  free(line);
	printf("exit\n");
	exit(0);
}

void add_bg_process(pid_t pid, char* command) {
	struct process *new_process = malloc(sizeof(struct process));
	new_process->process_id = pid;
	new_process->process_cmd = malloc(sizeof(command));
	strcpy(new_process->process_cmd, command);

	if (bg_process_list == NULL) {
		new_process->next_process = NULL;
		bg_process_list = new_process;
	} else {
		new_process->next_process = bg_process_list;
		bg_process_list = new_process;
	}
}

void remove_bg_process(pid_t pid) {
	process *current_process = bg_process_list;
	process *previous_process = NULL;

	if (current_process != NULL) {
		if (current_process->process_id == pid) {
			if (previous_process == NULL) {
				bg_process_list = current_process->next_process;
			} else {
				previous_process->next_process = current_process->next_process;
			}
			free(current_process->process_cmd);
			free(current_process);
			return;
		}
	}
}


void execute_command(struct cmdline *l) {
	pid_t pid;
	int pipe_exists = false;
	int pipefd[2];
    int in_old = dup(STDIN_FILENO);
	int out_old = dup(STDOUT_FILENO);
	
    // if |
	if (l->seq[1] != NULL) {
		pipe_exists = true;
		if (pipe(pipefd) == -1) {
			printf("Piping error! \n");
			exit(0);
		};
	}

    // if <
    if (l->in != NULL) {
        int input_fd = open(l->in, O_RDWR);
        if (input_fd > -1) {
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
        } else {
            printf("Opening file error! \n");
            exit(0);
        }
    }

    // if >
    if (l->out != NULL) {    
        int output_fd = open(l->out, O_WRONLY | O_CREAT);
        if (output_fd > -1) {
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);
        } else {
            printf("Opening file error! \n");
            exit(0);
        }
    }


	if ((pid = fork()) == -1) {
		printf("Forking error \n");
	} else if (pid == 0) {

		if (pipe_exists) {
			dup2(pipefd[0], STDIN_FILENO);
			if (close(pipefd[0]) == -1 || close(pipefd[1]) == -1 ) {
				printf("Closing pipe error \n");
				exit(0);
			}
            execvp(l->seq[1][0], l->seq[1]);
		} else {
            execvp(l->seq[0][0], l->seq[0]);
        }

		printf("Execvp error! \n" );
		exit(0);
	}

    // if |
	if (pipe_exists) {
		if (fork() == 0) {
			dup2(pipefd[1], STDOUT_FILENO);
			if (close(pipefd[0]) == -1 || close(pipefd[1]) == -1 ) {
				printf("Closing pipe error \n");
				exit(0);
			}

			execvp(l->seq[0][0], l->seq[0]);
			printf("Execvp error! \n" );
			exit(0);
		}
	}

    // if |
    if (pipe_exists) {
        if (close(pipefd[0]) == -1 || close(pipefd[1]) == -1 ) {
            printf("Closing pipe error \n");
            exit(0);
		}
    }

    // if &
	if(l->bg){
		add_bg_process(pid, l->seq[0][0]);
	} else {
		int status = 0;
		waitpid(pid, &status, 0);
	}

    close(in_old);	 		
	close(out_old);
}


int main() {
        printf("Variante %d: %s\n", VARIANTE, VARIANTE_STRING);

#if USE_GUILE == 1
        scm_init_guile();
        /* register "executer" function in scheme */
        scm_c_define_gsubr("executer", 1, 0, 0, executer_wrapper);
#endif

	while (1) {
		struct cmdline *l;
		char *line=0;
		int i, j;
		char *prompt = "ensishell>";

		/* Readline use some internal memory structure that
		   can not be cleaned at the end of the program. Thus
		   one memory leak per command seems unavoidable yet */
		line = readline(prompt);
		if (line == 0 || ! strncmp(line,"exit", 4)) {
			terminate(line);
		}

#if USE_GNU_READLINE == 1
		add_history(line);
#endif


#if USE_GUILE == 1
		/* The line is a scheme command */
		if (line[0] == '(') {
			char catchligne[strlen(line) + 256];
			sprintf(catchligne, "(catch #t (lambda () %s) (lambda (key . parameters) (display \"mauvaise expression/bug en scheme\n\")))", line);
			scm_eval_string(scm_from_locale_string(catchligne));
			free(line);
                        continue;
                }
#endif

		/* parsecmd free line and set it up to 0 */
		l = parsecmd( & line);

		/* If input stream closed, normal termination */
		if (!l) {
		  
			terminate(0);
		}
		

		if (l->err) {
			/* Syntax error, read another command */
			printf("error: %s\n", l->err);
			continue;
		}

		if (l->in) printf("in: %s\n", l->in);
		if (l->out) printf("out: %s\n", l->out);
		if (l->bg) printf("background (&)\n");

		/* Display each command of the pipe */
		for (i=0; l->seq[i]!=0; i++) {
			char **cmd = l->seq[i];
			printf("seq[%d]: ", i);
			for (j=0; cmd[j]!=0; j++) {
					printf("'%s' ", cmd[j]);
			}
			printf("\n");
		}

		if (strcmp(l->seq[0][0], "jobs") == 0) {
			printf("========================= \n");
			printf("Processes in background : \n");
			process *current_process = bg_process_list;
			pid_t pid;
			int child_state;

			if (current_process != NULL) {
				if (waitpid(current_process->process_id, &child_state, WNOHANG)) {
					pid = current_process->process_id;
					current_process = current_process->next_process;
					remove_bg_process(pid); // replace pid with line ?
				} else {
					printf(" > Process %d : %s \n", current_process->process_id, current_process->process_cmd);
					current_process = current_process->next_process;
				}
			}
			printf("========================= \n");
		
		} else {
			execute_command(l);
		}	
	}
}

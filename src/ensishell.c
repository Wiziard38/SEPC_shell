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
#include "ensishell.h"

#include "variante.h"
#include "readcmd.h"

#ifndef VARIANTE
#error "Variante non défini !!"
#endif


struct process *bg_process_list = NULL;

static char entered_command[100];

/* Guile (1.8 and 2.0) is auto-detected by cmake */
/* To disable Scheme interpreter (Guile support), comment the
 * following lines.  You may also have to comment related pkg-config
 * lines in CMakeLists.txt.
 */

#if USE_GUILE == 1
#include <libguile.h>

int question6_executer(char *line) {
	/* Question 6: Insert your code to execute the command line
	 * identically to the standard execution scheme:
	 * parsecmd, then fork+execvp, for a single command.
	 * pipe and i/o redirection are not required.
	 */

	execute_command(parsecmd(&line));

	return 0;
}

SCM executer_wrapper(SCM x) {
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
		// int i, j;
		char *prompt = "ensishell$ ";

		/* Readline use some internal memory structure that
		   can not be cleaned at the end of the program. Thus
		   one memory leak per command seems unavoidable yet */
		line = readline(prompt);
		strcpy(entered_command, line);
		if (line == 0 || ! strncmp(line,"exit", 4)) {
			terminate(line);
		}

#if USE_GNU_READLINE == 1
		if (strcmp(line, "") != 0) {
			add_history(line);
		}
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

		if (*l->seq == NULL) {
			// pass
		} else if (strcmp(l->seq[0][0], "jobs") == 0) {
			process *current_process = bg_process_list;
			pid_t pid;
			int child_state;

			while (current_process != NULL) {
				if (waitpid(current_process->process_id, &child_state, WNOHANG)) {
					printf("[%d]   Done%18s%s\n", current_process->process_id, "", current_process->process_cmd);
					pid = current_process->process_id;
					current_process = current_process->next_process;
					remove_bg_process(pid);
				} else {
					printf("[%d]   Running%15s%s\n", current_process->process_id, "", current_process->process_cmd);
					current_process = current_process->next_process;
				}
			}
		} else {
			execute_command(l);
		}
	}
}



void execute_command(struct cmdline *l) {
	pid_t pid;
	int status;

	int i, current_cmd_index = 0;
	int nb_pipes = count_pipes(l);
	bool pipe_exists = (nb_pipes != 0);
	int pipefd[nb_pipes][2];

    int old_input_fd = dup(STDIN_FILENO);
	int old_output_fd = dup(STDOUT_FILENO);

    // if pipe(s)
	if (pipe_exists) {
		for (i = 0; i < nb_pipes; i++) {
			if (pipe(pipefd[i]) == -1) {
				perror("Piping error");
				exit(EXIT_FAILURE);
			}
		}
	}

    // if <
    if (l->in != NULL) {
        int input_fd = open(l->in, O_RDWR);
        if (input_fd > -1) {
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
        } else {
            perror("Opening file error");
            exit(EXIT_FAILURE);
        }
    }

    // if >
    if (l->out != NULL) {
        mode_t open_mode = S_IRUSR | S_IXUSR | S_IWUSR | S_IRGRP | S_IROTH;
        int output_fd = open(l->out, O_RDWR | O_TRUNC | O_CREAT, open_mode);
        if (output_fd > -1) {
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);
        } else {
            perror("Opening file error");
            exit(EXIT_FAILURE);
        }
    }



	if (!pipe_exists) {
		if ((pid = fork()) == -1) {
			perror("Forking error");
			exit(EXIT_FAILURE);
		} else if (pid == 0) {
			execvp(l->seq[0][0], l->seq[0]);
			perror("Execvp error");
			exit(EXIT_FAILURE);
		} else {
			if(l->bg){
				add_bg_process(pid);
			} else {
				wait(&status);
			}
		}


	} else {
		while (current_cmd_index <= nb_pipes) {

			if ((pid = fork()) == -1) {
				perror("Forking error");
				exit(EXIT_FAILURE);

			} else if (pid == 0) { // in child

				// If not first command of pipe
				if (current_cmd_index != 0) {
					if (dup2(pipefd[current_cmd_index - 1][STDIN_FILENO], STDIN_FILENO) == -1) {
						perror("Duping error");
						exit(EXIT_FAILURE);
					}
				}
				
				// If not last command of pipe
				if (current_cmd_index != nb_pipes) {
					if (dup2(pipefd[current_cmd_index][STDOUT_FILENO], STDOUT_FILENO) == -1) {
						perror("Duping error");
						exit(EXIT_FAILURE);
					}
				}

				for (i = 0; i < nb_pipes; i++) {
					if ((close(pipefd[i][0]) == -1) || (close(pipefd[i][1]) == -1)) {
						perror("Closing pipe error");
						exit(EXIT_FAILURE);
					}
				}
				
				execvp(l->seq[current_cmd_index][0], l->seq[current_cmd_index]);
				perror("Execvp error" );
				exit(EXIT_FAILURE);
			}
			current_cmd_index++;
		}

		for (i = 0; i < nb_pipes; i++) {
			if ((close(pipefd[i][0]) == -1) || (close(pipefd[i][1]) == -1)) {
				perror("Closing pipe error");
				exit(EXIT_FAILURE);
			}
		}

		if(l->bg){
			add_bg_process(pid);
		} else {
			for (i = 0; i < 2*nb_pipes; i++) {
				wait(&status);
			}
		}
	}


	// Link back standard in and out
    if (l->in != NULL || pipe_exists) {
        if (dup2(old_input_fd, STDIN_FILENO) == -1) {
			perror("Duping error");
			exit(EXIT_FAILURE);
		}
    }

    if (l->out != NULL || pipe_exists) {
        if (dup2(old_output_fd, STDOUT_FILENO) == -1) {
			perror("Duping error");
			exit(EXIT_FAILURE);
		}
    }

	if ((close(old_input_fd) == -1) || (close(old_output_fd) == -1)) {
		perror("Closing pipe error");
		exit(EXIT_FAILURE);
	}
}


void add_bg_process(pid_t pid) {
	struct process *new_process = malloc(sizeof(struct process));
	new_process->process_id = pid;
	new_process->process_cmd = malloc(sizeof(entered_command));
	strcpy(new_process->process_cmd, entered_command);
	new_process->next_process = NULL;

	if (bg_process_list == NULL) {
		bg_process_list = new_process;
	} else {
		struct process *current_process = bg_process_list;
		while (current_process->next_process != NULL) {
			current_process = current_process->next_process;
		}
		current_process->next_process = new_process;
	}
}

void remove_bg_process(pid_t pid) {
	process *current_process = bg_process_list;
	process *previous_process = NULL;

	if (bg_process_list->process_id == pid) {
		bg_process_list = bg_process_list->next_process;
		free(current_process->process_cmd);
		free(current_process);
		return;
	}

	while (true) {
		previous_process = current_process;
		current_process = current_process->next_process;

		if (current_process->process_id == pid) {
			previous_process->next_process = current_process->next_process;
			free(current_process->process_cmd);
			free(current_process);
			return;
		}
	}
}

int count_pipes(struct cmdline *l) {
	int nb_pipes = 0;
	while (l->seq[nb_pipes] != NULL) {
		nb_pipes++;
	}
	return nb_pipes - 1;
}
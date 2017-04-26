#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "tokenizer.h"
#include "job.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

struct job_list *job_list;

int find_program_path(struct tokens *tokens, char *program_path);
int extract_parameters(struct tokens *tokens, char** argv, int *a_p, int *b_p, int *backgroud_p);

int cmd_exit(struct tokens *tokens);
int cmd_help(struct tokens *tokens);
int cmd_pwd(struct tokens *tokens);
int cmd_cd(struct tokens *tokens);
int cmd_wait(struct tokens *tokens);
int cmd_jobs(struct tokens *tokens);

int run_program(struct tokens *tokens);
/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens *tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?",    "show this help menu"},
  {cmd_exit, "exit", "exit the command shell"},
  {cmd_pwd,  "pwd",  "print the current working directory"},
  {cmd_cd,   "cd",   "change the current working directory"},
  {cmd_wait, "wait", "wait until all background jobs have terminated before returning to the prompt"},
  {cmd_jobs, "jobs", "No description"}
};



/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens *tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 0;
}

/* Exits this shell */
int cmd_exit(unused struct tokens *tokens) {
   exit(0);
}

/* Prints the current working directory */
int cmd_pwd(unused struct tokens *tokens) {
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) != NULL)
    fprintf(stdout, "%s\n", cwd);
  else
    return -1;
  return 0;
}

/* Changes the current working directory */
int cmd_cd(unused struct tokens *tokens) {
  char *p = tokens_get_token(tokens, 1);  
  if (!chdir(p)) {
    return 0;
  }
  fprintf(stdout, "-shell: cd: %s: No such file or directory\n", p);
  return -1;
}

/* Wait until all background jobs have terminated before returning to the prompt */
int cmd_wait(unused struct tokens *tokens) {
  wait_for_running_jobs(job_list);
  return 0;
}

int cmd_jobs(unused struct tokens *tokens) {
  print_job_list(job_list);
  return 0;
}

int find_program_path(struct tokens *tokens, char *program_path) {
  char *path = tokens_get_token(tokens, 0);
  if (!path) return -1;

  if (path[0] == '/') {
    if (access(path, F_OK) != -1) {
      strcpy(program_path, path);
      return 0;
    }
 	  else {
      fprintf(stdout, "-shell: %s: No such file or directory\n", path);
      return -1;
    }
  }
  char *command = path;
	char *env_PATH = getenv("PATH");
  int j = 0;
  for (int i=0; i<strlen(env_PATH); i++) {
		if (env_PATH[i] != ':') 
      program_path[j++] = env_PATH[i];
 		else {
      program_path[j++] = '/';
			program_path[j] = '\0';
			strcat(program_path, command);
			if (access(program_path, F_OK) != -1) 
        return 0;
		  else j = 0;
	  }
  }

	fprintf(stdout, "%s: command not found\n", command);
  return -1;
}

int extract_parameters(struct tokens *tokens, char** argv, int *a_p, int *b_p, int *background_p) {
  if (!strcmp(tokens_get_token(tokens, tokens_get_length(tokens)-1), "&")) 
   	*background_p = 1;
  int i = 1; 
  char *token = tokens_get_token(tokens, i);
  while (token && strcmp(token, "<") && strcmp(token, ">") && strcmp(token, "&")) {
    argv[i++] = token;
    token = tokens_get_token(tokens, i);
  }
  if (!token) {
    argv[i] = NULL;
    return 0;
  }
  if (!strcmp(token, "&")) {
	 argv[i] = NULL;
	 return 0;
  }
  char *arrow = tokens_get_token(tokens, i);
  char *file_path = tokens_get_token(tokens, i+1);
  if (!file_path) {
    fprintf(stdout, "-shell: syntax error near unexpected token `newline'\n");
    return -1;
  }

  int fd = open(file_path, O_RDONLY, 0);
  if (strcmp(arrow, "<") == 0) {  
    if (fd < 0)  {
      fprintf(stdout, "-shell: %s: No such file or directory\n", file_path);
      return -1;
    }
    *a_p = fd;
    *b_p = STDIN_FILENO;
  }
  else {
    *a_p  = open(file_path, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    *b_p = STDOUT_FILENO;
  }
  argv[i] = NULL;
  return 0;
}

int run_program(struct tokens *tokens) {
  char program_path[1024];
  if (find_program_path(tokens, program_path) == -1) return -1;

  int len = tokens_get_length(tokens);
  char* argv[len+1];
  argv[0] = program_path;

  int a = -1, b = -1, background = 0;
  if (extract_parameters(tokens, argv, &a, &b, &background) == -1) return -1;
 
  pid_t pid = fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    if (a != -1) 
      dup2(a, b);
	setpgrp();
    execv(argv[0], argv);
    exit(-1);
  }
  else {
    if (background) {
	  char *line = untokenize(tokens);
	  struct job new_job = {pid, line, running, 1};
	  push_job(job_list, new_job);
	  free(line);
    }
	else {
	  tcsetpgrp(0, pid);
      int status;
	  pid_t return_pid = waitpid(pid, &status, WUNTRACED);
      if (WIFSIGNALED(status)) printf("\n");
      else if (WIFSTOPPED(status)){
		printf("\n");
        char *line = untokenize(tokens);
		struct job new_job = {return_pid, line, stopped, 1};
		push_job(job_list, new_job);
 		free(line);
      }
	  signal(SIGTTOU, SIG_IGN);
	  tcsetpgrp(0, getpgrp());
    }
    if (a!= -1) close(a);	
    return 0;
  }
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

int main(unused int argc, unused char *argv[]) {
  init_shell();
  static char line[4096];
  int line_num = 0;
  job_list = init_job_list();
  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens *tokens = tokenize(line);

    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));
    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    }
    else {
      run_program(tokens);
    }
    wait_for_terminated_background_jobs(job_list);
    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);
    /* Clean up memory */
    tokens_destroy(tokens);
  }
  free_job_list(job_list);
  return 0;
}

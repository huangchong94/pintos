#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "job.h"

struct job_list {
  struct job *jobs;
  int count;
  int capacity;
};

struct job_list* init_job_list() {
  struct job_list* job_list= (struct job_list*)malloc(sizeof(struct job_list));
  job_list->capacity = 1;
  job_list->jobs = (struct job*)malloc(job_list->capacity*sizeof(struct job));
  job_list->count = 0;
  return job_list;
}

void push_job(struct job_list *job_list, struct job job) {
  if (job_list->capacity <= job_list->count) {
    job_list->capacity *= 2;
    job_list->jobs = realloc(job_list->jobs, sizeof(struct job)*job_list->capacity);
  }
  char *job_desc = (char*)malloc(sizeof(char)*4096);
  strcpy(job_desc, job.desc);
  job.desc = job_desc;
  job_list->jobs[job_list->count] = job;
  job_list->count += 1;
}

void set_job_state(struct job_list *job_list, pid_t pid, enum job_state state) {
  for (int i=0; i<job_list->count; i++) {
    struct job *job_p = job_list->jobs + i;
    if (job_p->pid == pid) job_p->state = state;
  }
}

void wait_for_terminated_background_jobs(struct job_list *job_list) {
  for (int i=0; i<job_list->count; i++) {
    struct job *job_p = job_list->jobs + i;
    pid_t pid = job_p->pid;
    if (job_p->background && job_p->state == running) {
      if ( waitpid(pid, NULL, WNOHANG) > 0)
        job_p->state = done;
    }
  }
}

void wait_for_running_jobs(struct job_list *job_list) {
  struct job *job_p = job_list->jobs;
  for (int i=0; i<job_list->count; i++) {
    job_p += i;
    if (job_p->state == running) {
	  pid_t pid = job_p->pid;
	  int status;
	  waitpid(pid, &status, 0);
	  if (WIFEXITED(status)) job_p->state = done;
    }
  }
}

void print_job_list(struct job_list *job_list) {
  struct job *job_p = job_list->jobs;
  for (int i=0; i<job_list->count; i++) {
    job_p = job_list->jobs + i;
    char *state;
    if (job_p->state == running) state = "Running";
	else if (job_p->state == stopped) state = "Stopped";
	else state = "Done";
    printf("[%d] %d %s %s\n", i+1, job_p->pid, state, job_p->desc);
  }
}

void free_job_list(struct job_list *job_list) {
  for (int i=0; i<job_list->count; i++)
    free(job_list->jobs[i].desc);
  free(job_list->jobs);
  free(job_list);
}

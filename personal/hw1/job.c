#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "job.h"

#define RUNNING 100
#define STOPPED 010
#define DONE    001

struct job {
  pid_t pid;
  char *desc;
  enum job_state state;
};

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

static int empty(struct job_list *job_list) {
  return job_list->count == 0;
}

// static int job_done(struct job *job) {
//   enum job_state state = job->state;
//   return state==done || state==done_and_displayed || state==terminated || state==terminated_and_displayed;
// }

static int job_running(struct job *job) {
  return job->state==running_background || job->state==running_foreground;
}

static int check(struct job *job, int conditions) {
  if (!conditions) return 1;
  int bits = 0;
  if (job_running(job)) bits |= 100;
  else if (job->state==stopped) bits |= 010;
  else  bits |= 001;
  if (bits & conditions) return 1;
  
  int result;
  for (int i=0; i<3; i++) {
    if (bits & (1<<i)) {
      if (i==0) result =  JOB_HAS_DONE;
      else if (i==1) result = JOB_HAS_STOPPED;
      else result = JOB_IS_RUNNING;
    }
  }
  return result;
}

static struct job* get_job_by_pid(struct job_list *job_list, pid_t pid, int *error, int conditions) {
  struct job *job_p = NULL;
  if (pid == -1) {
    if (empty(job_list)) {
      *error = NO_JOB;
      return NULL;
    }
    int i; 
    for (i=job_list->count-1; i>-1; i--) {
      job_p = job_list->jobs + i;
      if ((*error = check(job_p, conditions)) == 1)
        return job_p;
    }
    if (i==-1) {
      *error = NO_JOB;
      return NULL;
    }
  }

  job_p = NULL;
  int i = 0;
  while (i < job_list->count) {
    job_p = job_list->jobs + i;
    if (job_p->pid == pid) {
		  if ((*error=check(job_p, conditions)) == 1) 
        return job_p;
      else 
        return NULL;
    }
    i++;
  }

  *error = NO_SUCH_JOB;
  return NULL; 
}

void push_job(struct job_list *job_list, pid_t pid, char *desc, enum job_state state) {
  if (job_list->capacity <= job_list->count) {
    job_list->capacity *= 2;
    job_list->jobs = realloc(job_list->jobs, sizeof(struct job)*job_list->capacity);
  }
  char *job_desc = (char*)malloc(sizeof(char)*4096);
  strcpy(job_desc, desc);
  struct job new_job = {pid, job_desc, state};
  job_list->jobs[job_list->count] = new_job;
  job_list->count += 1;
}

int wait_foreground_job(pid_t pid, struct job_list *job_list, char *desc) {
  int rval = 0;
  tcsetpgrp(0, pid);
  int status;
	if (waitpid(pid, &status, WUNTRACED) < 0) return -1;
  if (WIFSIGNALED(status)) 
    rval =  1;
  else if (WIFSTOPPED(status)){
		push_job(job_list, pid, desc, stopped);
    rval =  1;
  }
	signal(SIGTTOU, SIG_IGN);
	tcsetpgrp(0, getpgrp());
  return rval;
}

void wait_terminated_background_jobs(struct job_list *job_list) {
  for (int i=0; i<job_list->count; i++) {
    struct job *job_p = job_list->jobs + i;
    pid_t pid = job_p->pid;
    if (job_p->state == running_background) {
      if ( waitpid(pid, NULL, WNOHANG) > 0)
        job_p->state = done;
    }
  }
}

void wait_all_running_jobs(struct job_list *job_list) {
  struct job *job_p = job_list->jobs;
  for (int i=0; i<job_list->count; i++) {
    job_p += i;
    if (job_p->state == running_background) {
	  pid_t pid = job_p->pid;
	  int status;
	  waitpid(pid, &status, 0);
	  if (WIFEXITED(status)) job_p->state = done;
    }
  }
}

int swith_job_to_foreground(pid_t pid, struct job_list *job_list) {
  wait_terminated_background_jobs(job_list);
  int error;
  struct job *job_p = get_job_by_pid(job_list, pid, &error, RUNNING|STOPPED);
  if (!job_p) return error;

  if (job_p->state == stopped) 
    kill(pid, SIGCONT);
  job_p->state = running_foreground;

  tcsetpgrp(0, job_p->pid);
  int rval = 0;
  int status;
  waitpid(pid, &status, WUNTRACED);
  if (WIFEXITED(status)) {
    job_p->state = done;
    rval = 0;
  }
  else if (WIFSIGNALED(status)) {
    job_p->state = terminated;
    rval = 1;
  }
  else if (WIFSTOPPED(status)) {
    job_p->state = stopped;
    rval = 1;
  }

  signal(SIGTTOU, SIG_IGN);
  tcsetpgrp(0, getpid());
  return rval;
}

int resume_background_job(pid_t pid, struct job_list *job_list) {
  wait_terminated_background_jobs(job_list);
  int error;
  struct job *job_p = get_job_by_pid(job_list, pid, &error, STOPPED);
  if (!job_p) return error;

  kill(job_p->pid, SIGCONT);
  job_p->state = running_background;
  return 0;
}

void print_job_list(struct job_list *job_list) {
  struct job *job_p = job_list->jobs;
  for (int i=0; i<job_list->count; i++) {
    job_p = job_list->jobs + i;
    enum job_state job_state = job_p->state;
    if (job_state == done_and_displayed || job_state==terminated_and_displayed 
        || job_state==running_foreground)
      continue;
    char *state_str;
    if (job_p->state == running_background) state_str = "Running";
	else if (job_p->state == stopped) state_str = "Stopped";
	else if (job_p->state == done) {
    state_str = "Done";
    job_p->state = done_and_displayed;
  }
  else {
    state_str = "Terminated";
    job_p->state = terminated_and_displayed;
  }
    printf("[%d] %d %s     %s\n", i+1, job_p->pid, state_str, job_p->desc);
  }
}

void free_job_list(struct job_list *job_list) {
  for (int i=0; i<job_list->count; i++)
    free(job_list->jobs[i].desc);
  free(job_list->jobs);
  free(job_list);
}

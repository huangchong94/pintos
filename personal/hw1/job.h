#include <unistd.h>
#include <stdlib.h>
enum job_state {running, stopped, done};

struct job {
  pid_t pid;
  char *desc;
  enum job_state state;
  int background;
};
struct job_list;

struct job_list* init_job_list();
void push_job(struct job_list *job_list, struct job job);
void set_job_state(struct job_list *job_list, pid_t pid, enum job_state);
void wait_for_running_jobs(struct job_list *job_list);
void wait_for_terminated_background_jobs(struct job_list *job_list);
void print_job_list(struct job_list *job_list);
void free_job_list(struct job_list *job_list);  

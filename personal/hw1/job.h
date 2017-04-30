#define NO_JOB -5
#define JOB_HAS_DONE -4
#define JOB_HAS_STOPPED -3
#define JOB_IS_RUNNING -2
#define NO_SUCH_JOB -1


enum job_state {
    running_foreground, running_background, 
    stopped, done, 
    terminated, done_and_displayed,
    terminated_and_displayed
};

struct job_list;

struct job_list* init_job_list();
void push_job(struct job_list *job_list, pid_t pid, char *desc, enum job_state state);
int wait_foreground_job(pid_t pid, struct job_list *job_list, char *desc);
int swith_job_to_foreground(pid_t pid, struct job_list *job_list);
int resume_background_job(pid_t pid, struct job_list *job_list);
void wait_all_running_jobs(struct job_list *job_list);
void wait_terminated_background_jobs(struct job_list *job_list);
void print_job_list(struct job_list *job_list);
void free_job_list(struct job_list *job_list);  

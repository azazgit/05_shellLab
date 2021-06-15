/* 
 * tsh - A tiny shell program with job control
 * 
 * <Put your name and login ID here>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>


/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */


/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */


/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */


/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Helper functions I created. */
pid_t Fork(void);
void Sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
void Sigemptyset(sigset_t *set);
void Sigaddset(sigset_t *set, int signum); 
void Sigfillset(sigset_t * set);
pid_t Waitpid(pid_t pid, int *iptr, int options); 
pid_t jid2pid(int jid);

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);


/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);


/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
            case 'h':             /* print help message */
                usage();
	            break;
            case 'v':             /* emit additional diagnostic info */
                verbose = 1;
	            break;
            case 'p':             /* don't print a prompt */
                emit_prompt = 0;  /* handy for automatic testing */
	            break;
	        default:
                usage();
	    }
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	    /* Read command line */
	    if (emit_prompt) {
	        printf("%s", prompt);
	        fflush(stdout);
	    }
	    
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)){
	        app_error("fgets error");
        }
	
        if (feof(stdin)) { /* End of file (ctrl-d) */
	        fflush(stdout);
	        exit(0);
	    }

	    /* Evaluate the command line */
	    eval(cmdline);
	    fflush(stdout);
	    fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}


/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) {
    
    /* Parse cmd line into the arguments list for exec(). */
    char * argv[MAXARGS];
    int bg = parseline(cmdline, argv);
    
    /* Ignore empty lines. */
    if (argv[0] == NULL){return;}

    /* If not a builtin command ... */
    if (!builtin_cmd(argv)) {
        
        sigset_t mask_all, prev_all;// For masking all signals.
        sigset_t mask_chld, prev_chld;// For masking just sigchld signals.
        Sigfillset(&mask_all);
        Sigemptyset(&mask_chld);
        Sigaddset(&mask_chld, SIGCHLD);
        
        // Avoid race condition. Block sigchld signal until after addjobs(). 
        Sigprocmask(SIG_BLOCK, &mask_chld, &prev_chld);
        pid_t pid;
        if((pid = Fork()) == 0) {
            
            // Enure no signals interfere until child has created its own pgrp.
            //Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
            setpgid(0,0);
            //Sigprocmask(SIG_SETMASK, &prev_all, NULL);
            
            // Unblock SIGCHLD in child process.
            Sigprocmask(SIG_SETMASK, &prev_chld, NULL);

            /* ... and use exec func to run user's job. */ 
            if (execve(argv[0], argv, environ) < 0) {
                printf("%s: Command not found.\n", argv[0]);
                exit(1);
            }
        }
        
        // Block all signals until after jobs is updated. 
        Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
        addjob(jobs, pid, bg ? BG : FG, cmdline);
        int jid = pid2jid(pid);
        Sigprocmask(SIG_SETMASK, &prev_all, NULL);
        Sigprocmask(SIG_SETMASK, &prev_chld, NULL);
        
        /* Parent waits for foreground job to terminate. */
        if (!bg) {
            waitfg(pid);
        }
        else {
            printf("[%d] (%d) %s", jid, pid, cmdline);
        }
    }
    return;
}


/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
	    buf++;
	    delim = strchr(buf, '\'');
    }
    else {
	    delim = strchr(buf, ' ');
    }

    while (delim) {
	    argv[argc++] = buf;
	    *delim = '\0';
	    buf = delim + 1;
	
        while (*buf && (*buf == ' ')) { /* ignore spaces */
	       buf++;
        }

	    if (*buf == '\'') {
	        buf++;
	        delim = strchr(buf, '\'');
	    }
	    else {
	        delim = strchr(buf, ' ');
	    }
    }
    
    argv[argc] = NULL;
    
    if (argc == 0) {  /* ignore blank line */
	    return 1;
    }

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	    argv[--argc] = NULL;
    }
    
    return bg;
}


/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) {
    
    if (!strcmp(argv[0], "quit")){ /* quit command */
        exit(0);
    }
    
    else if (!strcmp(argv[0], "jobs")){ // jobs command.
        // Block all signals while working on global var jobs.
        sigset_t mask, prev;
        Sigprocmask(SIG_BLOCK, &mask, &prev);
        listjobs(jobs);
        Sigprocmask(SIG_SETMASK, &prev, NULL);
        return 1;
    }

    // fg or bg command.
    else if ((!strcmp(argv[0], "fg")) || (!strcmp(argv[0], "bg"))){
        do_bgfg(argv);
        return 1;
    }
    
    return 0; // Not a builtin command 
}


/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) {
    
    sigset_t mask_all, prev_all;
    Sigfillset(&mask_all);
    
    struct job_t * thisJob; 
    int jid;
    pid_t pid;
    
    // =========== Check argv[1] for valid pid/jid. ==========
    if (!argv[1]) {
        printf("%s command requires PID or %%jobid argument\n", argv[0]);
        return;
    }
    
    char * argv1ptr = argv[1];
    Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
    
    // Arg 2 is a jid.
    if (*argv1ptr == '%') {
        
        // Check that jid exists in jobs list.
        jid = atoi(++argv1ptr);    
        thisJob = getjobjid(jobs, jid);
        if (!thisJob) { // jid not valid.
            printf("%s: No such job\n", argv[1]);
            Sigprocmask(SIG_SETMASK, &prev_all, NULL);
            return;
        }
        pid = thisJob->pid; // Needed for kill() later.
    } 

    // Arg 2 is pid.
    else if (isdigit(*argv1ptr)) { // pid starts with 1-9.
        
        // Check that pid exists in jobs list.
        pid = atoi(argv[1]);

        //Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
        thisJob = getjobpid(jobs, pid);
        if(!thisJob) { // pid not valid.
             printf("(%s): No such process\n", argv[1]);
             Sigprocmask(SIG_SETMASK, &prev_all, NULL);
             return;
        }
        //Sigprocmask(SIG_SETMASK, &prev_all, NULL);
    }
    // Otherwise, arg 2 is neither a pid or jid.
    else {
        printf("%s: argument must be a PID or %%jobid\n", argv[0]);
        Sigprocmask(SIG_SETMASK, &prev_all, NULL);
        return;
    }
    // =========== END: Check argv[1] for valid pid/jid.    ==========

    /* =========== Update jobs based on fg / bg job.        ==========
     Implementation notes:
        argv[0]     |           fg              |   bg      
     ---------------|---------------------------|------------
     thisjob->state |                           |
            FG      |         error_fg          |   error_fg
            BG      |       update_bg2fg        | update_none
            ST      |       update_st2fg        | update_st2bg
            UNDEF   |   error_fg & error_undef  | error_undef

    error_fg:       If there is a fg job running tsh must wait for it to 
                    finish. If tsh read an instruction from shell, then an 
                    error must have occured. Either there is inconsistent data 
                    in jobs, or waitfg() is not waiting for fg job to finish.

    update_bg2fg:   Run bg job in fg. Change its state to FG. Must use waitfg() 
                    here because logic goes: eval() -> builtin_cmd -> do_fgbg 
                    and never reaches waitfg() in eval for builtin commands. 
    
    update_st2fg:   Run stopped job in fg. Change its state to FG.
                    Send SIGCONT signal to job's process group so all its child
                    processes receive the signal as well and run.
                    As above, use waitfg().

    update_none:    Nothing needs to be updated. This job was already running 
                    in bg when tsh processed instruction to run it in bg.
    
    update_st2bg:   run stopped job in bg. Change its state to BG. As above 
                    send SIGCONT to job's process group.

    error_undef:    An existing job must not have UNDEF state.
    */ 

    if (strcmp(argv[0], "bg")) { //fg job.
        
        switch(thisJob->state) {
            case FG:
                unix_error("tsh not waiting for existing FG job to end.\n");
                break;
            case BG:
                //Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
                thisJob->state = FG;
                Sigprocmask(SIG_SETMASK, &prev_all, NULL);
                waitfg(pid); /* Parent waits for fg job to terminate. */
                break;
            case ST:
                //Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
                thisJob->state = FG;
                kill(-pid, SIGCONT);       
                Sigprocmask(SIG_SETMASK, &prev_all, NULL);
                waitfg(pid); /* Parent waits for fg job to terminate. */
                break;
            case UNDEF:
                unix_error("Existing job with UNDEF state.\n");
                break;
            default:
                unix_error("Inconsistent state in job");
                break;
        }
    }
    else { // bg job.
        switch(thisJob->state) {
            case FG:
                unix_error("tsh not waiting for existing FG job to end.\n");
                break;
            case BG:
                break;
            case ST:
                //Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
                thisJob->state = BG;
                kill(-pid, SIGCONT);
                printf("[%d] (%d) %s", thisJob->jid, thisJob->pid, thisJob->cmdline);
                //Sigprocmask(SIG_SETMASK, &prev_all, NULL);
                break;
            case UNDEF:
                unix_error("Existing job with UNDEF state.\n");
                break;
            default:
                unix_error("Inconsistent state in job.");
                break;
        }
    }
    Sigprocmask(SIG_SETMASK, &prev_all, NULL);
    return;
}


/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid) { 
    
    struct job_t * job = getjobpid(jobs, pid);
    // Ensure job exists and has not been deleted already.
    // This can happen if sigchld kicks in before parent starts
    // waiting for child to finish.
    if (job) {
        while (job->state == FG) {
            sleep(1);
        }
    }
    return;
}


/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) {

    int olderrno = errno;
    
    sigset_t mask_all, prev_all;
    Sigfillset(&mask_all);

    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
        
        
        if (WIFEXITED(status)) {
            deletejob(jobs, pid);
            // add verbose print here.
        }

        // if ctrl-c: SIGINT killed the child.
        else if (WIFSIGNALED(status)) {
            int jid = pid2jid(pid);
            deletejob(jobs, pid);
            
            //add verbose print here.
            // Job [1] (684115) terminated by signal 2
            printf("Job [%d] (%d) terminated by signal %d\n", jid, pid, sig);
        }

        // if ctrl-z: SIGTSTP OR SIGSTOP stopped the child.
        else if (WIFSTOPPED(status)) {
            
            // Update fg job's status to ST [stopped]. 
            pid_t pid = fgpid(jobs);
            struct job_t * job;
            job = getjobpid(jobs, pid);
            job->state = ST;
            
            // Job [2] (684321) stopped by signal 20
            int jid = pid2jid(pid);
            printf("Job [%d] (%d) stopped by signal %d\n", jid, pid, sig);
        }

        Sigprocmask(SIG_SETMASK, &prev_all, NULL);
    }
    if(errno != ECHILD) {
        printf("waitpid error.");
    }

    errno = olderrno;
    
    /*
    int olderrno = errno;
    sigset_t mask_all, prev_all;
    Sigfillset(&mask_all);
    
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
        if (WIFSIGNALED(status)) {
            printf("sigint signal\n");
        }
        deletejob(jobs, pid);
        Sigprocmask(SIG_SETMASK, &prev_all, NULL);
    }
    errno = olderrno;
    */
    /* Below code works.
    int olderrno = errno;
    sigset_t mask_all, prev_all;
    Sigfillset(&mask_all);
    
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
        deletejob(jobs, pid);
        Sigprocmask(SIG_SETMASK, &prev_all, NULL);
    }
    errno = olderrno;*/
}


/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) {

    int olderrno = errno;
    
    sigset_t mask_all, prev_all;
    Sigfillset(&mask_all);
    Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
    
    // Send SIGINT sig to fg job. Job's deleted when parent handles sigchld.
    pid_t pid = fgpid(jobs);
    if (pid) { // if pid = 0, then there is no fg job. Ignore.
        kill(-pid, SIGINT);
    }
    
    Sigprocmask(SIG_SETMASK, &prev_all, NULL);
    errno = olderrno;
    return;
}


/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) {

    int olderrno = errno;
    
    sigset_t mask_all, prev_all;
    Sigfillset(&mask_all);
    Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
    
    // Send SIGTSTP signal to process group of fg job. 
    pid_t pid = fgpid(jobs);
    if (pid) {
        kill(-pid, SIGTSTP);
    }

    Sigprocmask(SIG_SETMASK, &prev_all, NULL);
    errno = olderrno;
    return;
}

/*********************
 * End signal handlers
 *********************/


/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}


/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}


/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) {
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++) {
	    if (jobs[i].jid > max) {
	        max = jobs[i].jid;
        }
    }
    return max;
}


/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) {
    
    if (pid < 1) {return 0;}
    
    int i;
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}


/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}


/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}


/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}


/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}


/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}


/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG: 
		    printf("Running ");
		    break;
		case FG: 
		    printf("Foreground ");
		    break;
		case ST: 
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ", 
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}

/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}


/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}


/* 
 * fork() with error handling, as per the book.
 */
pid_t Fork(void){
    pid_t pid;
    if((pid = fork()) < 0)
        unix_error("Fork error");
    return pid;
}


/*
 * sigprocmask() with error handling, as per the book.
 */
void Sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    if (sigprocmask(how, set, oldset) < 0) {
	    unix_error("Sigprocmask error");
    }
    return;
}


/*
 * sigaddset() with error handling, as per the book.
 */
void Sigaddset(sigset_t *set, int signum) {
    if (sigaddset(set, signum) < 0) {
	    unix_error("Sigaddset error");
    }
    return;
}


/*
 * sigemptyset() with error handling, as per the book.
 */
void Sigemptyset(sigset_t *set) {
    if (sigemptyset(set) < 0) {
	    unix_error("Sigemptyset error");
    }
    return;
}


/*
 * sigfillset() with error handling, as per the book.
 */
void Sigfillset(sigset_t * set) {
    if(sigfillset(set) < 0) {
        unix_error("Sigfillset error");
    }
}


/*
 * waitpid() with error handling, as per the book.
 */
pid_t Waitpid(pid_t pid, int *iptr, int options) {
    
    pid_t retpid;

    if ((retpid  = waitpid(pid, iptr, options)) < 0) {
	    unix_error("Waitpid error");
    }
    
    return(retpid);
}

/* jid2pid - Map job ID to process ID. */
pid_t jid2pid(int jid) {
    
    if (jid < 1) {return 0;}
    
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].jid == jid) {
            return jobs[i].pid;
        }
    }
    return 0;
}


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAXLINE    1024   /* max line size */

int parseline(const char *cmdline, char **argv) {
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



void hand_main(int sig) {
    
    if (sig == SIGINT) {
        printf("process %d in main received signal: %d\n", getpid(), sig);
        return;//exit(0);
    }

    else if (sig == SIGTSTP) {
        printf("process %d in main received signal: %d\n", getpid(), sig);
        return;
    }

    else if (sig == SIGCHLD) {
        printf("process %d in main received signal: %d\n", getpid(), sig);
        return;
    }
}

int sigint_count = 0;
extern char **environ;

void sigchld_handler(int sig) {

    int status;
    int pid;
    //pid = waitpid(-1, &status, WUNTRACED);
    
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
    printf("reaped child %d\n", pid);
    
    printf("%d%s%s%s%s\n", pid, 
            WIFEXITED(status) ? " NORMAL" : "", 
            WIFCONTINUED(status) ? " CONTINUED" : "", 
            WIFSIGNALED(status) ? " SIGNALED" : "", 
            WIFSTOPPED(status) ? " STOPPED" : "");

    // if ctrl-c SIGINT
    if (WIFSIGNALED(status)) { 
        printf("child %d, exit status %d\n", pid, WTERMSIG(status));
        printf("sigint count: %d\n", ++sigint_count);
    }

    // if ctrl-z SIGTSTP OR SIGSTOP
    if (WIFSTOPPED(status)) { 
        printf("child %d, exit status %d\n", pid, WSTOPSIG(status));
    }
    
    // Normal exit or return.
    if (WIFEXITED(status)) {
        printf("child %d, exit status %d\n", pid, WEXITSTATUS(status));
    }
    }
}


int main(int argc, char **argv) {
    char cmdline[100];
    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)){
	    printf("fgets error");
    }
    char * arg[100];
    int bg = parseline(cmdline, arg);
    
    signal(SIGCHLD, sigchld_handler);
    printf("In main, pid: %d, pgrp: %d\n", getpid(), getpgrp());
    
    int pid;
    if ((pid = fork()) == 0) {
        printf("In child with pid: %d, pgrp: %d\n", getpid(), getpgrp());    
        if(execve(arg[0], arg, environ) < 0) {
            printf("%s, Command not found\n", argv[0]);
        }
        exit(0);
    }
    
    wait(NULL);
    write(1, "main exiting\n", 15);
    exit(0);
}

/*

    sigset_t mask_all, prev_all; 
    sigfillset(&mask_all); 
    sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
    printf("main about to exit...\n");
    sigprocmask(SIG_SETMASK, &prev_all, NULL);
   */ 

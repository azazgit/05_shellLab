// stop.c: Sleeps for <n> seconds and sends SIGTSTP to itself.
// ./stop <n>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

int main(int argc, char **argv) {
    if (argc != 2) {
	    fprintf(stderr, "Usage: %s <n>\n", argv[0]);
	    exit(0);
    }
    
    int i, secs;
    secs = atoi(argv[1]);
    for (i=0; i < secs; i++) {sleep(1);}
	
    pid_t pid = getpid(); 
    if (kill(-pid, SIGTSTP) < 0){
       fprintf(stderr, "kill (tstp) error");
    }

    exit(0);
}

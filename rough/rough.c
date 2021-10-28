#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#define MAXLINE 1024
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


int main(int argc, char **argv) {
    char cmdline[100];
    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)){
	    printf("fgets error");
    }
    char * arg[100];
    int bg = parseline(cmdline, arg);

    //int BG = 2; int FG = 1;
    printf("arg[0]: %s, bg: %d\n", arg[0], bg);

    int fgORbg = strcmp(arg[0], "bg") ? 0 : 1;
    printf("bg: %d\n", fgORbg);

} 

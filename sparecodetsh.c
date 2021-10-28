
void eval(char *cmdline) {
    
    // Parse cmd line into the arguments list for exec().
    char * argv[MAXARGS];
    int bg = parseline(cmdline, argv);
    
    if (argv[0] == NULL){return;}// Ignore empty lines.

    if (!builtin_cmd(argv)) {// Not a builtin command.
        
        /*sigset_t mask_all, prev_all;// For masking all signals.
        sigset_t mask_chld, prev_chld;// For masking just sigchld signals.
        Sigfillset(&mask_all);
        Sigemptyset(&mask_chld);
        Sigaddset(&mask_chld, SIGCHLD);*/
        
        // Avoid race condition. Block sigchld signal until after addjobs(). 
        //Sigprocmask(SIG_BLOCK, &mask_chld, &prev_chld);
        pid_t pid;
        if((pid = Fork()) == 0) {
            
            // Enure no signals interfere until child has created its own pgrp.
            //Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
            setpgid(0,0);
            //Sigprocmask(SIG_SETMASK, &prev_all, NULL);
        
            // Unblock SIGCHLD in child process.
            //Sigprocmask(SIG_SETMASK, &prev_chld, NULL);

            // Use exec to run the job typed in the tsh shell by user.
            if (execve(argv[0], argv, environ) < 0) {
                printf("%s: Command not found.\n", argv[0]);
                exit(1);
            }
        }
        
        // Block all signals until after jobs is updated. 
        //Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
        addjob(jobs, pid, bg ? BG : FG, cmdline);
        int jid = pid2jid(pid);
        //Sigprocmask(SIG_SETMASK, &prev_all, NULL);
        //Sigprocmask(SIG_SETMASK, &prev_chld, NULL);
        
        // Parent waits for foreground job to terminate.
        if (!bg) {
            waitfg(pid);
        }
        else {
            printf("[%d] (%d) %s", jid, pid, cmdline);
        }
    }
    return;
}

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#define BG 111
#define PIPE 222

// initialization and setup for process_arglist
int prepare(void){

    // ignore SIGINT
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = SA_RESTART;

    if( 0 != sigaction(SIGINT, &sa, NULL) ){
        fprintf(stderr, "Signal handle registration failed. %s\n", strerror(errno));
        return 1;
    }

    return 0;
}

int finalize(void){
    return 0;
}

// default handler - SIGINT
int dfl_sigint(){
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sa.sa_flags = SA_RESTART;

    if( 0 != sigaction(SIGINT, &sa, NULL) ){
        fprintf(stderr, "Signal handle registration failed. %s\n", strerror(errno));
        return 1;
    }

    return 0;
}

/*
SIGCHLD handler for getting signals from background processes that died
The handler is set only for a process with a bg child process
 */
void sigchld_handler(int signum, siginfo_t *info, void *ptr){
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

int run_cmd_bg(int count, char** arglist) {
    arglist[count - 1] = NULL;

    pid_t pid = fork();

    if(pid < 0){
        fprintf(stderr, "fork failed. Error: %s\n", strerror(errno));
        return 1;
    }

    // child
    if(pid == 0){
        if(execvp(arglist[0], arglist) == -1){
            fprintf(stderr, "execvp failed. Error: %s\n", strerror(errno));
            exit(1);
        }
    }

    //parent
    else{

        // handler for reaping the bg child process
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = sigchld_handler;
        sa.sa_flags = SA_RESTART;

        if(sigaction(SIGCHLD, &sa, NULL) != 0){
            fprintf(stderr, "sigchld_handler registration failed. Error: %s\n", strerror(errno));
            exit(1);
        }
    }

    return 0;
}

// Returns the index of the pip char in arglist
int find_pipe(int count, char** arglist){
    int ind = 0;
    for(int i = 0; i < count; i++){
        if(strcmp(arglist[i], "|") == 0){
            ind = i;
            break;
        }
    }
    return ind;
}

int run_cmd_pipe(int count, char** arglist) {
    int pipe_ind = find_pipe(count, arglist);
    arglist[pipe_ind] = NULL;

    int fds[2];
    if (pipe(fds) == -1) {
        fprintf(stderr, "pipe failed. Error: %s\n", strerror(errno));
        return 1;
    }

    pid_t pid_child1 = fork();

    if(pid_child1 < 0){
        fprintf(stderr, "fork failed. Error: %s\n", strerror(errno));
        close(fds[0]);
        close(fds[1]);
        return 1;
    }

    // child 1 - writer
    if (pid_child1 == 0) {

        // cancel ignoring SIGINT
        if(dfl_sigint())
            exit(1);

        // redirect STDOUT -> fds[1]
        if(dup2(fds[1], STDOUT_FILENO) == -1 && errno != EINTR){
            fprintf(stderr, "dup2 failed. Error: %s\n", strerror(errno));
            exit(1);
        }

        close(fds[0]);
        close(fds[1]);

        if(execvp(arglist[0], arglist) == -1){
            fprintf(stderr, "execvp failed. Error: %s\n", strerror(errno));
            exit(1);
        }
    }

    // parent
    else {
        pid_t pid_child2 = fork();

        if(pid_child2 < 0){
            fprintf(stderr, "fork failed. Error: %s\n", strerror(errno));
            close(fds[0]);
            close(fds[1]);
            return 1;
        }

        // child 2 - reader
        if(pid_child2 == 0){

            // cancel ignoring SIGINT
            if(dfl_sigint())
                exit(1);

            // redirect STDIN -> fds[0]
            if(dup2(fds[0], STDIN_FILENO) == -1 && errno != EINTR){
                fprintf(stderr, "dup2 failed. Error: %s\n", strerror(errno));
                exit(1);
            }

            close(fds[1]);
            close(fds[0]);

            if(execvp(arglist[pipe_ind + 1], arglist + pipe_ind + 1) == -1){
                fprintf(stderr, "execvp failed. Error: %s\n", strerror(errno));
                exit(1);
            }
        }

        // parent
        else{
            close(fds[0]);
            close(fds[1]);

            waitpid(pid_child1, NULL, 0);
            waitpid(pid_child2, NULL, 0);
        }
    }

    return 0;
}

int run_cmd_fg(char** arglist){
    pid_t pid = fork();

    if(pid < 0){
        fprintf(stderr, "fork failed. Error: %s\n", strerror(errno));
        return 1;
    }

    // child process
    if(pid == 0){

        // cancel ignoring SIGINT
        if(dfl_sigint())
            exit(1);

        if(execvp(arglist[0], arglist) == -1){
            fprintf(stderr, "execvp failed. Error: %s\n", strerror(errno));
            exit(1);
        }
    }

    // parent process
    else{
        waitpid(pid, NULL, 0);
    }

    return 0;
}

// Returns in which state the command will be executed: background, foreground, pipe
int get_state(int count, char** arglist){
    if(strcmp(arglist[count - 1], "&") == 0)
        return BG;

    for(int i = 0; i < count; i++){
        if(strcmp(arglist[i], "|") == 0)
            return PIPE;
    }

    return 0;
}

int process_arglist(int count, char** arglist){
    int exit_code;
    int state = get_state(count, arglist);

    if(state == BG){
        exit_code = run_cmd_bg(count, arglist);
    }

    else if(state == PIPE) {
        exit_code = run_cmd_pipe(count, arglist);
    }

    else {
        exit_code = run_cmd_fg(arglist);
    }

    if(exit_code != 0)
        return 0;
    else
        return 1;
}

// gcc -O3 -D_POSIX_C-SOURCE=200809 -Wall -std=c11 shell.c myshell.c
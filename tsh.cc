// 
// tsh - A tiny shell program with job control
// 
// Jacob Christiansen - jach7037
//

using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>

#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"

//
// Needed global variable definitions
//

static char prompt[] = "tsh> ";
int verbose = 0;

//
// You need to implement the functions eval, builtin_cmd, do_bgfg,
// waitfg, sigchld_handler, sigstp_handler, sigint_handler
//
// The code below provides the "prototypes" for those functions
// so that earlier code can refer to them. You need to fill in the
// function bodies below.
// 

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

//
// main - The shell's main routine 
//
int main(int argc, char **argv){
  int emit_prompt = 1; // emit prompt (default)

  //
  // Redirect stderr to stdout (so that driver will get all output
  // on the pipe connected to stdout)
  //
  dup2(1, 2);

  /* Parse the command line */
  char c;
  while((c = getopt(argc, argv, "hvp")) != EOF){
    switch (c) {
    case 'h':             // print help message
      usage();
      break;
    case 'v':             // emit additional diagnostic info
      verbose = 1;
      break;
    case 'p':             // don't print a prompt
      emit_prompt = 0;  // handy for automatic testing
      break;
    default:
      usage();
    }
  }

  //
  // Install the signal handlers
  //

  //
  // These are the ones you will need to implement
  //
  Signal(SIGINT,  sigint_handler);   // ctrl-c
  Signal(SIGTSTP, sigtstp_handler);  // ctrl-z
  Signal(SIGCHLD, sigchld_handler);  // Terminated or stopped child

  //
  // This one provides a clean way to kill the shell
  //
  Signal(SIGQUIT, sigquit_handler); 

  //
  // Initialize the job list
  //
  initjobs(jobs);

  //
  // Execute the shell's read/eval loop
  //
  for(;;){
    //
    // Read command line
    //
    if(emit_prompt){
      printf("%s", prompt);
      fflush(stdout);
    }

    char cmdline[MAXLINE];

    if((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)){
      app_error("fgets error");
    }
    //
    // End of file? (did user type ctrl-d?)
    //
    if (feof(stdin)){
      fflush(stdout);
      exit(0);
    }

    //
    // Evaluate command line
    //
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  } 

  exit(0); //control never reaches here
}
  
/////////////////////////////////////////////////////////////////////////////
//
// eval - Evaluate the command line that the user has just typed in
// 
// If the user has requested a built-in command (quit, jobs, bg or fg)
// then execute it immediately. Otherwise, fork a child process and
// run the job in the context of the child. If the job is running in
// the foreground, wait for it to terminate and then return.  Note:
// each child process must have a unique process group ID so that our
// background children don't receive SIGINT (SIGTSTP) from the kernel
// when we type ctrl-c (ctrl-z) at the keyboard.
//
void eval(char *cmdline){
  /* Parse command line */
  //
  // The 'argv' vector is filled in by the parseline
  // routine below. It provides the arguments needed
  // for the execve() routine, which you'll need to
  // use below to launch a process.
  //
  char *argv[MAXARGS]; //command line argument that gets sent to you
  pid_t pid;  //saves the process ID to a var
  sigset_t mask; /*create a mask to supress output from children */

 //initailize mask
  sigemptyset(&mask);
  
  //add the SIGCHILD signal to the signal set
  sigaddset(&mask, SIGCHLD);

  //
  // The 'bg' variable is TRUE if the job should run
  // in background mode or FALSE if it should run in FG
  //
  int bg = parseline(cmdline, argv); 
  if (argv[0] == NULL){
    return; /* ignore empty lines */
  }


  if(!builtin_cmd(argv)){ //checking if the command line is a built in function. if not a command then fork

  //Parent will block SIGCHLD signal temporarily
  sigprocmask(SIG_BLOCK, &mask, 0);
                          // IF PID IS A 0, IT'S A CHILD, SET IT IN THE SAME PLACE AS PARENT, UNBLOCKS THE CHILD
    if( (pid = fork()) <= 0) { //create a child and run process in this if-statement
      setpgid(0,0);  // creates process group, want all processes to run in one shell (don't want multiple shells)
      sigprocmask(SIG_UNBLOCK, &mask, 0); //(manually unblock child process)

      if(execve(argv[0], argv, environ) < 0){ //handles if the command is valid or not
        printf("%s: Command not found. \n", argv[0]);
        exit(0);
      }
    }
//checking if in background, if not then in fg
    // Parent will wait for fg job to terminate 
    if(!bg){ // if in FG:
      addjob(jobs, pid, FG, cmdline);//add job
      sigprocmask(SIG_UNBLOCK, &mask, 0);//unblocking the child signal
      waitfg(pid);//waiting for foreground to finish

    }
    else { //if the process is in background
      addjob(jobs, pid, BG, cmdline); //add job
      printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline); //formatting
      sigprocmask(SIG_UNBLOCK, &mask, 0);//unblocking the child signal
    }
  }
 
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is a C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need 
// to use the argv array as well to look for a job number.
//
int builtin_cmd(char **argv){
  string cmd(argv[0]);
//MY CODE START
  if(cmd == "quit"){  //quit the command
    exit(0);
  }
  if(cmd == "&"){ //ignore background process with no argument
    return 1;
  }
  if(cmd == "jobs"){ //list jobs
    listjobs(jobs);
    return 1;
  }
  if(cmd == "bg" || cmd == "fg"){ //set to background or foreground
    do_bgfg(argv);
    return 1;
  }

  return 0;
}

/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the built-in bg and fg commands
//
void do_bgfg(char **argv) //execute process as background or foreground
{
  struct job_t *jobp=NULL;
    
  //Ignore command if no arg
  if (argv[1] == NULL){
    printf("%s command requires PID or %%jobid argument\n", argv[0]);
    return;
  }
    
  //Parse the necesary PID or %JID arg
  if (isdigit(argv[1][0])){
    pid_t pid = atoi(argv[1]);
    if (!(jobp = getjobpid(jobs, pid))){
      printf("(%d): No such process\n", pid);
      return;
    }
  }
  else if (argv[1][0] == '%'){
    int jid = atoi(&argv[1][1]);
    if (!(jobp = getjobjid(jobs, jid))){
      printf("%s: No such job\n", argv[1]);
      return;
    }
  }
  else{
    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
    return;
  }
  string cmd(argv[0]);

//MY CODE START
  if(cmd == "bg"){ //if command line is background process
    if(kill(-(jobp->pid), SIGCONT)); //labelling process that it needs to be killed bc it has completed
    jobp->state = BG; // change job state to bg
    printf("[%d] (%d) %s", (jobp->jid), (jobp->pid), (jobp->cmdline)); //print info
    //don't wait b/c job is in bg.
  }
  else if (cmd == "fg"){ //if command line is foreground process
    if(kill(-(jobp->pid), SIGCONT)); //labelling process that it needs to be killed bc it has completed
    jobp->state = FG; //change job state to the foreground
    waitfg(jobp->pid);  //wait for said job since now it's in foreground.
  }
  else{
  }

  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid){ //makes it sleep (wait)
  while(pid == fgpid(jobs)){
    sleep(1);
  }
}

/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers
//


/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.  
//
void sigchld_handler(int sig){//different cases of child signals 
    int status;
    pid_t pid;

  while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED))>0){


  if (WIFEXITED(status)){   //checks if child terminated normally
      deletejob(jobs, pid); //then "delete"
  }

  if (WIFSIGNALED(status)){  //checks if child was terminated by a signal that was not caught cntlc
      printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid), pid, WTERMSIG(status));
      deletejob(jobs,pid); //then delete, say that it was terminated by signal
  }

  if (WIFSTOPPED(status)){    //checks if child process is paused, switch to stopped cntlz
      getjobpid(jobs, pid)->state = ST;
      printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(pid), pid, WSTOPSIG(status) );
  
  }
    }
    
    return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.  
//
void sigint_handler(int sig){
  pid_t pid = fgpid(jobs);

    if (pid!=0){ //if it's not a child
      if(kill(-(pid), SIGINT)){ //then kill it
      }
    }
    
    return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.  
//
void sigtstp_handler(int sig){
  pid_t pid = fgpid(jobs);

  if(pid!=0){ //if it's not a child
    if(kill(-pid, SIGTSTP)); //then stop it
  }

  return;
}

/*********************
 * End signal handlers
 *********************/
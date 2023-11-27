//--------------------------------------------------------------------------------------------------
// System Programming                        Shell Lab                                   Fall 2021
//
/// @file
/// @brief csapsh- a tiny shell with job control
/// @author CS:APP & CSAP lab
/// @section changelog Change Log
/// 2020/11/14 Bernhard Egger adapted from CS:APP lab
/// 2021/11/03 Bernhard Egger improved for 2021 class
///
/// @section license_section License
/// Copyright CS:APP authors
/// Copyright (c) 2020-2021, Computer Systems and Platforms Laboratory, SNU
/// All rights reserved.
///
/// Redistribution and use in source and binary forms, with or without modification, are permitted
/// provided that the following conditions are met:
///
/// - Redistributions of source code must retain the above copyright notice, this list of condi-
///   tions and the following disclaimer.
/// - Redistributions in binary form must reproduce the above copyright notice, this list of condi-
///   tions and the following disclaimer in the documentation and/or other materials provided with
///   the distribution.
///
/// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
/// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED  TO, THE IMPLIED  WARRANTIES OF MERCHANTABILITY
/// AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
/// CONTRIBUTORS  BE LIABLE FOR ANY DIRECT,  INDIRECT, INCIDENTAL, SPECIAL,  EXEMPLARY,  OR CONSE-
/// QUENTIAL DAMAGES  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
/// LOSS OF USE, DATA,  OR PROFITS; OR BUSINESS INTERRUPTION)  HOWEVER CAUSED AND ON ANY THEORY OF
/// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
/// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
/// DAMAGE.
//--------------------------------------------------------------------------------------------------

#define _GNU_SOURCE          // to get basename() in string.h
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

//--------------------------------------------------------------------------------------------------
// Limits and constant definitions
//
#define MAXLINE    1024      ///< max. length of command line
#define MAXJOBS      16      ///< max. number of jobs at any point in time

/// @name job states
/// @{

/// @brief Jobs states: FG (foreground), BG (background), ST (stopped)
/// Job state transitions and enabling actions:
///   FG -> ST    : ctrl-z or SIGSTOP
///   ST -> FG    : fg command
///   ST -> BG    : bg command
///   BG -> FG    : fg command
///   FG -> UNDEF : ctrl-c or signal terminating job
///
/// At most one job can be in FG state at any given time.
#define UNDEF 0              ///< undefined
#define FG 1                 ///< running in foreground
#define BG 2                 ///< running in background
#define ST 3                 ///< stopped
/// @}

#define READ  0             ///< pip read end
#define WRITE 1             ///< pipe write end


//--------------------------------------------------------------------------------------------------
// Data types
//

/// @brief Job struct containing information about a job
typedef struct job_t {
  pid_t pid;                 ///< group ID of process group. GID must be PID of last process in pipe
  int jid;                   ///< job ID [ 1, 2, ..., MAXJOBS ]
  int state;                 ///< job state (UNDEF, BG, FG, or ST)
  char cmdline[MAXLINE];     ///< command line
} Job;


//--------------------------------------------------------------------------------------------------
// Global variables
//

char prompt[] = "csapsh> ";  ///< command line prompt (DO NOT CHANGE)
int emit_prompt = 1;         ///< 1: emit prompt; 0: do not emit prompt
int verbose = 0;             ///< 1: verbose mode; 0: normal mode
int nextjid = 1;             ///< next job ID to allocate

Job jobs[MAXJOBS];           ///< the job list


//--------------------------------------------------------------------------------------------------
// Functions that you need to implement
//

void eval(char *cmdline);
int builtin_cmd(char *argv[]);
void do_bgfg(char *argv[]);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigint_handler(int sig);
void sigtstp_handler(int sig);


//--------------------------------------------------------------------------------------------------
// Implemented functions - do not modify
//

// Parse command line and build the command array
int parseline(const char *cmdline, char ****argv, char **outfile);

// Job list manipulation functions
void clearjob(Job *job);
void initjobs(Job *jobs);
int maxjid(Job *jobs);
int addjob(Job *jobs, pid_t pid, int state, char *cmdline);
int deletejob(Job *jobs, pid_t pid);
pid_t fgpid(Job *jobs);
Job *getjobpid(Job *jobs, pid_t pid);
Job *getjobjid(Job *jobs, int jid);
int pid2jid(pid_t pid);
void listjobs(Job *jobs);

// Helper functions
void usage(const char *program);
void unix_error(char *msg);
void app_error(char *msg);
void Signal(int signum, void (*handler)(int));
void sigquit_handler(int sig);
char* stripnewline(char *str);


#define VERBOSE(...)  if (verbose) { fprintf(stderr, ##__VA_ARGS__); fprintf(stderr, "\n"); }


/// @brief Program entry point.
int main(int argc, char **argv)
{
  char c;
  char cmdline[MAXLINE];

  // redirect stderr to stdout so that the driver will get all output 
  // on the pipe connected to stdout.
  dup2(STDOUT_FILENO, STDERR_FILENO);

  // parse command line
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
      case 'h': usage(argv[0]);   // print help message
                break;
      case 'v': verbose = 1;      // emit additional diagnostic info
                break;
      case 'p': emit_prompt = 0;  // don't print a prompt
                break;            // handy for automatic testing
      default:  usage(argv[0]);   // invalid option -> print help message
    }
  }

  // install signal handlers
  VERBOSE("Installing signal handlers...");
  Signal(SIGINT,  sigint_handler);    // Ctrl-c
  Signal(SIGTSTP, sigtstp_handler);   // Ctrl-z
  Signal(SIGCHLD, sigchld_handler);   // Terminated or stopped child
  Signal(SIGQUIT, sigquit_handler);   // Ctrl-Backslash (useful to exit shell)

  // initialize job list
  initjobs(jobs);

  // execute read/eval loop
  VERBOSE("Execute read/eval loop...");
  while (1) {
    if (emit_prompt) { printf("%s", prompt); fflush(stdout); }

    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
      app_error("fgets error");
    }

    if (feof(stdin)) break;   // end of input (Ctrl-d)

    eval(cmdline);

    fflush(stdout);
  }

  // that's all, folks!
  return EXIT_SUCCESS;
}

/// @brief Print a parsed command structure to stdout
/// @param cmd array of char *argv[] arrays obtained by calling @a parseline.
/// @param outfile filename for output redirection or NULL
/// @param mode execution mode
void dump_cmdstruct(char ***cmd, char *outfile, int mode)
{
  if (cmd == NULL) return;

  size_t cmd_idx = 0, arg_idx;

  while (cmd[cmd_idx] != NULL) {
    printf("    argv[%lu]:\n", cmd_idx);
    arg_idx = 0;
    while (cmd[cmd_idx][arg_idx] != NULL) {
      printf("      argv[%lu][%lu] = %s\n", cmd_idx, arg_idx, cmd[cmd_idx][arg_idx]);
      arg_idx++;
    }
    cmd_idx++;
  }

  if (outfile) printf("Output redirect to %s.\n", outfile);
  printf("Command runs in %sground.\n", mode == BG ? "back" : "fore");
}

/// @brief Free a parsed command structure
/// @param cmd array of char *argv[] arrays obtained by calling @a parseline
void free_cmdstruct(char ***cmd)
{
  if (cmd == NULL) return;

  int cmd_idx = 0, arg_idx;

  while (cmd[cmd_idx] != NULL) {
    arg_idx = 0;
    while (cmd[cmd_idx][arg_idx] != NULL) free(cmd[cmd_idx][arg_idx++]);
    free(cmd[cmd_idx++]);
  }
}

/// @brief Evaluate the command line. The function @a parseline() does the heavy lifting of parsing
///        the command line and splitting it into separate char *argv[] arrays that represent
///        individual commands with their arguments. 
///        A job consists of one process or several processes connected via pipes. Optionally,
///        the output of the entire job can be saved into a file specified by outfile.
///        The shell waits for jobs that are executed in the foreground (FG), while jobs that run
///        in the background are not waited for.
///        To allow piping of built-in commands, the shell should fork itself before executing a
///        built-in command. This requires special treatement of the standalone "quit" command
///        that must not be executed in a forked shell to have the desired effect.
/// @param cmdline command line
void eval(char *cmdline)
{
  char *str = strdup(cmdline);
  VERBOSE("eval(%s)", stripnewline(str));
  free(str);

  char ***argv = NULL;
  char *outfile = NULL;

  int mode = parseline(cmdline, &argv, &outfile);
  if (mode == -1) return;      // parse error
  if (argv == NULL) return;    // no input

  pid_t pid, pgid = 0;
  sigset_t set;
  int numproc = 0, out, cmd_idx;

  while (argv[numproc] != NULL) numproc++;

  if (outfile == NULL && numproc == 1 && builtin_cmd(argv[0])) {
    waitfg(fgpid(jobs));

    free_cmdstruct(argv);
    free(argv);
    return;
  }

  if (outfile != NULL) {
    if ((out = creat(outfile, S_IRUSR | S_IRGRP | S_IWUSR | S_IWGRP)) < 0)
      unix_error("Cannot creat.");

    dup2(out, STDOUT_FILENO);
    close(out);
  }

  sigemptyset(&set);
  sigaddset(&set, SIGCHLD | SIGINT | SIGTSTP);

  sigprocmask(SIG_BLOCK, &set, NULL);

  for (cmd_idx = numproc - 1; cmd_idx >= 0; cmd_idx--) {
    int pipefd[2];

    if (cmd_idx > 0)  pipe(pipefd);

    pid = fork();

    if (pid < 0) {
      unix_error("Cannot fork.");
    } else if (pid == 0) {
      if (setpgid(0, pgid) < 0) unix_error("Cannot setpgid.");

      sigprocmask(SIG_UNBLOCK, &set, NULL);

      if (cmd_idx > 0) {
        dup2(pipefd[READ], STDIN_FILENO);
        close(pipefd[READ]);
        close(pipefd[WRITE]);
      }

      if (!builtin_cmd(argv[cmd_idx])) {
        execvp(argv[cmd_idx][0], argv[cmd_idx]);
        unix_error(NULL);
      }

      exit(EXIT_SUCCESS);
    } else {
      if (pgid == 0) { // first fork
        pgid = pid; // set process group id

        addjob(jobs, pgid, mode, cmdline);

        if (mode == BG) fprintf(stderr, "[%d] (%d) %s", 1 + (nextjid + MAXJOBS - 2) % MAXJOBS, emit_prompt ? pid : -1, cmdline);
      }

      if (cmd_idx > 0) {
        dup2(pipefd[WRITE], STDOUT_FILENO);
        close(pipefd[READ]);
        close(pipefd[WRITE]);
      } else {
        dup2(STDERR_FILENO, STDOUT_FILENO);

        sigprocmask(SIG_UNBLOCK, &set, NULL);

        waitfg(pid);

        free_cmdstruct(argv);
        free(argv);
        free(outfile);
      }
    }
  }
}

/// @brief Execute built-in commands
/// @param argv command
/// @retval 1 if the command was a built-in command
/// @retval 0 otherwise
int builtin_cmd(char *argv[])
{
  if (!strcmp(argv[0], "bg") || !strcmp(argv[0], "fg")) {
    do_bgfg(argv);
    return 1;
  } else if (!strcmp(argv[0], "quit")) {
    exit(EXIT_SUCCESS);
  } else if (!strcmp(argv[0], "jobs")) {
    listjobs(jobs);
    return 1;
  }

  return 0;
}

/// @brief Execute the builtin bg and fg commands
/// @param argv char* argv[] array where argv[0] is either "bg" or "fg"
void do_bgfg(char *argv[])
{
  Job *job = NULL;

  if (argv[1] == NULL) {
    fprintf(stdout, "%s command requires PID or %%jobid argument\n", argv[0]);
    return;
  } else if (argv[1][0] == '%') {
    long jid = strtol(argv[1]+1, NULL, 10);

    job = getjobjid(jobs, jid);

    if (job == NULL) {
      fprintf(stdout, "[%%%ld]: No such job\n", jid);
      return;
    }
  } else {
    long pid = strtol(argv[1], NULL, 10);

    job = getjobpid(jobs, pid);

    if (job == NULL) {
      fprintf(stdout, "(%ld): No such process\n", pid);
      return;
    }
  }

  kill(-(job->pid), SIGCONT);

  if (!strcmp(argv[0], "fg"))       job->state = FG;
  else if (!strcmp(argv[0], "bg")) {
    job->state = BG;
    fprintf(stderr, "[%d] (%d) %s", job->jid, emit_prompt ? job->pid : -1, job->cmdline);
  }
}

/// @brief Block until process pid is no longer the foreground process
/// @param pid PID of foreground process
void waitfg(pid_t pid)
{
  Job *job;

  while ((job = getjobpid(jobs, pid)) != NULL && job->state == FG)  sleep(1);
}


//--------------------------------------------------------------------------------------------------
// Signal handlers
//

/// @brief Print handler-level error msg based on errno
static inline void handler_error() {
  printf("%s\n", strerror(errno));
}

/// @brief SIGCHLD handler. Sent to the shell whenever a child process terminates or stops because
///        it received a SIGSTOP or SIGTSTP signal. This handler reaps all zombies.
/// @param sig signal (SIGCHLD)
void sigchld_handler(int sig)
{
  int err = errno, status;
  pid_t pid;
  Job *job;

  while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
    if (WIFEXITED(status) || WIFSIGNALED(status))
      deletejob(jobs, pid);

    else if (WIFSTOPPED(status))
      if ((job = getjobpid(jobs, pid)) != NULL)
        job->state = ST;
  }

  if (pid < 0 && errno != ECHILD)  handler_error();

  errno = err;
}

/// @brief SIGINT handler. Sent to the shell whenever the user types Ctrl-c at the keyboard.
///        Forward the signal to the foreground job.
/// @param sig signal (SIGINT)
void sigint_handler(int sig)
{
  int err = errno;
  pid_t pid = fgpid(jobs);

  if (pid > 0 && kill(-pid, SIGINT) < 0) handler_error();

  errno = err;
}

/// @brief SIGTSTP handler. Sent to the shell whenever the user types Ctrl-z at the keyboard.
///        Forward the signal to the foreground job.
/// @param sig signal (SIGTSTP)
void sigtstp_handler(int sig)
{
  int err = errno;
  pid_t pid = fgpid(jobs);

  if (pid > 0 && kill(-pid, SIGTSTP) < 0)  handler_error();

  errno = err;
}


//--------------------------------------------------------------------------------------------------
// parseline - Parse the command line and build the argv array.
//
// The syntax of a command line is as follows:
//   cmdline = command { "|" command } [ ioredir ] ["&"].
//   command = cmd { arg }.
//   ioredir = ">" filename
//
// That is, a command line consists of at least one command, follwed by 0 or more piped commands,
// and optional I/O redirection, and an optional background execution flag '&'.
//
// A command consists of the name of the executable followed by 0 or more arguments. Commands
// and arguments enclosed in quotes are treated a a single entity.
//
// Examples:
//   csapsh> ls -l /tmp
//
//   csapsh> ls -l /tmp | sort -r
//
//   csapsh> ls -l /tmp | wc > statistics.txt
//
//   csapsh> sleep 10 &
//
//   csapsh> ls -l /tmp | sort | shuf > listing.txt &
//

/// @brief Returns true if char @a c is a regular delimiter (space, tab, '|', or '>') or matches
///        the specific delimiter @a extra.
/// @param c character to examine
/// @param extra specific delimiter
/// @retval true if extra==NONE and @a c is a regular delimiter (' ', '\t', '|', '>')
/// @retval true if extra!=NONE and @a c == @a extra
/// @retval false otherwise
#define NONE '\0'
int isdelim(char c, char extra)
{
  return ((extra == NONE) && ((c == ' ') || (c == '\t') || (c == '|') || (c == '>'))) ||
         (c == extra);
}

/// @brief Skip over whitespace in string @a str starting at position @a pos. Returns the position
///        of the first non-whitespace character (or the \0 byte).
/// @param str string to search
/// @param pos starting position
/// @retval int position of first non-whitespace character or the null-byte
int skip_whitespace(const char *str, int pos)
{
  while ((str[pos] == ' ') || (str[pos] == '\t')) pos++;
  return pos;
}

/// @brief Prints an error marker at position @a pos + the length of csapsh's prompt and an error
///        message in dependence of @a error. The error codes are purposefully chosen to match
///        the mode in parseline below. Always returns -1.
/// @param cmdline command line containing the error
/// @param pos error position
/// @param error error type
/// @retval -1 Always returns -1
int parseline_error(const char *cmdline, int pos, int error)
{
  assert(pos >= 0);

  if (emit_prompt) pos+=(int)strlen(prompt);
  else printf("%s", cmdline);
  printf("%*s^\n", pos, " ");
  switch (error) {
    case 0:  printf("Command expected.\n");
             break;
    case 1:  printf("Argument expected.\n");
             break;
    case 2:  printf("Filename expected.\n");
             break;
    case 3:
    case 4:  printf("Extra input after end of command.\n");
             break;
    case 5:  printf("Quoted argument not terminated.\n");
             break;
    case 6:  printf("Out of memory.\n");
             break;
    default: printf("Invalid error code: %d\n", error);
  }

  return -1;
}

/// @brief parses a command line and splits it into separate argv arrays that can be used directly
///        with execv. The first and second level arrays are NULL terminated. The pipe character
///        ('|') separates commands. The filename following the I/O output redirection character 
///        ('>') is returned in @a outfile. Returns 0 if the command(s) are to be executed in the
///        foreground, 1 for background execution, and -1 on error.
/// @param cmdline command line to parse
/// @param[out] argv NULL-terminated array of 'char *argv[]' arrays suitable for the execv family
/// @param[out] outfile NULL or name of file to redirect stdout of (last) command to
/// @retval 0 parse successful, execute commands in foreground
/// @retval 1 parse successful, execute commands in background
/// @retval -1 invalid command line @a cmdline
int parseline(const char *cmdline, char ****argv , char **outfile)
{
  int pos = 0;   // current position in cmdline
  int bgnd = FG; // foreground/background execution flag
  int mode = 0;  // 0: argument required
                 // 1: optional arguments
                 // 2: filename required
                 // 3: end of input or background (only '&' and newline allowed)
                 // 4: end of input (only newline allowed)

  *outfile = NULL;
  char ***cmd = NULL;
  int cmd_idx = 0, cmd_max = 0;   // current & maximum index into argv
  int arg_idx = 0, arg_max = 0;   // current & maximum index into argv[cmd_idx]


  while (cmdline[pos] != '\n') {
    // skip whitespace
    pos = skip_whitespace(cmdline, pos);

    switch (cmdline[pos]) {
      case '|':
        { //
          // pipe
          //
          if (mode != 1) return parseline_error(cmdline, pos, mode);
          pos++;
          cmd_idx++; arg_idx = 0; arg_max = 0;
          mode = 0;
          break;
        }

      case '>':
        { //
          // output redirection
          //
          if (mode != 1) return parseline_error(cmdline, pos, mode);
          pos++;
          mode = 2;
          break;
        }

      case '&':
        { //
          // background
          //
          if ((mode != 1) && (mode != 3)) return parseline_error(cmdline, pos, mode);
          pos++;
          bgnd = BG;
          mode = 4;
          break;
        }

      case '\n':
        { //
          // end of input
          //
          if ((mode == 0) || (mode == 2)) return parseline_error(cmdline, pos, mode);
          break;
        }

      default:
        { //
          // command, argument, or filename
          //
          if (mode >= 3) return parseline_error(cmdline, pos, mode);

          // check for quoted arguments
          char extra = NONE;
          if ((cmdline[pos] == '\'') || (cmdline[pos] == '"')) {
            extra = cmdline[pos];
            pos++;
          }

          // find end of argument
          int astart = pos;
          while ((cmdline[pos] != '\n') && (!isdelim(cmdline[pos], extra))) pos++;
          int aend = pos;

          if (extra != NONE) {
            if (cmdline[pos] == extra) pos++;                // include closing quote
            else return parseline_error(cmdline, astart, 5); // no matching end quote found
          }

          // extract argument
          char *argument = malloc(aend-astart+1);
          strncpy(argument, &cmdline[astart], aend-astart);
          argument[aend-astart] = '\0';

          if (mode < 2) {
            // command/argument

            if (cmd_idx >= cmd_max-1) {
              // resize argv
              cmd_max += 8;
              cmd = realloc(cmd, cmd_max * sizeof(cmd[0]));
              if (cmd == NULL) return parseline_error(cmdline, pos, 6);
              for (int i=cmd_idx; i<cmd_max; i++) cmd[i] = NULL;
            }
            if (arg_idx >= arg_max-1) {
              // resize argv[cmd_idx]
              arg_max += 8;
              cmd[cmd_idx] = realloc(cmd[cmd_idx], arg_max * sizeof(cmd[0][0]));
              if (cmd[cmd_idx] == NULL) return parseline_error(cmdline, pos, 6);
              for (int i=arg_idx; i<arg_max; i++) cmd[cmd_idx][i] = NULL;
            }

            cmd[cmd_idx][arg_idx++] = argument;

            if (mode == 0) mode = 1;
          } else {
            // filename
            *outfile = argument;
            mode = 3;
          }
        }
    }
  }
  if ((cmd != NULL) && ((mode == 0) || (mode == 2))) return parseline_error(cmdline, pos, mode);

  *argv = cmd;
  return bgnd;
}


//--------------------------------------------------------------------------------------------------
// Job list manipulation functions
//

/// @brief Clear a job struct
/// @param job job struct
void clearjob(Job *job)
{
  job->pid = 0;
  job->jid = 0;
  job->state = UNDEF;
  job->cmdline[0] = '\0';
}

/// @brief Initialize the job list
/// @param jobs job list
void initjobs(Job *jobs)
{
  for (int i = 0; i < MAXJOBS; i++)
    clearjob(&jobs[i]);
}

/// @brief Returns largest allocated job ID
/// @param jobs job list
/// @retval int largest allocated job ID
int maxjid(Job *jobs)
{
  int max = 0;

  for (int i = 0; i < MAXJOBS; i++) {
    if (jobs[i].jid > max) max = jobs[i].jid;
  }

  return max;
}

/// @brief Add a job to the job list
/// @param jobs job list
/// @param pid process ID
/// @param state job state
/// @param cmdline command line
/// @retval 1 on success
/// @retval 0 on failure
int addjob(Job *jobs, pid_t pid, int state, char *cmdline)
{
  if (pid < 1) return 0;

  for (int i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid == 0) {
      jobs[i].pid = pid;
      jobs[i].state = state;
      jobs[i].jid = nextjid++;
      if (nextjid > MAXJOBS) nextjid = 1;
      strncpy(jobs[i].cmdline, cmdline, MAXLINE-1);
      jobs[i].cmdline[MAXLINE-1] = '\0';
      VERBOSE("Added job [%d] %d %s", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
      return 1;
    }
  }

  printf("Maximum number of jobs exceeded. Increase MAXJOBS and recompile shell.\n");
  return 0;
}

/// @brief Delete job with PID @a pid from the job list
/// @param jobs job list
/// @param pid process ID
/// @retval 1 on success
/// @retval 0 on failure
int deletejob(Job *jobs, pid_t pid)
{
  if (pid < 1) return 0;

  for (int i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid == pid) {
      clearjob(&jobs[i]);
      nextjid = maxjid(jobs)+1;
      return 1;
    }
  }

  return 0;
}

/// @brief Return PID of current foreground job, 0 if no such job
/// @param jobs job list
/// @retval pid of current foreground job
/// @retval 0 if no such job
pid_t fgpid(Job *jobs)
{
  for (int i = 0; i < MAXJOBS; i++) {
    if (jobs[i].state == FG) return jobs[i].pid;
  }

  return 0;
}

/// @brief Find a job by a process ID
/// @param jobs job list
/// @param pid process ID
/// @retval job_t* pointer to job struct
/// @retval NULL if no such job exists
Job* getjobpid(Job *jobs, pid_t pid)
{
  if (pid < 1) return NULL;

  for (int i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid == pid) return &jobs[i];
  }

  return NULL;
}

/// @brief Find a job by its job ID
/// @param jobs job list
/// @param jid job ID
/// @retval job_t* pointer to job struct
/// @retval NULL if no such job exists
Job* getjobjid(Job *jobs, int jid)
{
  if (jid < 1) return NULL;

  for (int i = 0; i < MAXJOBS; i++) {
    if (jobs[i].jid == jid) return &jobs[i];
  }

  return NULL;
}

/// @brief Map process ID to job ID
/// @param pid process ID
/// @retval int job ID (> 0)
/// @retval 0 if no such job exists
int pid2jid(pid_t pid)
{
  if (pid < 1) return 0;

  for (int i=0; i<MAXJOBS; i++) {
    if (jobs[i].pid == pid) return jobs[i].jid;
  }

  return 0;
}

/// @brief Print job list
/// @param jobs job list
void listjobs(Job *jobs)
{
  for (int i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid != 0) {
      printf("[%d] (%d) ", jobs[i].jid, emit_prompt ? jobs[i].pid : -1);

      switch (jobs[i].state) {
        case BG: printf("Running ");    break;
        case FG: printf("Foreground "); break;
        case ST: printf("Stopped ");    break;
        default: printf("listjobs: Internal error: job[%d].state=%d ", i, jobs[i].state);
      }

      printf("%s", jobs[i].cmdline); // cmdline includes a newline
    }
  }
}


//--------------------------------------------------------------------------------------------------
// Other helper functions
//

/// @brief Print help message. Does not return.
__attribute__((noreturn))
void usage(const char *program)
{
  printf("Usage: %s [-hvp]\n", basename(program));
  printf("   -h   print this message\n");
  printf("   -v   print additional diagnostic information\n");
  printf("   -p   do not emit a command prompt\n");
  exit(EXIT_FAILURE);
}

/// @brief Print a Unix-level error message based on errno. Does not return.
/// param msg optional additional descriptive string
__attribute__((noreturn))
void unix_error(char *msg)
{
  if (msg != NULL) fprintf(stdout, "%s: ", msg);
  fprintf(stdout, "%s\n", strerror(errno));
  exit(EXIT_FAILURE);
}

/// @brief Print an application-level error message. Does not return.
/// @param msg error message
__attribute__((noreturn))
void app_error(char *msg)
{
  fprintf(stdout, "%s\n", msg);
  exit(EXIT_FAILURE);
}

/// @brief Wrapper for sigaction(). Installs the function @a handler as the signal handler
///        for signal @a signum. Does not return on error.
/// @param signum signal number to catch
/// @param handler signal handler to invoke
void Signal(int signum, void (*handler)(int))
{
  struct sigaction action;

  action.sa_handler = handler;
  sigemptyset(&action.sa_mask); // block sigs of type being handled
  action.sa_flags = SA_RESTART; // restart syscalls if possible

  if (sigaction(signum, &action, NULL) < 0) unix_error("Sigaction");
}

/// @brief SIGQUIT handler. Terminates the shell.
__attribute__((noreturn))
void sigquit_handler(int sig)
{
  printf("Terminating after receipt of SIGQUIT signal\n");
  exit(EXIT_FAILURE);
}

/// @brief strip newlines (\n) from a string. Warning: modifies the string itself!
///        Inside the string, newlines are replaced with a space, at the end 
///        of the string, the newline is deleted.
///
/// @param str string
/// @reval char* stripped string
char* stripnewline(char *str)
{
  char *p = str;
  while (*p != '\0') {
    if (*p == '\n') *p = *(p+1) == '\0' ? '\0' : ' ';
    p++;
  }

  return str;
}

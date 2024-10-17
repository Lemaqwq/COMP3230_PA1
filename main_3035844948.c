/*
* PLEASE WRITE DOWN FOLLOWING INFO BEFORE SUBMISSION
* FILE NAME: main_3035844948.c
* NAME: CHEN Liheng
* UID: 3035844948
* Development Platform: Linux
* Remark: (How much you implemented?) All parts in the requirements have been completed
* How to compile separately: 
* gcc -o inference inference_3035844948.c -O3 -lm
* gcc -o main main_3035844948.c
*/

#include "common.h"  // common definitions

#include <stdio.h>   // for printf, fgets, scanf, perror
#include <stdlib.h>  // for exit() related
#include <unistd.h>  // for folk, exec...
#include <wait.h>    // for waitpid
#include <signal.h>  // for signal handlers and kill
#include <string.h>  // for string related 
#include <sched.h>   // for sched-related
#include <syscall.h> // for syscall interface

#define READ_END       0    // helper macro to make pipe end clear
#define WRITE_END      1    // helper macro to make pipe end clear
#define SYSCALL_FLAG   0    // flags used in syscall, set it to default 0

// Define Global Variable, Additional Header, and Functions Here
#include <errno.h>

pid_t inference_pid;  // PID of the inference process
int pipe_fd[2];      // Pipe for communication between main and inference processes
volatile sig_atomic_t generation_finished = 0;

// Handle SIGINT to terminate both processes gracefully
void sigint_handler(int sig) {
    kill(inference_pid, SIGINT);  // Terminate inference process
    int status;
    waitpid(inference_pid, &status, 0);  // Wait for process exit
    printf("[Main] Child exited with status: %d\n", WEXITSTATUS(status));
    exit(0);
}

// Handle SIGUSR1 to receive signal from the child process that one generation is done
void SIGUSR1_handler(int sig) {
    if (sig == SIGUSR1) {
        generation_finished = 1;
    }
}



// Function to collect and display process statistics
void collect_stats(pid_t pid, int reset) {

    // Define variables to store the previous utime and stime
    static unsigned long prev_utime = 0;
    static unsigned long prev_stime = 0;

    char stat_path[256];
    sprintf(stat_path, "/proc/%d/stat", pid);

    // Open the /prc/pid/stat file
    FILE *stat_file = fopen(stat_path, "r");
    if (!stat_file) {
        perror("Failed to open stat file");
        return;
    }

    
    // Read the stat file
    char buffer[1024];
    if (!fgets(buffer, sizeof(buffer), stat_file)) {
        perror("Failed to read stat file");
        fclose(stat_file);
        return;
    }
    fclose(stat_file);

    // Save buffer to monitor file
    // FILE *monitor_file = fopen("./monitor", "w");
    // if (!monitor_file) {
    //     perror("Failed to open monitor file");
    //     return;
    // }
    // fprintf(monitor_file, "%s", buffer);
    // fclose(monitor_file);


    // Parse the line
    int pid_stat;
    char tcomm[256];
    char state;
    long nice;
    unsigned int policy;
    unsigned long vsize;
    int processor;
    unsigned long utime;
    unsigned long stime;

    // Extract tcomm (process name) which is enclosed in parentheses
    char *start = strchr(buffer, '(');
    char *end = strrchr(buffer, ')');

    if (!start || !end || start >= end) {
        fprintf(stderr, "Failed to parse tcomm\n");
        return;
    }

    // Extract pid
    *start = '\0';
    sscanf(buffer, "%d", &pid_stat);
    *start = '(';  // restore

    // Extract tcomm
    int tcomm_len = end - start - 1;
    strncpy(tcomm, start + 1, tcomm_len);
    tcomm[tcomm_len] = '\0';

    // The rest of the fields
    char *rest = end + 2;  // Skip ') '

    // Tokenize the rest of the string
    char *token;
    int field_num = 3;  // Start from field 3
    char *saveptr = NULL;
    token = strtok_r(rest, " ", &saveptr);

    while (token != NULL) {
        // Extract the fields we're interested in
        switch (field_num) {
            case 3:
                state = token[0]; // Extract the state
                break;
            case 14:
                utime = strtoul(token, NULL, 10); // Extract utime
                break;
            case 15:
                stime = strtoul(token, NULL, 10); // Extract stime
                break;
            case 19:
                nice = atol(token); // Extract nice value
                break;
            case 23:
                vsize = strtoul(token, NULL, 10); // Extract vsize
                break;
            case 39:
                processor = atoi(token); // Extract task_cpu
                break;
            case 41:
                policy = (unsigned int)atoi(token); // Extract policy
                break;
            default:
                break;
        }

        field_num++;
        token = strtok_r(NULL, " ", &saveptr);
    }



    // Handle reset
    if (reset) {
        prev_utime = utime;
        prev_stime = stime;
        return;
    }

    // Calculate CPU usage
    unsigned long utime_diff = utime - prev_utime;
    unsigned long stime_diff = stime - prev_stime;
    double cpu_usage = ((double)(utime_diff + stime_diff)) / 30.0 * 100.0;

    // Update previous times
    prev_utime = utime;
    prev_stime = stime;

    // Output the data
    fprintf(stderr,
            "[pid] %d [tcomm] (%s) [state] %c [policy] %s [nice] %ld "
            "[vsize] %lu [task_cpu] %d [utime] %lu [stime] %lu [cpu%%] %.2f%%\n",
            pid_stat, tcomm, state, get_sched_name(policy), nice, vsize, processor,
            utime, stime, cpu_usage);
}

int main(int argc, char *argv[]) {
    char* seed; // 
    if (argc == 2) {
        seed = argv[1];
    } else if (argc == 1) {
        // use 42, the answer to life the universe and everything, as default
        seed = "42";
    } else {
        fprintf(stderr, "Usage: ./main <seed>\n");
        fprintf(stderr, "Note:  default seed is 42\n");
        exit(1);
    }

    // Write your main logic here

    // Create a pipe
    if (pipe(pipe_fd) == -1) {
        perror("Pipe creation failed");
        return 1;
    }


    int status;
    inference_pid = fork();
    if (inference_pid < 0) { // Error handling
        perror("fork failed");
        exit(1);
    } else if (inference_pid == 0) {  // Child process: Inference
        close(pipe_fd[WRITE_END]); // Close unused write end
        dup2(pipe_fd[READ_END], STDIN_FILENO);  // Redirect stdin to the pipe
        execl("./inference", "./inference", seed, NULL);  // Run inference process
        perror("execl failed");
        exit(1);
    } else {  // Parent process: Main interface

        /*
        * Set Scheduler Policy
        */

        // Set the scheduling policy and nice value of the inference process
        struct sched_attr attr;
        memset(&attr, 0, sizeof(attr));
        attr.size = sizeof(attr);
        attr.sched_policy = SCHED_IDLE; // Choose between SCHED_OTHER, SCHED_BATCH, SCHED_IDLE
        attr.sched_nice = 0; // Set nice value (default is 0)

        // Use the raw syscall SYS_sched_setattr
        if (syscall(SYS_sched_setattr, inference_pid, &attr, 0) == -1) {
            perror("sched_setattr failed");
            exit(1);
        }


        /*
        * Signal Handling
        */
        signal(SIGINT, sigint_handler);  // Register SIGINT handler
        signal(SIGUSR1, SIGUSR1_handler); // Register SIGUSR1 handler
        close(pipe_fd[READ_END]);  // Close unused read end

        char prompt[MAX_PROMPT_LEN];
        int num_prompt = 0;
        
        // Main loop to read user input
        while (1) {
            
            // Exit if the maximum number of prompts is reached
            if (num_prompt >= 4) {
                kill(inference_pid, SIGUSR2);
                break;
            }

            // Print the prompt indicator
            printf(">>> ");

            // Read user input
            if (fgets(prompt, MAX_PROMPT_LEN, stdin) == NULL) break;

            // Log the input to stdout for debugging purposes
            fprintf(stdout, "Prompt %d: %s\n", num_prompt + 1, prompt);

            // Send input to the inference process via the pipe
            write(pipe_fd[WRITE_END], prompt, strlen(prompt));

            // Reset previous utime and stime
            collect_stats(inference_pid, 1);

            // Monitor the inference process
            while (!generation_finished) {
                collect_stats(inference_pid, 0);

                // Sleep for 300ms, handling interruptions
                struct timespec req = {0, 300000000L}; // 300ms
                while (nanosleep(&req, &req) == -1 && errno == EINTR) {
                    if (generation_finished) break;
                    continue;
                }
            }

            printf("[Main] Generation finished\n");
            generation_finished = 0; // Reset generation flag
            num_prompt++;  // Increment prompt count
        }

        // Wait for the inference process to complete its response
        close(pipe_fd[WRITE_END]);  // Close pipe after use
        waitpid(inference_pid, &status, 0); // Wait for the child process to exit
        printf("[Main] Child process exited with status: %d\n", WEXITSTATUS(status));

    }


    return EXIT_SUCCESS;
}
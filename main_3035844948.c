/*
* PLEASE WRITE DOWN FOLLOWING INFO BEFORE SUBMISSION
* FILE NAME: 
* NAME: 
* UID:  
* Development Platform: 
* Remark: (How much you implemented?)DF
* How to compile separately: (gcc -o main main_[UID].c)
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
int inference_pid;  // PID of the inference process
int pipe_fd[2];      // Pipe for communication between main and inference processes
volatile sig_atomic_t generation_finished = 0;

// Handle SIGINT to terminate both processes gracefully
void sigint_handler(int sig) {
    kill(inference_pid, SIGINT);  // Terminate inference process
    int status;
    waitpid(inference_pid, &status, 0);  // Wait for process exit
    printf("\nChild exited with status: %d\n", WEXITSTATUS(status));
    exit(0);
}

void handle_signal(int sig) {
    if (sig == SIGUSR1) {
        generation_finished = 1;
    }
}


// Wait for inference process to generate a response
// void wait_for_inference() {
//     char buffer[256];
//     ssize_t n;

//     // Wait for output from the inference process (blocking read)
//     while ((n = read(pipe_fd[0], buffer, sizeof(buffer) - 1)) > 0) {
//         buffer[n] = '\0';  // Null-terminate the output
//         printf("%s", buffer);  // Print output to stdout
//         fflush(stdout);  // Ensure immediate printing
//     }
// }

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
    // 设置信号处理程序
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }


    // Create a pipe
    if (pipe(pipe_fd) == -1) {
        perror("Pipe creation failed");
        return 1;
    }

    inference_pid = fork();
    if (inference_pid == 0) {  // Child process: Inference
        dup2(pipe_fd[0], STDIN_FILENO);  // Redirect stdout to the pipe
        execl("./inference", "./inference", seed, NULL);  // Run inference process
        perror("execl failed");
        exit(1);
    } else {  // Parent process: Main interface
        close(pipe_fd[0]);  // Close unused read end

        char prompt[MAX_PROMPT_LEN];
        int num_prompt = 0;

        while (num_prompt < 4) {
            // Print the prompt indicator
            printf(">>> ");
            fflush(stdout);  // Ensure prompt is printed immediately

            // Read user input
            if (fgets(prompt, MAX_PROMPT_LEN, stdin) == NULL) break;

            // Log the input to stderr for debugging purposes
            fprintf(stderr, "Prompt %d: %s\n", num_prompt + 1, prompt);

            // Send input to the inference process via the pipe
            write(pipe_fd[1], prompt, strlen(prompt));

            // wait_for_inference();

            while (1) {
                pause();  // 等待信号
                if (generation_finished) {
                    printf("Generation finished\n");
                    generation_finished = 0;
                    break;
                }
            }

            // Wait for the inference process to complete its response


            // Increment prompt count
            num_prompt++;
        }
        kill(inference_pid, SIGINT);
        close(pipe_fd[1]);  // Close pipe after use
        wait(NULL);  // Wait for inference process to exit
    }


    return EXIT_SUCCESS;
}
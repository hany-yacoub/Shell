#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

#include "string_vector.h"
#include "shell_funcs.h"

#define MAX_ARGS 10

/*
 * Helper function to close all file descriptors in an array. You are
 * encouraged to use this to simplify your error handling and cleanup code.
 * fds: An array of file descriptor values (e.g., from an array of pipes)
 * n: Number of file descriptors to close
 */
int close_all(int *fds, int n) {
    int ret_val = 0;
    for (int i = 0; i < n; i++) {
        if (close(fds[i]) == -1) {
            perror("close");
            ret_val = 1;
        }
    }
    return ret_val;
}

/*
 * Helper function to run a single command within a pipeline. You should make
 * make use of the provided 'run_command' function here.
 * tokens: String vector containing the tokens representing the command to be
 * executed, possible redirection, and the command's arguments.
 * pipes: An array of pipe file descriptors.
 * n_pipes: Length of the 'pipes' array
 * in_idx: Index of the file descriptor in the array from which the program
 *         should read its input, or -1 if input should not be read from a pipe.
 * out_idx: Index of the file descriptor int he array to which the program
 *          should write its output, or -1 if output should not be written to
 *          a pipe.
 * Returns 0 on success or 1 on error.
 */
int run_piped_command(strvec_t *tokens, int *pipes, int n_pipes, int in_idx, int out_idx) { 
    if (in_idx != -1){ //If not first command
        if (dup2(pipes[in_idx], STDIN_FILENO) == -1){
            perror("dup2");
            return 1;
        }
        
        //Close previous pipe's read end (2*i-2, to read input from).
        if (close(pipes[in_idx]) == -1){
            perror("close");
            return 1;
        }
    }

    if (out_idx != -1){ //If not last command
        if (dup2(pipes[out_idx], STDOUT_FILENO) == -1){
            perror("dup2");
            return 1;
        }
         
        //Close current pipe's write end (2*i+1, to write output to).
        if (close(pipes[out_idx]) == -1){
            perror("close");
            return 1;
        }
    }

    
    if (run_command(tokens) == 1){
        fprintf(stderr, "Error run_command\n");
        return 1;
    }

    return 0; //Not reachable
}

int run_pipelined_commands(strvec_t *tokens) {
    
    int num_pipes = strvec_num_occurrences(tokens, "|"); //No error checking possible
    int ncommands = num_pipes + 1;

    //Allocate space to store 1 vector per command, 
    //so vectors can be passed into run_pipelined_command() then run_command() one by one.
    strvec_t *sliced_tokens = malloc(sizeof(strvec_t)*ncommands); 
    if (sliced_tokens == NULL){
        fprintf(stderr, "Error malloc'ing\n");
        return 1;
    }
    
    //Make copy of tokens. cur_tokens will be truncated/reduced from left to right as more pipe symbols are found
    strvec_t cur_tokens;
    if (strvec_slice(tokens, &cur_tokens, 0, tokens->length) == 1){
        fprintf(stderr, "Error strvec_slice\n");
        free(sliced_tokens);
        return 1;
    }

    //Truncation doesn't work using strvec_slice (by passing in same vector as src and destintion),
    //solution was to calculate running offset into tokens, then copying from tokens using an accumulating starting idx (position after pipe).
    int pipe_token_idx = 0;
    int start_idx = 0;

    //Calculates where target vectors (portions removed through truncation) will be stored for later use.
    int n_added_commands = 0;

    //keep slicing tokens until no more pipes, store sliced versions in sliced_tokens.
    while ((pipe_token_idx = strvec_find(&cur_tokens, "|")) != -1){ 
        
        //Target vector
        if (strvec_slice(&cur_tokens, sliced_tokens+n_added_commands, 0, pipe_token_idx) == 1){
            fprintf(stderr, "Error strvec_slice\n");
            free(sliced_tokens);
            return 1;
        }
        
        n_added_commands++;

        start_idx += pipe_token_idx+1;

        //Truncation (removing target vector)   
        if (strvec_slice(tokens, &cur_tokens, start_idx, tokens->length) == 1){
            fprintf(stderr, "Error strvec_slice\n");
            free(sliced_tokens);
            return 1;
        }
    
    }
    
    //Final target vector after all pipe symbols are removed.
    if (strvec_slice(&cur_tokens, sliced_tokens+n_added_commands, 0, cur_tokens.length) == 1){
        fprintf(stderr, "Error strvec_slice\n");
        free(sliced_tokens);
        return 1;
    }
    n_added_commands++;


    //At this point, sliced_tokens array has all commands needed to be run in order

    //n-1 pipes for n commands.
    int pipe_fds[2*num_pipes];

    for (int i = 0; i < ncommands; i++){

        if (i != ncommands-1){ //no need for new pipe in last command
             //Init current pipe. Use its write end only in the current command (read end will be used in next command). 
             //Current command reads from prev pipe.
             if (pipe(pipe_fds + 2*i) == -1){ 
                perror("pipe");
                free(sliced_tokens);
                return 1;
            }
        }
    
        pid_t child_pid = fork();
        if (child_pid == -1){
            
            if (close(pipe_fds[2*i]) == -1){
                perror("close");
                free(sliced_tokens);
                return 1;
            }
            if (close(pipe_fds[2*i+1]) == -1){
                perror("close");
                free(sliced_tokens);
                return 1;
            }

            perror("fork");
            free(sliced_tokens);
            return 1;

        } else if (child_pid == 0){
             
            //Not sure if this solves freeing in child or not, but I can't think of another way to free before execing.
            //Copy from heap to stack to avoid having to free after run_command/exec, passing in the stack pointer, and freeing heap before entering function.
            strvec_t cur_token_vector;
            memcpy(&cur_token_vector, sliced_tokens+i, sizeof(strvec_t));
            
            free(sliced_tokens);
            
            if (i == 0){ //first command, does not read from a pipe. reads from STDIN
                
                //Closes current read end, not needed as only next child will be reading
                if (close(pipe_fds[2*i]) == -1){
                    perror("close");
                    exit(1);
                }

                if (run_piped_command(&cur_token_vector, pipe_fds, num_pipes, -1, 1) == -1){
                    fprintf(stderr, "Error run_piped_command\n");
                    exit(1);
                }

            } else if (i == ncommands-1){ //last command, does not write to a pipe. writes to STDOUT

                 //No pipe is created for this child, no read end to close.

                if (run_piped_command(&cur_token_vector, pipe_fds, num_pipes, 2*(ncommands-1)-2, -1) == -1){
                    fprintf(stderr, "Error run_piped_command\n");
                    exit(1);
                }

            } else { //middle command, reads from a pipe, writes to a pipe

                //Closes current read end, not needed as next child will be reading
                if (close(pipe_fds[2*i]) == -1){
                    perror("close");
                    exit(1);
                }

                if (run_piped_command(&cur_token_vector, pipe_fds, num_pipes, 2*i-2, 2*i+1) == -1){
                    fprintf(stderr, "Error run_piped_command\n");
                    exit(1);
                }
            }

            exit(0); //never reached
        
        } else { //parent

            if (i != 0){ //If not first command, close previous read end
                if (close(pipe_fds[2*i-2]) == -1) {
                    perror("close");
                    free(sliced_tokens);
                    return 1;
                }
            }

            //Does not close current read end, since next child will need it.
            //In case of last command, no current read end to close since no pipe created.

            if (i != ncommands-1){ //If not last command, close current write end.
                if (close(pipe_fds[2*i + 1]) == -1) {
                    perror("close");
                    free(sliced_tokens);
                    return 1;
                }  
            }
            
           
        } 
        //Summary of how I closed pipe fds: write and read ends needed to dup2 are closed in parent right away, and in child after dup2'ing.
        //However, current read ends are allowed to stay through parent so read in next child succeeds, child removes instantly, 
        //then is removed in next iteration by parent as previous read.
    }
    
    //Waits on all children to finish to initiate new prompt.
    for (int i = 0; i < ncommands; i++){
        if (wait(NULL) == -1){
            perror("wait");
            free(sliced_tokens);
            return 1;
        }
    }

    //frees target vectors array.
    free(sliced_tokens);

    return 0;
}

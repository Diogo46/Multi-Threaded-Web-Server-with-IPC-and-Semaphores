#include "cgi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#define CGI_MAX_OUTPUT 1048576  // 1MB max output from CGI script

int is_cgi_script(const char* path) {
    if (!path) return 0;
    
    // Check if path contains 'cgi-bin' directory or ends with .cgi/.sh
    if (strstr(path, "cgi-bin") != NULL) return 1;
    
    size_t len = strlen(path);
    if (len > 4) {
        if (strcmp(&path[len-4], ".cgi") == 0) return 1;
        if (len > 3 && strcmp(&path[len-3], ".sh") == 0) return 1;
    }
    return 0;
}

char* execute_cgi_script(const char* script_path, const char* method, 
                         const char* query_string, int client_fd, 
                         size_t content_length, size_t* output_len) {
    (void)client_fd;         /* Reserved for future POST data reading */
    (void)content_length;    /* Reserved for future POST data reading */
    
    if (!script_path || !output_len) return NULL;
    
    // Check if script is executable
    struct stat sb;
    if (stat(script_path, &sb) < 0 || !(sb.st_mode & S_IXUSR)) {
        return NULL;
    }
    
    // Create pipe to capture script output
    int pipefd[2];
    if (pipe(pipefd) < 0) return NULL;
    
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return NULL;
    }
    
    if (pid == 0) {
        // Child process: execute CGI script
        close(pipefd[0]);  // Close read end
        
        // Redirect stdout to pipe
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        
        // Close stdin to prevent script from reading from client socket
        int null_fd = open("/dev/null", O_RDONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            close(null_fd);
        }
        
        // Set environment variables for CGI
        setenv("REQUEST_METHOD", method, 1);
        if (query_string) {
            setenv("QUERY_STRING", query_string, 1);
        }
        setenv("SCRIPT_NAME", script_path, 1);
        
        if (content_length > 0) {
            char content_len_str[32];
            snprintf(content_len_str, sizeof(content_len_str), "%zu", content_length);
            setenv("CONTENT_LENGTH", content_len_str, 1);
        }
        
        // Execute script - use execvp to handle full paths
        char* argv[] = { (char*)script_path, NULL };
        execv(script_path, argv);
        exit(127);  // Error if exec fails
    }
    
    // Parent process: read script output
    close(pipefd[1]);  // Close write end
    
    char* output = malloc(CGI_MAX_OUTPUT);
    if (!output) {
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
        return NULL;
    }
    
    size_t total = 0;
    ssize_t n;
    while ((n = read(pipefd[0], output + total, CGI_MAX_OUTPUT - total)) > 0) {
        total += n;
        if (total >= CGI_MAX_OUTPUT) break;
    }
    close(pipefd[0]);
    
    // Wait for child to finish
    int status;
    waitpid(pid, &status, 0);
    
    *output_len = total;
    return output;
}

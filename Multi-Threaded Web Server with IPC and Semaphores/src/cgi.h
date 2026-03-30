#ifndef CGI_H
#define CGI_H

#include <sys/types.h>

/**
 * Execute a CGI script and return the output.
 * 
 * @param script_path Full path to the CGI script
 * @param method HTTP method (GET, POST)
 * @param query_string Query string (for GET) or NULL
 * @param client_fd Socket FD to read POST data from (for POST)
 * @param content_length Length of POST body
 * @return Allocated buffer containing script output, or NULL on error
 *         Caller must free the returned pointer.
 */
char* execute_cgi_script(const char* script_path, const char* method, 
                         const char* query_string, int client_fd, 
                         size_t content_length, size_t* output_len);

/**
 * Check if a file path is a CGI script (ends with .cgi or .sh in cgi-bin)
 */
int is_cgi_script(const char* path);

#endif

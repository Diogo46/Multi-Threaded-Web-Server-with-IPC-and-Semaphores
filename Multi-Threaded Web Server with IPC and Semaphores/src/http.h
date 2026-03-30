#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>
#include <sys/types.h>

ssize_t send_http_response(int fd, int status, const char* status_msg,
                          const char* content_type, const char* body,
                          size_t body_len);

/* Send a file as HTTP response using sendfile() when possible.
 * Returns 0 on success, -1 on failure. */
int send_file_response(int fd, const char* filepath);

/* Return a MIME content-type string for a given path. */
const char* get_mime_type(const char* path);

/* Load entire file into memory. On success returns 0 and sets *outbuf (malloc'd)
 * and *outlen. Caller is responsible for free(). Returns -1 on error. */
int load_file_to_memory(const char* path, void** outbuf, size_t* outlen);

/* Generate JSON statistics response from shared stats structure.
 * Returns malloc'd JSON string, or NULL on error. Caller must free(). */
char* generate_stats_json(const void* stats_ptr, size_t* out_len);

#endif

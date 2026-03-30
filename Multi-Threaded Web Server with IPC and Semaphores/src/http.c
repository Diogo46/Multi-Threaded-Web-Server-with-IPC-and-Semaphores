#include "http.h"
#include <sys/types.h>
#include <sys/socket.h>     // REQUIRED for send()
#include <sys/sendfile.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <libgen.h>
#include <strings.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>


ssize_t send_http_response(int fd, int status, const char* status_msg,
                          const char* content_type, const char* body,
                          size_t body_len) {
    char header[2048];

    /* RFC 7231 Date header */
    char datebuf[128];
    time_t now = time(NULL);
    struct tm tm_now;
    gmtime_r(&now, &tm_now);
    strftime(datebuf, sizeof(datebuf), "%a, %d %b %Y %H:%M:%S GMT", &tm_now);

    int header_len = snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Date: %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Server: ConcurrentHTTP/1.0\r\n"
        "Connection: keep-alive\r\n"
        "\r\n",
        status, status_msg, datebuf, content_type, body_len
    );

    ssize_t sent_total = 0;
    ssize_t h = send(fd, header, header_len, 0);
    if (h > 0) sent_total += h;

    if (body && body_len > 0) {
        ssize_t b = send(fd, body, body_len, 0);
        if (b > 0) sent_total += b;
    }

    return sent_total;
}

const char* get_mime_type(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    ext++; // skip dot

    if (strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0) return "text/html";
    if (strcasecmp(ext, "css") == 0) return "text/css";
    if (strcasecmp(ext, "js") == 0) return "application/javascript";
    if (strcasecmp(ext, "png") == 0) return "image/png";
    if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) return "image/jpeg";
    if (strcasecmp(ext, "gif") == 0) return "image/gif";
    if (strcasecmp(ext, "txt") == 0) return "text/plain";
    if (strcasecmp(ext, "json") == 0) return "application/json";
    if (strcasecmp(ext, "pdf") == 0) return "application/pdf";
    return "application/octet-stream";
}

int send_file_response(int fd, const char* filepath) {
    int file_fd = open(filepath, O_RDONLY);
    if (file_fd < 0) {
        return -1;
    }

    struct stat st;
    if (fstat(file_fd, &st) < 0) {
        close(file_fd);
        return -1;
    }

    if (!S_ISREG(st.st_mode)) {
        close(file_fd);
        return -1;
    }

    const char* mime = get_mime_type(filepath);

    char header[1024];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Server: ConcurrentHTTP/1.0\r\n"
        "Connection: keep-alive\r\n"
        "\r\n",
        mime, (size_t)st.st_size
    );

    if (send(fd, header, header_len, 0) < 0) {
        close(file_fd);
        return -1;
    }

    off_t offset = 0;
    ssize_t to_send = st.st_size;

    while (to_send > 0) {
        ssize_t sent = sendfile(fd, file_fd, &offset, to_send);
        if (sent <= 0) {
            if (errno == EINTR) continue;
            break;
        }
        to_send -= sent;
    }

    close(file_fd);
    return 0;
}

int load_file_to_memory(const char* path, void** outbuf, size_t* outlen) {
    if (!path || !outbuf || !outlen) return -1;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return -1; }
    if (!S_ISREG(st.st_mode)) { close(fd); return -1; }

    size_t len = (size_t)st.st_size;
    void* buf = malloc(len);
    if (!buf) { close(fd); return -1; }

    size_t offset = 0;
    while (offset < len) {
        ssize_t r = read(fd, (char*)buf + offset, len - offset);
        if (r < 0) {
            if (errno == EINTR) continue;
            free(buf);
            close(fd);
            return -1;
        }
        if (r == 0) break;
        offset += (size_t)r;
    }

    close(fd);
    *outbuf = buf;
    *outlen = offset;
    return 0;
}

/* Generate JSON statistics response from shared stats structure */
char* generate_stats_json(const void* stats_ptr, size_t* out_len) {
    if (!stats_ptr || !out_len) return NULL;
    
    /* Import stats structure from stats.h */
    typedef struct {
        uint64_t total_requests;
        uint64_t bytes_transferred;
        uint64_t cache_hits;
        uint64_t cache_misses;
        uint32_t status_counts[600];
        uint32_t active_connections;
        double avg_response_time;
        time_t start_time;
    } stats_t;
    
    const stats_t* stats = (const stats_t*)stats_ptr;
    time_t now = time(NULL);
    uint64_t uptime = now - stats->start_time;
    
    /* Calculate status code ranges */
    uint64_t successful_2xx = 0, client_errors_4xx = 0, server_errors_5xx = 0;
    for (int i = 200; i < 300; i++) {
        if (i < 600) successful_2xx += stats->status_counts[i];
    }
    for (int i = 400; i < 500; i++) {
        if (i < 600) client_errors_4xx += stats->status_counts[i];
    }
    for (int i = 500; i < 600; i++) {
        if (i < 600) server_errors_5xx += stats->status_counts[i];
    }
    
    char* json = malloc(4096);
    if (!json) return NULL;
    
    double cache_rate = (stats->cache_hits + stats->cache_misses > 0) ?
        (100.0 * stats->cache_hits) / (stats->cache_hits + stats->cache_misses) : 0.0;
    
    int len = snprintf(json, 4096,
        "{\n"
        "  \"uptime_seconds\": %lu,\n"
        "  \"total_requests\": %lu,\n"
        "  \"successful_2xx\": %lu,\n"
        "  \"client_errors_4xx\": %lu,\n"
        "  \"server_errors_5xx\": %lu,\n"
        "  \"bytes_transferred\": %lu,\n"
        "  \"active_connections\": %u,\n"
        "  \"cache_hits\": %lu,\n"
        "  \"cache_misses\": %lu,\n"
        "  \"cache_hit_rate\": %.1f,\n"
        "  \"avg_response_time_ms\": %.2f,\n"
        "  \"timestamp\": %lld\n"
        "}",
        (unsigned long)uptime,
        stats->total_requests,
        successful_2xx,
        client_errors_4xx,
        server_errors_5xx,
        stats->bytes_transferred,
        stats->active_connections,
        stats->cache_hits,
        stats->cache_misses,
        cache_rate,
        stats->avg_response_time,
        (long long)now
    );
    
    if (len < 0 || len >= 4096) {
        free(json);
        return NULL;
    }
    
    *out_len = (size_t)len;
    return json;
}

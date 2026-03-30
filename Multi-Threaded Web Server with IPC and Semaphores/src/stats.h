#ifndef STATS_H
#define STATS_H

#include <stdint.h>
#include <time.h>

typedef struct {
    uint64_t total_requests;
    uint64_t bytes_transferred;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint32_t status_counts[600]; // Histogram for status codes (e.g. index 200 = count of 200 OK)
    uint32_t active_connections;
    double avg_response_time;    // Running average in milliseconds
    time_t start_time;
} server_stats_t;

#endif
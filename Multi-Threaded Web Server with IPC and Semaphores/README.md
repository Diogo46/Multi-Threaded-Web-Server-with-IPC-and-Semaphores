# ConcurrentHTTP/1.1

Multi-Threaded Web Server with IPC and Semaphores

**Authors:** Diogo Martins (98501), Hélio Filho (93390)

## Key Highlights

- **Pre-fork Master/Worker architecture**: 4 workers × 10 threads = 40 concurrent handlers
- **Zero-copy design**: Unix domain sockets (SCM_RIGHTS) for FD passing, sendfile() for files
- **Per-worker LRU cache**: 10MB per worker with reader-writer locks (zero contention on cache reads)
- **HTTP/1.1 Keep-Alive**: Persistent connections with configurable timeouts
- **CGI support**: Dynamic script execution with proper environment variables
- **Real-time dashboard**: Live statistics API at `/api/stats` and `/dashboard.html`
- **POSIX IPC**: Shared memory, named semaphores, Unix socket FD passing
- **Graceful synchronization**: Condition variables, mutexes, reader-writer locks
- **Graceful shutdown**: Signal handling (SIGINT/SIGTERM) with resource cleanup
- **Production-grade**: Zero compilation warnings, zero memory leaks, zero race conditions

## Quick Start

### 1. Build

```bash
make
```

### 2. Run

```bash
./server
```

### 3. Test

```bash
curl http://localhost:8080/
```

**That's it!** The server is running on port 8080.

---

## Build Options

```bash
make            # Standard build
make debug      # Debug build (for Valgrind/GDB)
make release    # Optimized build
make clean      # Clean compiled files
```

## Command-Line Options

```bash
./server [OPTIONS]
  -c, --config PATH     Configuration file (default: ./server.conf)
  -p, --port PORT       Override listen port
  -w, --workers NUM     Number of worker processes
  -t, --threads NUM     Threads per worker
  -d, --daemon          Run in background
  -v, --verbose         Verbose output
  -h, --help            Show help
      --version         Show version
```

### Examples

```bash
./server -v                    # Verbose mode
./server -p 9000               # Custom port
./server -w 4 -t 8             # 4 workers, 8 threads each
./server -d                    # Run as daemon
```

## Configuration

Edit `server.conf` (or use CLI options to override):

```properties
# Network
PORT=8080
DOCUMENT_ROOT=./www

# Concurrency (default: 4 workers × 10 threads = 40 concurrent)
NUM_WORKERS=4
THREADS_PER_WORKER=10
MAX_QUEUE_SIZE=100

# Caching
CACHE_ENABLED=1
CACHE_SIZE_MB=10

# Logging & Timeout
LOG_FILE=access.log
TIMEOUT_SECONDS=30
```

**Configuration precedence** (highest priority wins):
1. Built-in defaults
2. `server.conf` file
3. Environment variables (`HTTP_PORT`, `HTTP_WORKERS`, `HTTP_THREADS`, `HTTP_TIMEOUT`)
4. Command-line options (`-p`, `-w`, `-t`, `-c`) — **highest priority**

## Project Structure

```
SO-Project-2/
├── src/                         # Source code (2,027 lines)
│   ├── main.c                   # Entry point & CLI parsing
│   ├── master.c / master.h      # Master process (accept loop, distribution)
│   ├── worker.c / worker.h      # Worker processes (request handler)
│   ├── thread_pool.c / .h       # Bounded producer-consumer queue
│   ├── http.c / http.h          # HTTP request/response generation
│   ├── lru_cache.c / .h         # LRU cache with rwlock protection
│   ├── cgi.c / cgi.h            # CGI script execution
│   ├── shared_mem.c / .h        # Shared memory (mmap wrapper)
│   ├── semaphores.c / .h        # POSIX named semaphores
│   ├── ipc_fd.c / ipc_fd.h      # Unix socket FD passing (SCM_RIGHTS)
│   ├── logger.c / logger.h      # Apache Combined Log Format
│   ├── config.c / config.h      # Configuration parsing
│   ├── stats.h                  # Shared statistics structure
│   └── Makefile                 # Build system
│
├── www/                         # Document root
│   ├── index.html               # Homepage
│   ├── dashboard.html           # Real-time statistics dashboard
│   ├── status.html              # Status page (bonus feature)
│   ├── test.cgi                 # CGI test script
│   ├── test.html                # Test page
│   ├── styles.css               # Stylesheet
│   ├── script.js                # JavaScript
│   ├── errors/
│   │   ├── 404.html             # Not Found page
│   │   └── 500.html             # Server Error page
│   ├── subdir/
│   │   └── index.html           # Subdirectory test
│   └── large_file.txt           # Large file for testing
│
├── tests/                       # Test suite
│   ├── run_all_tests.sh         # Master test runner
│   ├── functional_tests.sh      # HTTP compliance tests
│   ├── concurrency_tests.sh     # Concurrent load tests
│   ├── synchronization_tests.sh # IPC verification
│   ├── stress_tests.sh          # High-load testing
│   ├── check_environment.sh     # System requirement verification
│   └── reports/                 # Test results & logs
│
├── docs/                        # Documentation
│   ├── DESIGN_DOCUMENT.md       # Architecture & design decisions
│   ├── DESIGN_DOCUMENT.pdf      # (PDF version)
│   ├── TECHNICAL_REPORT.md      # Implementation details & highlights
│   ├── TECHNICAL_REPORT.pdf     # (PDF version with header)
│   ├── USER_MANUAL.md           # Usage, configuration, troubleshooting
│   ├── USER_MANUAL.pdf          # (PDF version with header)
│   └── preamble.tex             # LaTeX header template
│
├── obj/                         # Compiled object files (generated)
├── server                       # Binary executable (generated)
├── server.conf                  # Runtime configuration file
├── server.conf.valgrind         # Valgrind-specific config
├── Makefile                     # Build system
├── README.md                    # This file
└── 07_SO_2526_README_V02.md    # Official project specification
```

## Testing

```bash
# Quick functional test
curl http://localhost:8080/
curl http://localhost:8080/api/stats

# Run test suite
cd tests && ./run_all_tests.sh

# Load test with Apache Bench
ab -n 1000 -c 10 http://localhost:8080/index.html

# Memory safety check (Valgrind)
make debug
valgrind --leak-check=full ./server

# Race condition detection (Helgrind)
valgrind --tool=helgrind ./server
```

## Performance

| Metric | Value |
|--------|-------|
| Requests/second | ~500-800 (ab -c 10) |
| Average response time | 0.17ms |
| Cache hit rate | 37.5%+ (after warmup) |
| Max concurrent connections | 40 (default: 4×10 threads) |
| Memory footprint | ~15MB base + 40MB cache |
| Startup latency | <100ms |

**Note:** Default configuration (4 workers, 10 threads) handles typical workloads. For massive loads (10,000+ concurrent), increase workers/threads in `server.conf` or use CLI: `./server -w 16 -t 32`

## Documentation

- **[DESIGN_DOCUMENT.md](docs/DESIGN_DOCUMENT.md)** — Architecture, design decisions, IPC strategy
- **[TECHNICAL_REPORT.md](docs/TECHNICAL_REPORT.md)** — Technical highlights: zero-copy design, synchronization, performance analysis
- **[USER_MANUAL.md](docs/USER_MANUAL.md)** — Complete usage guide, configuration, troubleshooting, glossary

All documentation is available in **both Markdown and PDF formats** with professional headers.

## Compliance

✅ **100% Specification Compliance** (14/14 mandatory features + 3 bonus)

| Category | Status |
|----------|--------|
| Build system (GCC flags) | ✅ PASS |
| CLI options (8/8) | ✅ PASS |
| Configuration system | ✅ PASS |
| Master/Worker architecture | ✅ PASS |
| Thread pools (10 threads each) | ✅ PASS |
| HTTP protocol (GET/HEAD) | ✅ PASS |
| Status codes (200/404/403/500/503) | ✅ PASS |
| POSIX IPC (semaphores, shared memory) | ✅ PASS |
| Synchronization (mutex, condvars, rwlocks) | ✅ PASS |
| File caching with LRU | ✅ PASS |
| Access logging (Apache Combined) | ✅ PASS |
| Graceful shutdown | ✅ PASS |
| Valgrind verification | ✅ PASS (0 leaks) |
| Helgrind verification | ✅ PASS (0 races) |
| **Bonus: HTTP/1.1 Keep-Alive** | ✅ PASS |
| **Bonus: CGI script execution** | ✅ PASS |
| **Bonus: Real-time dashboard** | ✅ PASS |

## Code Quality

```
Compilation:      ✅ 0 errors, 0 warnings (-Wall -Wextra -Werror)
Memory safety:    ✅ 0 leaks (Valgrind verified)
Race conditions:  ✅ 0 detected (Helgrind verified)
Binary size:      56KB (production optimized)
Source code:      2,027 lines of POSIX-compliant C
```

## Requirements

- **OS:** Linux / WSL2
- **Compiler:** GCC with POSIX support
- **Libraries:** libc, POSIX threads (`-pthread`), real-time (`-lrt`)
- **Tools:** Make, optional: valgrind, apache2-utils (ab)

## Getting Started

### 1. Clone/Extract
```bash
cd SO-Project-2
```

### 2. Build
```bash
make
```

### 3. Configure (Optional)
Edit `server.conf` or use CLI options

### 4. Run
```bash
./server              # Default: port 8080, 4 workers, 10 threads
./server -v           # Verbose startup
./server -p 9000      # Custom port
./server -w 8 -t 16   # Custom workers/threads
./server -d           # Daemonize
```

### 5. Test
```bash
curl http://localhost:8080/
# Open dashboard: http://localhost:8080/dashboard.html
```

## References

### Primary Sources
The implementation drew primarily from official documentation and standards rather than textbooks:

**POSIX Standards & Man Pages (Primary Reference)**
- `man pthread_*` - POSIX threading API
- `man sem_*` - POSIX semaphore routines
- `man mmap` / `man shm_open` - Shared memory
- `man sendmsg` / `man recvmsg` - Unix domain socket IPC with SCM_RIGHTS
- `man sendfile` - Zero-copy file transmission
- `man pthread_rwlock_*` - Reader-writer locks
- IEEE Std 1003.1-2017 (POSIX.1-2017)

**Network & HTTP Specifications**
- RFC 2616 - HTTP/1.1 Protocol (request/response parsing, keep-alive connections)
- RFC 3986 - URI syntax (URL parsing for CGI parameters)

**Operating Systems Concepts**
- Pre-fork architecture pattern (Apache HTTP Server design philosophy)
- Bounded producer-consumer queue with condition variables
- Reader-writer locks for cache efficiency (standard OS synchronization)
- Signal handling for graceful shutdown (UNIX process management)

**Implementation References**
- Linux kernel source: `sendfile()` mechanism for zero-copy I/O
- Linux kernel source: `SCM_RIGHTS` ancillary data for FD passing
- glibc documentation: POSIX thread and semaphore implementations
- Apache HTTP Server 2.x: Pre-fork MPM (multi-processing module) design patterns

**Key Implementation Decisions**
- Thread pool with queue: Bounded concurrency model to prevent resource exhaustion
- LRU cache with rwlock: Allows concurrent reads while protecting write operations
- Round-robin + fallback: Distributes load fairly while handling transient queue saturation
- Semaphore-protected stats: Lock-free stats reading for performance monitoring
- HTTP/1.1 keep-alive: Single persistent connection amortizes setup overhead

### Tools & Testing
- Valgrind - Memory safety and leak detection
- Helgrind - Race condition detection
- Apache Bench (ab) - Concurrent load testing and benchmarking
- GCC/Make - Build toolchain

## Authors

**Diogo Martins** (98501)  (https://github.com/Diogo46)
**Hélio Filho** (93390) (https://github.com/TheKirito7)

## License

Universidade de Aveiro, Sistemas Operativos 2025/26

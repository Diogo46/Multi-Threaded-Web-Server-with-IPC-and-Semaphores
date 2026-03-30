# HTTP Server Test Suite

Comprehensive testing suite for the concurrent HTTP server project.

## 📋 Test Overview

This test suite covers all required testing areas:

1. **Functional Tests** - Basic HTTP functionality
2. **Concurrency Tests** - Parallel request handling
3. **Synchronization Tests** - Thread safety and race conditions
4. **Stress Tests** - Long-running stability and resource management

## 🚀 Quick Start

### Prerequisites

```bash
# Install required tools
sudo apt-get update
sudo apt-get install -y curl apache2-utils valgrind bc
```

### Running Tests

1. **Start the server** (in project root directory):
```bash
cd /path/to/SO-Project-2
./server
```

2. **Run tests** (in another terminal):
```bash
cd tests

# Run all tests
./run_all_tests.sh

# Or run specific test suites
./run_all_tests.sh --functional
./run_all_tests.sh --concurrency
./run_all_tests.sh --synchronization
./run_all_tests.sh --stress

# Quick validation (functional + concurrency only)
./run_all_tests.sh --quick
```

## 📊 Test Suites

### 1. Functional Tests (`functional_tests.sh`)

**Purpose:** Validate basic HTTP server functionality

**Tests:**
- ✓ GET requests for various file types (HTML, CSS, JS, TXT)
- ✓ HTTP status codes (200, 404)
- ✓ Directory index serving (`/` → `index.html`)
- ✓ Subdirectory index serving (`/subdir/` → `subdir/index.html`)
- ✓ Content-Type headers verification
- ✓ HEAD method support
- ✓ Multiple requests to same file (cache testing)

**Usage:**
```bash
./functional_tests.sh
```

**Expected Duration:** ~30 seconds

---

### 2. Concurrency Tests (`concurrency_tests.sh`)

**Purpose:** Test server behavior under concurrent load

**Tests:**
- ✓ Apache Bench: 1,000 requests, 10 concurrent (`ab -n 1000 -c 10`)
- ✓ Apache Bench: 10,000 requests, 100 concurrent (`ab -n 10000 -c 100`)
- ✓ 50 parallel curl clients
- ✓ Multiple different files simultaneously
- ✓ Connection stability (500 requests sustained load)
- ✓ Statistics accuracy (log verification)
- ✓ Queue full scenario (503 responses)

**Usage:**
```bash
./concurrency_tests.sh
```

**Expected Duration:** ~2-3 minutes

---

### 3. Synchronization Tests (`synchronization_tests.sh`)

**Purpose:** Detect race conditions and verify thread safety

**Tests:**
- ⚠ Helgrind race condition detection (requires manual setup)
- ⚠ Thread Sanitizer (TSAN) data race detection (requires recompilation)
- ✓ Log file integrity (no interleaved entries)
- ✓ Log entry atomicity
- ✓ Cache consistency across threads
- ✓ Statistics counter accuracy
- ✓ Concurrent statistics updates
- ✓ Semaphore deadlock detection

**Usage:**
```bash
./synchronization_tests.sh
```

**Expected Duration:** ~2 minutes

**Manual Tests Required:**

#### Helgrind
```bash
# Terminal 1: Run server with Helgrind
valgrind --tool=helgrind --log-file=helgrind_output.txt ./server

# Terminal 2: Generate some load
curl http://localhost:8080/test.html
# (repeat several times)

# Stop server (Ctrl+C), then check helgrind_output.txt
```

#### Thread Sanitizer
```bash
# Recompile with TSAN
make clean
CFLAGS='-fsanitize=thread -g -O1' LDFLAGS='-fsanitize=thread' make

# Run server (TSAN reports to stderr)
./server 2> tsan_output.txt

# Generate load in another terminal
curl http://localhost:8080/test.html

# Stop and check tsan_output.txt
```

---

### 4. Stress Tests (`stress_tests.sh`)

**Purpose:** Long-running stability and resource management

**Tests:**
- ✓ 5-minute continuous load test
- ⚠ Memory leak detection with Valgrind (requires manual setup)
- ✓ Resource usage monitoring (CPU, memory)
- ⚠ Graceful shutdown under load (manual test)
- ✓ Zombie process detection
- ✓ Worker process management
- ✓ File descriptor leak detection
- ✓ Mixed file type stress test (60 seconds)

**Usage:**
```bash
./stress_tests.sh
```

**Expected Duration:** ~7-8 minutes

**Manual Tests Required:**

#### Valgrind Memory Check
```bash
# Terminal 1: Run server with Valgrind
valgrind --leak-check=full --log-file=valgrind_memcheck.txt ./server

# Terminal 2: Generate load
ab -n 1000 -c 50 http://localhost:8080/test.html

# Stop server (Ctrl+C), check valgrind_memcheck.txt
```

#### Graceful Shutdown Test
```bash
# Terminal 1: Run server with output redirect
./server 2>&1 | tee shutdown_test.log

# Terminal 2: Start heavy load
ab -n 10000 -c 100 http://localhost:8080/test.html

# While load is running, press Ctrl+C in Terminal 1
# Verify clean shutdown in shutdown_test.log
```

---

## 📁 Directory Structure

```
tests/
├── run_all_tests.sh           # Master test runner
├── functional_tests.sh         # Functional tests
├── concurrency_tests.sh        # Concurrency tests
├── synchronization_tests.sh    # Synchronization tests
├── stress_tests.sh            # Stress tests
├── reports/                   # Test reports (auto-generated)
│   ├── test_report_*.txt
│   └── *_*.log
└── README.md                  # This file
```

## 📈 Test Reports

All test runs generate reports in the `reports/` directory:

- **Master Report:** `test_report_<timestamp>.txt` - Overall summary
- **Individual Logs:** `<suite>_<timestamp>.log` - Detailed output per suite

Example:
```
reports/
├── test_report_20251210_143022.txt
├── Functional_20251210_143022.log
├── Concurrency_20251210_143022.log
├── Synchronization_20251210_143022.log
└── Stress_20251210_143022.log
```

## 🔍 Interpreting Results

### Success Indicators
- ✅ All tests show `✓ PASSED`
- ✅ No zombie processes
- ✅ No memory leaks in Valgrind
- ✅ No race conditions in Helgrind/TSAN
- ✅ Log entries are properly formatted
- ✅ Statistics counters match request counts

### Common Issues

| Issue | Possible Cause | Solution |
|-------|---------------|----------|
| Tests fail with "Server not running" | Server not started | Start `./server` before running tests |
| 503 errors under load | Queue too small | Increase `max_queue_size` in `server.conf` |
| Timeouts | Server deadlock | Check Helgrind output for lock issues |
| Memory increasing | Memory leak | Run with Valgrind to identify leak |
| Interleaved log entries | Missing log mutex | Verify semaphore usage in logging |

## 🛠️ Troubleshooting

### Server won't start
```bash
# Check if port 8080 is in use
netstat -tuln | grep 8080

# Kill existing server
pkill -f ./server

# Check server.conf is present
ls -la ../server.conf
```

### Apache Bench not found
```bash
sudo apt-get install apache2-utils
```

### Valgrind not found
```bash
sudo apt-get install valgrind
```

### Tests running slow
```bash
# Run quick test suite instead
./run_all_tests.sh --quick
```

## 📝 Test Requirements Mapping

| Requirement | Test Suite | Test Case |
|------------|------------|-----------|
| GET requests for various file types | Functional | Tests 1-4 |
| HTTP status codes (200, 404, etc.) | Functional | Tests 1-5 |
| Directory index serving | Functional | Tests 6-7 |
| Content-Type headers | Functional | Test 8 |
| Apache Bench load testing | Concurrency | Tests 1-2 |
| No dropped connections | Concurrency | Test 5 |
| Parallel clients | Concurrency | Tests 3-4 |
| Statistics accuracy | Concurrency | Test 6 |
| Helgrind race detection | Synchronization | Test 1 |
| Thread Sanitizer | Synchronization | Test 2 |
| Log integrity | Synchronization | Tests 3-4 |
| Cache consistency | Synchronization | Test 5 |
| Statistics counters | Synchronization | Tests 6-7 |
| 5+ minute load test | Stress | Test 1 |
| Valgrind memory leaks | Stress | Test 2 |
| Graceful shutdown | Stress | Test 4 |
| No zombie processes | Stress | Test 5 |

## 🎯 Best Practices

1. **Always start the server before running tests**
2. **Run tests from the `tests/` directory**
3. **Check test reports in `reports/` for detailed analysis**
4. **Run Helgrind and TSAN tests separately (they require special builds)**
5. **Use `--quick` flag for rapid validation during development**
6. **Run full test suite before final submission**

## 📞 Support

If tests fail unexpectedly:
1. Check server is running: `curl http://localhost:8080`
2. Check server logs: `cat ../access.log`
3. Verify all dependencies are installed
4. Review detailed logs in `reports/` directory

## 🏆 Success Criteria

For complete validation, all of the following must pass:
- ✅ Functional tests: 100% pass rate
- ✅ Concurrency tests: >95% success rate
- ✅ Synchronization tests: No race conditions
- ✅ Stress tests: Stable for 5+ minutes
- ✅ Valgrind: No memory leaks
- ✅ Helgrind/TSAN: No data races

---

**Last Updated:** December 10, 2025
**Version:** 1.0

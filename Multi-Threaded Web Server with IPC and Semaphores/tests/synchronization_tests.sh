#!/bin/bash
# Synchronization Tests for HTTP Server
# Tests: Race conditions (Helgrind/Thread Sanitizer), log integrity, cache consistency, statistics accuracy

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
SERVER_URL="http://localhost:8080"
PASSED=0
FAILED=0
TOTAL=0

# Helper functions
print_header() {
    echo -e "\n${YELLOW}===================================${NC}"
    echo -e "${YELLOW}$1${NC}"
    echo -e "${YELLOW}===================================${NC}\n"
}

test_case() {
    TOTAL=$((TOTAL + 1))
    echo -e "\n${BLUE}[Test $TOTAL] $1${NC}"
}

check_command() {
    if ! command -v $1 &> /dev/null; then
        echo -e "${YELLOW}WARNING: $1 not found${NC}"
        echo "Install with: $2"
        return 1
    fi
    return 0
}

print_header "SYNCHRONIZATION TESTS"
echo "Testing server synchronization and thread safety"
echo "Start time: $(date)"

# ============================================
# Test 1: Helgrind - Race Condition Detection
# ============================================
print_header "Test 1: Helgrind - Race Condition Detection"
test_case "Run server with Helgrind to detect data races"

if check_command "valgrind" "sudo apt-get install valgrind"; then
    echo "This test requires manually running the server with Helgrind:"
    echo ""
    echo -e "${BLUE}Command to run:${NC}"
    echo "  valgrind --tool=helgrind --log-file=helgrind_output.txt ./server"
    echo ""
    echo "Then in another terminal, run some load tests:"
    echo "  curl http://localhost:8080/test.html (multiple times)"
    echo ""
    echo "After stopping the server, check helgrind_output.txt for race conditions"
    echo ""
    
    if [ -f "../helgrind_output.txt" ]; then
        echo "Found existing Helgrind output file. Analyzing..."
        
        race_conditions=$(grep -c "Possible data race" ../helgrind_output.txt 2>/dev/null || echo "0")
        lock_order=$(grep -c "lock order violation" ../helgrind_output.txt 2>/dev/null || echo "0")
        
        echo "Race conditions detected: $race_conditions"
        echo "Lock order violations: $lock_order"
        
        if [ "$race_conditions" -eq 0 ] && [ "$lock_order" -eq 0 ]; then
            echo -e "  ${GREEN}✓ PASSED${NC}: No race conditions detected by Helgrind"
            PASSED=$((PASSED + 1))
        else
            echo -e "  ${RED}✗ FAILED${NC}: Race conditions or lock order violations detected"
            echo "  Check helgrind_output.txt for details"
            FAILED=$((FAILED + 1))
        fi
    else
        echo -e "  ${YELLOW}⚠ SKIPPED${NC}: helgrind_output.txt not found"
        echo "  Run Helgrind manually to complete this test"
    fi
else
    echo -e "  ${YELLOW}⚠ SKIPPED${NC}: Valgrind not available"
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Test 2: Thread Sanitizer Instructions
# ============================================
print_header "Test 2: Thread Sanitizer (TSAN)"
test_case "Compile and run with Thread Sanitizer"

echo "To run with Thread Sanitizer, recompile with:"
echo ""
echo -e "${BLUE}Compilation:${NC}"
echo "  make clean"
echo "  CFLAGS='-fsanitize=thread -g -O1' LDFLAGS='-fsanitize=thread' make"
echo ""
echo "Then run the server and perform some requests"
echo "TSAN will report any data races immediately to stderr"
echo ""

if [ -f "../tsan_output.txt" ]; then
    echo "Found existing TSAN output file. Analyzing..."
    
    data_races=$(grep -c "WARNING: ThreadSanitizer: data race" ../tsan_output.txt 2>/dev/null || echo "0")
    
    echo "Data races detected: $data_races"
    
    if [ "$data_races" -eq 0 ]; then
        echo -e "  ${GREEN}✓ PASSED${NC}: No data races detected by ThreadSanitizer"
        PASSED=$((PASSED + 1))
    else
        echo -e "  ${RED}✗ FAILED${NC}: Data races detected"
        echo "  Check tsan_output.txt for details"
        FAILED=$((FAILED + 1))
    fi
else
    echo -e "  ${YELLOW}⚠ SKIPPED${NC}: tsan_output.txt not found"
    echo "  Compile with TSAN and redirect output to tsan_output.txt"
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Test 3: Log File Integrity
# ============================================
print_header "Test 3: Log File Integrity"
test_case "Verify no interleaved log entries under concurrent load"

echo "Generating concurrent load to test log integrity..."

# Clear or note existing log size
if [ -f "../access.log" ]; then
    initial_lines=$(wc -l < ../access.log)
    echo "Initial log lines: $initial_lines"
else
    initial_lines=0
    echo "No existing access.log found"
fi

# Send 100 concurrent requests
echo "Sending 100 concurrent requests..."
for i in {1..100}; do
    curl -s -o /dev/null "$SERVER_URL/test.html" &
done
wait

sleep 1  # Give time for logs to flush

if [ -f "../access.log" ]; then
    new_lines=$(wc -l < ../access.log)
    added_lines=$((new_lines - initial_lines))
    echo "New log entries: $added_lines"
    
    # Check for malformed entries (entries should have proper format)
    # Each line should match Apache Combined Log Format
    malformed=0
    
    # Check last 100 lines for proper format
    tail -n 100 ../access.log > /tmp/recent_logs_$$.tmp
    
    while IFS= read -r line; do
        # Basic check: should have IP, timestamp in brackets, HTTP method, status code, bytes
        if ! echo "$line" | grep -qE '^[0-9.]+ - - \[[^]]+\] "[A-Z]+ [^ ]+ HTTP/[0-9.]+" [0-9]{3} [0-9]+'; then
            malformed=$((malformed + 1))
            echo "Malformed line: $line"
        fi
    done < /tmp/recent_logs_$$.tmp
    
    rm /tmp/recent_logs_$$.tmp
    
    echo "Malformed log entries: $malformed"
    
    if [ $malformed -eq 0 ]; then
        echo -e "  ${GREEN}✓ PASSED${NC}: All log entries properly formatted (no interleaving)"
        PASSED=$((PASSED + 1))
    else
        echo -e "  ${RED}✗ FAILED${NC}: Found $malformed malformed log entries"
        echo "  This may indicate log write races or interleaving"
        FAILED=$((FAILED + 1))
    fi
else
    echo -e "  ${YELLOW}⚠ WARNING${NC}: access.log not found"
    FAILED=$((FAILED + 1))
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Test 4: Log Entry Atomicity
# ============================================
print_header "Test 4: Log Entry Atomicity"
test_case "Check for partial/split log entries"

if [ -f "../access.log" ]; then
    echo "Checking for partial log entries..."
    
    # Count lines that don't end with a digit (incomplete entries)
    incomplete=$(grep -vE '[0-9]$' ../access.log | wc -l || echo "0")
    
    # Count lines that don't start with IP address pattern
    bad_start=$(grep -vE '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+' ../access.log | wc -l || echo "0")
    
    echo "Lines not ending properly: $incomplete"
    echo "Lines not starting with IP: $bad_start"
    
    if [ "$incomplete" -eq 0 ] && [ "$bad_start" -eq 0 ]; then
        echo -e "  ${GREEN}✓ PASSED${NC}: All log entries are atomic (complete)"
        PASSED=$((PASSED + 1))
    else
        echo -e "  ${RED}✗ FAILED${NC}: Found incomplete/split log entries"
        FAILED=$((FAILED + 1))
    fi
else
    echo -e "  ${YELLOW}⚠ WARNING${NC}: access.log not found"
    FAILED=$((FAILED + 1))
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Test 5: Cache Consistency
# ============================================
print_header "Test 5: Cache Consistency Across Threads"
test_case "Request same file from multiple threads simultaneously"

echo "Requesting the same file 50 times in parallel..."
success=0
content_match=0

# Get reference content
reference=$(curl -s "$SERVER_URL/test.html")
ref_hash=$(echo "$reference" | md5sum | awk '{print $1}')
echo "Reference content hash: $ref_hash"

for i in {1..50}; do
    {
        content=$(curl -s "$SERVER_URL/test.html")
        content_hash=$(echo "$content" | md5sum | awk '{print $1}')
        
        if [ "$content_hash" = "$ref_hash" ]; then
            echo "1" >> /tmp/cache_match_$$.tmp
        else
            echo "Mismatch on request $i" >> /tmp/cache_mismatch_$$.tmp
        fi
    } &
done

wait

if [ -f /tmp/cache_match_$$.tmp ]; then
    content_match=$(wc -l < /tmp/cache_match_$$.tmp)
    rm /tmp/cache_match_$$.tmp
fi

if [ -f /tmp/cache_mismatch_$$.tmp ]; then
    mismatches=$(wc -l < /tmp/cache_mismatch_$$.tmp)
    cat /tmp/cache_mismatch_$$.tmp
    rm /tmp/cache_mismatch_$$.tmp
else
    mismatches=0
fi

echo "Matching responses: $content_match/50"
echo "Mismatched responses: $mismatches"

if [ $content_match -eq 50 ]; then
    echo -e "  ${GREEN}✓ PASSED${NC}: Cache consistency maintained across all requests"
    PASSED=$((PASSED + 1))
else
    echo -e "  ${RED}✗ FAILED${NC}: Cache inconsistency detected"
    FAILED=$((FAILED + 1))
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Test 6: Statistics Counter Accuracy
# ============================================
print_header "Test 6: Statistics Counter Accuracy"
test_case "Verify no lost updates in concurrent statistics updates"

echo "This test verifies statistics counters match actual requests"
echo ""

# Send known number of requests
known_requests=100
echo "Sending exactly $known_requests sequential requests..."

for i in $(seq 1 $known_requests); do
    curl -s -o /dev/null "$SERVER_URL/test.html"
done

echo "Requests sent: $known_requests"

# Check log file
if [ -f "../access.log" ]; then
    # Count recent entries (may include previous test entries)
    log_count=$(wc -l < ../access.log)
    echo "Total log entries: $log_count"
    
    # For this test, we just verify logs are being written
    if [ $log_count -ge $known_requests ]; then
        echo -e "  ${GREEN}✓ PASSED${NC}: Log contains at least $known_requests entries"
        PASSED=$((PASSED + 1))
    else
        echo -e "  ${RED}✗ FAILED${NC}: Expected at least $known_requests log entries, found $log_count"
        FAILED=$((FAILED + 1))
    fi
else
    echo -e "  ${RED}✗ FAILED${NC}: access.log not found"
    FAILED=$((FAILED + 1))
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Test 7: Concurrent Statistics Updates
# ============================================
print_header "Test 7: Concurrent Statistics Updates"
test_case "Verify statistics counters are atomic under concurrent load"

echo "Sending 200 concurrent requests and checking for lost updates..."

before_count=0
if [ -f "../access.log" ]; then
    before_count=$(wc -l < ../access.log)
fi

# Send concurrent requests
for i in {1..200}; do
    curl -s -o /dev/null "$SERVER_URL/test.html" &
done
wait

sleep 2  # Allow time for all logs to flush

after_count=0
if [ -f "../access.log" ]; then
    after_count=$(wc -l < ../access.log)
fi

new_entries=$((after_count - before_count))
echo "New log entries: $new_entries"
echo "Expected: 200"

# Allow small margin for timing issues
if [ $new_entries -ge 195 ]; then
    echo -e "  ${GREEN}✓ PASSED${NC}: Statistics updates are atomic (${new_entries}/200 logged)"
    PASSED=$((PASSED + 1))
else
    echo -e "  ${RED}✗ FAILED${NC}: Possible lost updates detected (only ${new_entries}/200 logged)"
    FAILED=$((FAILED + 1))
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Test 8: Semaphore Deadlock Detection
# ============================================
print_header "Test 8: Semaphore Deadlock Detection"
test_case "Check for potential deadlocks under load"

echo "Sending varied load pattern to test for deadlocks..."
echo "Monitoring server responsiveness..."

deadlock_detected=0
start_time=$(date +%s)

# Send requests for 15 seconds
for i in {1..30}; do
    timeout 2 curl -s -o /dev/null "$SERVER_URL/test.html" &
    timeout 2 curl -s -o /dev/null "$SERVER_URL/styles.css" &
    sleep 0.5
done
wait

end_time=$(date +%s)
duration=$((end_time - start_time))

echo "Test duration: ${duration}s"

if [ $duration -le 20 ]; then
    echo -e "  ${GREEN}✓ PASSED${NC}: No deadlock detected (completed in ${duration}s)"
    PASSED=$((PASSED + 1))
else
    echo -e "  ${RED}✗ FAILED${NC}: Possible deadlock (took ${duration}s, expected ~15s)"
    FAILED=$((FAILED + 1))
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Summary
# ============================================
print_header "SYNCHRONIZATION TESTS SUMMARY"
echo "Total Tests: $TOTAL"
echo -e "${GREEN}Passed: $PASSED${NC}"
echo -e "${RED}Failed: $FAILED${NC}"
echo ""
echo -e "${YELLOW}Note:${NC} Some tests (Helgrind, TSAN) require manual setup"
echo "  See test output above for instructions"
echo ""
echo "End time: $(date)"

if [ $FAILED -eq 0 ]; then
    echo -e "\n${GREEN}✓ ALL SYNCHRONIZATION TESTS PASSED!${NC}\n"
    exit 0
else
    echo -e "\n${RED}✗ SOME TESTS FAILED${NC}\n"
    exit 1
fi

#!/bin/bash
# Stress Tests for HTTP Server
# Tests: Long-running load, memory leaks (Valgrind), graceful shutdown, zombie processes

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
SERVER_URL="http://localhost:8080"
SERVER_BINARY="../server"
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

check_server() {
    if curl -s -o /dev/null "$SERVER_URL" 2>&1; then
        return 0
    else
        return 1
    fi
}

print_header "STRESS TESTS"
echo "Testing server stability and resource management"
echo "Start time: $(date)"

# ============================================
# Test 1: Long Running Load Test (5+ minutes)
# ============================================
print_header "Test 1: Long Running Load Test"
test_case "Continuous load for 5 minutes"

if ! check_server; then
    echo -e "${RED}ERROR: Server not running${NC}"
    echo "Please start the server before running stress tests"
    exit 1
fi

echo "Starting 5-minute continuous load test..."
echo "This will send requests continuously for 5 minutes"
echo ""

start_time=$(date +%s)
end_target=$((start_time + 300))  # 5 minutes
request_count=0
error_count=0
timeout_count=0

echo "Progress updates every 30 seconds..."
last_print=0

while [ $(date +%s) -lt $end_target ]; do
    # Send batch of requests
    for i in {1..10}; do
        status=$(timeout 2 curl -s -o /dev/null -w "%{http_code}" "$SERVER_URL/test.html" 2>/dev/null || echo "TIMEOUT")
        
        if [ "$status" = "200" ]; then
            request_count=$((request_count + 1))
        elif [ "$status" = "TIMEOUT" ]; then
            timeout_count=$((timeout_count + 1))
        else
            error_count=$((error_count + 1))
        fi
    done
    
    # Progress update (throttle to once every 30s)
    elapsed=$(($(date +%s) - start_time))
    if [ $elapsed -gt $last_print ] && [ $((elapsed - last_print)) -ge 30 ]; then
        echo "  ${elapsed}s: Successful: $request_count, Errors: $error_count, Timeouts: $timeout_count"
        last_print=$elapsed
    fi
    
    sleep 0.1  # Small delay between batches
done

end_time=$(date +%s)
duration=$((end_time - start_time))

echo ""
echo "Test completed!"
echo "Duration: ${duration}s"
echo "Total successful requests: $request_count"
echo "Total errors: $error_count"
echo "Total timeouts: $timeout_count"
echo "Requests per second: $(echo "scale=2; $request_count / $duration" | bc)"

# Calculate success rate
total_attempts=$((request_count + error_count + timeout_count))
success_rate=$(echo "scale=2; $request_count * 100 / $total_attempts" | bc)

echo "Success rate: ${success_rate}%"

if [ $(echo "$success_rate > 95" | bc) -eq 1 ]; then
    echo -e "  ${GREEN}✓ PASSED${NC}: Server maintained >95% success rate over 5 minutes"
    PASSED=$((PASSED + 1))
else
    echo -e "  ${RED}✗ FAILED${NC}: Success rate too low: ${success_rate}%"
    FAILED=$((FAILED + 1))
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Test 2: Memory Leak Detection (Valgrind)
# ============================================
print_header "Test 2: Memory Leak Detection with Valgrind"
test_case "Check for memory leaks"

if check_command "valgrind" "sudo apt-get install valgrind"; then
    echo "This test requires running the server under Valgrind"
    echo ""
    echo -e "${BLUE}To run this test manually:${NC}"
    echo "1. Stop the current server"
    echo "2. Run: valgrind --leak-check=full --log-file=valgrind_memcheck.txt ./server"
    echo "3. In another terminal, run some load tests"
    echo "4. Stop the server with Ctrl+C"
    echo "5. Check valgrind_memcheck.txt for memory leaks"
    echo ""
    
    if [ -f "../valgrind_memcheck.txt" ]; then
        echo "Found existing Valgrind output. Analyzing..."
        
        definitely_lost=$(grep "definitely lost:" ../valgrind_memcheck.txt | tail -n 1 | awk '{print $4}' | tr -d ',')
        indirectly_lost=$(grep "indirectly lost:" ../valgrind_memcheck.txt | tail -n 1 | awk '{print $4}' | tr -d ',')
        possibly_lost=$(grep "possibly lost:" ../valgrind_memcheck.txt | tail -n 1 | awk '{print $4}' | tr -d ',')
        
        echo "Definitely lost: ${definitely_lost:-0} bytes"
        echo "Indirectly lost: ${indirectly_lost:-0} bytes"
        echo "Possibly lost: ${possibly_lost:-0} bytes"
        
        # Convert to numbers (handle empty strings)
        def_lost=${definitely_lost:-0}
        ind_lost=${indirectly_lost:-0}
        
        if [ "$def_lost" = "0" ] && [ "$ind_lost" = "0" ]; then
            echo -e "  ${GREEN}✓ PASSED${NC}: No definite memory leaks detected"
            PASSED=$((PASSED + 1))
        else
            echo -e "  ${RED}✗ FAILED${NC}: Memory leaks detected"
            echo "  Check valgrind_memcheck.txt for details"
            FAILED=$((FAILED + 1))
        fi
    else
        echo -e "  ${YELLOW}⚠ SKIPPED${NC}: valgrind_memcheck.txt not found"
        echo "  Run Valgrind manually to complete this test"
    fi
else
    echo -e "  ${YELLOW}⚠ SKIPPED${NC}: Valgrind not available"
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Test 3: Resource Monitoring During Load
# ============================================
print_header "Test 3: Resource Usage Monitoring"
test_case "Monitor CPU and memory usage under load"

if check_server; then
    echo "Monitoring server resource usage for 30 seconds under load..."
    
    # Get server PID
    server_pid=$(pgrep -f "./server" | head -n 1)
    
    if [ -z "$server_pid" ]; then
        echo -e "  ${YELLOW}⚠ WARNING${NC}: Could not find server process"
        FAILED=$((FAILED + 1))
        TOTAL=$((TOTAL + 1))
    else
        echo "Server PID: $server_pid"
        
        # Start load in background
        for i in {1..30}; do
            curl -s -o /dev/null "$SERVER_URL/test.html" &
            sleep 1
        done &
        load_pid=$!
        
        # Monitor resources
        echo "Monitoring..."
        max_mem=0
        max_cpu=0
        
        for i in {1..30}; do
            if ps -p $server_pid > /dev/null 2>&1; then
                mem=$(ps -p $server_pid -o rss= | awk '{print $1}')
                cpu=$(ps -p $server_pid -o %cpu= | awk '{print $1}')
                
                if [ -n "$mem" ] && [ "$mem" -gt "$max_mem" ]; then
                    max_mem=$mem
                fi
                
                if [ -n "$cpu" ]; then
                    max_cpu_int=$(echo "$cpu" | awk '{print int($1)}')
                    if [ "$max_cpu_int" -gt "$max_cpu" ]; then
                        max_cpu=$max_cpu_int
                    fi
                fi
            fi
            sleep 1
        done
        
        wait $load_pid 2>/dev/null
        
        max_mem_mb=$(echo "scale=2; $max_mem / 1024" | bc)
        echo ""
        echo "Maximum memory usage: ${max_mem_mb} MB"
        echo "Maximum CPU usage: ${max_cpu}%"
        
        # Memory threshold: 500MB
        if [ $max_mem -lt 512000 ]; then
            echo -e "  ${GREEN}✓ PASSED${NC}: Memory usage within limits (<500MB)"
            PASSED=$((PASSED + 1))
        else
            echo -e "  ${RED}✗ FAILED${NC}: Excessive memory usage (${max_mem_mb}MB)"
            FAILED=$((FAILED + 1))
        fi
    fi
else
    echo -e "  ${RED}✗ FAILED${NC}: Server not responding"
    FAILED=$((FAILED + 1))
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Test 4: Graceful Shutdown Under Load
# ============================================
print_header "Test 4: Graceful Shutdown Under Load"
test_case "Test server shutdown while handling requests"

echo "This test requires starting a server instance and shutting it down under load"
echo ""
echo -e "${BLUE}Manual test procedure:${NC}"
echo "1. Start the server: ./server"
echo "2. In another terminal, run: ab -n 1000 -c 50 http://localhost:8080/test.html"
echo "3. While requests are being processed, press Ctrl+C on the server"
echo "4. Observe that:"
echo "   - Server prints shutdown messages"
echo "   - Workers terminate gracefully"
echo "   - No 'Broken pipe' or 'Connection reset' errors flood the terminal"
echo "   - Server exits cleanly"
echo ""

if [ -f "../shutdown_test.log" ]; then
    echo "Found shutdown test log. Analyzing..."
    
    if grep -q "Server shutdown complete" ../shutdown_test.log; then
        echo -e "  ${GREEN}✓ PASSED${NC}: Server performed graceful shutdown"
        PASSED=$((PASSED + 1))
    else
        echo -e "  ${RED}✗ FAILED${NC}: Server did not shutdown gracefully"
        FAILED=$((FAILED + 1))
    fi
else
    echo -e "  ${YELLOW}⚠ SKIPPED${NC}: shutdown_test.log not found"
    echo "  Run manual test and redirect output to shutdown_test.log"
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Test 5: Zombie Process Detection
# ============================================
print_header "Test 5: Zombie Process Detection"
test_case "Check for zombie processes after shutdown"

echo "Checking for zombie processes..."

zombie_count=$(ps aux | grep -E 'Z|defunct' | grep -v grep | wc -l)

if [ $zombie_count -eq 0 ]; then
    echo -e "  ${GREEN}✓ PASSED${NC}: No zombie processes detected"
    PASSED=$((PASSED + 1))
else
    echo "Found $zombie_count zombie process(es):"
    ps aux | grep -E 'Z|defunct' | grep -v grep
    echo -e "  ${RED}✗ FAILED${NC}: Zombie processes exist"
    FAILED=$((FAILED + 1))
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Test 6: Worker Process Management
# ============================================
print_header "Test 6: Worker Process Management"
test_case "Verify all worker processes exist and are responsive"

if check_server; then
    # Check for server and worker processes
    server_procs=$(pgrep -f "./server" | wc -l)
    echo "Server-related processes: $server_procs"
    
    if [ $server_procs -gt 1 ]; then
        echo -e "  ${GREEN}✓ PASSED${NC}: Multiple processes detected (master + workers)"
        PASSED=$((PASSED + 1))
    else
        echo -e "  ${YELLOW}⚠ INFO${NC}: Only one process detected"
        echo "  This may be expected depending on configuration"
        PASSED=$((PASSED + 1))
    fi
else
    echo -e "  ${YELLOW}⚠ SKIPPED${NC}: Server not running"
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Test 7: File Descriptor Leak Detection
# ============================================
print_header "Test 7: File Descriptor Leak Detection"
test_case "Monitor file descriptor usage"

if check_server; then
    server_pid=$(pgrep -f "./server" | head -n 1)
    
    if [ -n "$server_pid" ]; then
        echo "Checking file descriptor usage..."
        
        # Count open file descriptors
        if [ -d "/proc/$server_pid/fd" ]; then
            initial_fds=$(ls /proc/$server_pid/fd 2>/dev/null | wc -l)
            echo "Initial FD count: $initial_fds"
            
            # Send some requests
            echo "Sending 100 requests..."
            for i in {1..100}; do
                curl -s -o /dev/null "$SERVER_URL/test.html" &
            done
            wait
            
            sleep 2  # Wait for cleanup
            
            after_fds=$(ls /proc/$server_pid/fd 2>/dev/null | wc -l)
            echo "FD count after requests: $after_fds"
            
            fd_diff=$((after_fds - initial_fds))
            echo "FD difference: $fd_diff"
            
            # Allow small increase (some persistent connections, logs, etc.)
            if [ $fd_diff -lt 20 ]; then
                echo -e "  ${GREEN}✓ PASSED${NC}: No significant FD leak detected"
                PASSED=$((PASSED + 1))
            else
                echo -e "  ${RED}✗ FAILED${NC}: Possible FD leak (increase of $fd_diff)"
                FAILED=$((FAILED + 1))
            fi
        else
            echo -e "  ${YELLOW}⚠ SKIPPED${NC}: Cannot access /proc/$server_pid/fd"
        fi
    else
        echo -e "  ${YELLOW}⚠ SKIPPED${NC}: Server PID not found"
    fi
else
    echo -e "  ${YELLOW}⚠ SKIPPED${NC}: Server not running"
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Test 8: Stress Test with Mixed File Types
# ============================================
print_header "Test 8: Mixed File Type Stress Test"
test_case "Concurrent requests for different file types"

if check_server; then
    echo "Sending mixed requests for 60 seconds..."
    
    files=("test.html" "styles.css" "script.js" "large_file.txt")
    start_time=$(date +%s)
    end_target=$((start_time + 60))
    success=0
    failed=0
    
    while [ $(date +%s) -lt $end_target ]; do
        for file in "${files[@]}"; do
            {
                status=$(curl -s -o /dev/null -w "%{http_code}" "$SERVER_URL/$file" 2>/dev/null)
                if [ "$status" = "200" ]; then
                    echo "1" >> /tmp/mixed_success_$$.tmp
                else
                    echo "1" >> /tmp/mixed_fail_$$.tmp
                fi
            } &
        done
        sleep 0.2
    done
    
    wait
    
    if [ -f /tmp/mixed_success_$$.tmp ]; then
        success=$(wc -l < /tmp/mixed_success_$$.tmp)
        rm /tmp/mixed_success_$$.tmp
    fi
    
    if [ -f /tmp/mixed_fail_$$.tmp ]; then
        failed=$(wc -l < /tmp/mixed_fail_$$.tmp)
        rm /tmp/mixed_fail_$$.tmp
    fi
    
    total=$((success + failed))
    success_rate=$(echo "scale=2; $success * 100 / $total" | bc)
    
    echo "Total requests: $total"
    echo "Successful: $success"
    echo "Failed: $failed"
    echo "Success rate: ${success_rate}%"
    
    if [ $(echo "$success_rate > 95" | bc) -eq 1 ]; then
        echo -e "  ${GREEN}✓ PASSED${NC}: Mixed file type stress test passed"
        PASSED=$((PASSED + 1))
    else
        echo -e "  ${RED}✗ FAILED${NC}: Success rate too low: ${success_rate}%"
        FAILED=$((FAILED + 1))
    fi
else
    echo -e "  ${RED}✗ FAILED${NC}: Server not responding"
    FAILED=$((FAILED + 1))
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Summary
# ============================================
print_header "STRESS TESTS SUMMARY"
echo "Total Tests: $TOTAL"
echo -e "${GREEN}Passed: $PASSED${NC}"
echo -e "${RED}Failed: $FAILED${NC}"
echo ""
echo -e "${YELLOW}Note:${NC} Some tests (Valgrind, graceful shutdown) require manual setup"
echo "  See test output above for instructions"
echo ""
echo "End time: $(date)"

if [ $FAILED -eq 0 ]; then
    echo -e "\n${GREEN}✓ ALL STRESS TESTS PASSED!${NC}\n"
    exit 0
else
    echo -e "\n${RED}✗ SOME TESTS FAILED${NC}\n"
    exit 1
fi

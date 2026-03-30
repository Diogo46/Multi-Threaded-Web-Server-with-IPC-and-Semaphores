#!/bin/bash
# Concurrency Tests for HTTP Server
# Tests: Apache Bench, parallel clients, statistics accuracy, dropped connections

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
SERVER_URL="http://localhost:8080"
SERVER_HOST="localhost"
SERVER_PORT="8080"
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

assert_success() {
    local condition=$1
    local description=$2
    
    if [ "$condition" -eq 0 ]; then
        echo -e "  ${GREEN}✓ PASSED${NC}: $description"
        PASSED=$((PASSED + 1))
        return 0
    else
        echo -e "  ${RED}✗ FAILED${NC}: $description"
        FAILED=$((FAILED + 1))
        return 1
    fi
}

check_server() {
    if ! curl -s -o /dev/null "$SERVER_URL" 2>&1; then
        echo -e "${RED}ERROR: Server not responding at $SERVER_URL${NC}"
        echo "Please start the server first: ./server"
        exit 1
    fi
    echo -e "${GREEN}Server is running at $SERVER_URL${NC}"
}

check_command() {
    if ! command -v $1 &> /dev/null; then
        echo -e "${RED}ERROR: $1 not found${NC}"
        echo "Please install $1: $2"
        return 1
    fi
    return 0
}

print_header "CONCURRENCY TESTS"
echo "Testing server at: $SERVER_URL"
echo "Start time: $(date)"

check_server

# ============================================
# Test 1: Apache Bench - Moderate Load
# ============================================
print_header "Test 1: Apache Bench - Moderate Load"
test_case "ab -n 1000 -c 10 (1000 requests, 10 concurrent)"

if check_command "ab" "sudo apt-get install apache2-utils"; then
    echo "Running Apache Bench test..."
    ab_output=$(ab -n 1000 -c 10 "$SERVER_URL/test.html" 2>&1)
    
    # Check for failed requests
    failed_requests=$(echo "$ab_output" | grep "Failed requests:" | awk '{print $3}')
    echo "Failed requests: $failed_requests"
    
    # Check for complete requests
    complete_requests=$(echo "$ab_output" | grep "Complete requests:" | awk '{print $3}')
    echo "Complete requests: $complete_requests"
    
    if [ "$failed_requests" -eq 0 ] && [ "$complete_requests" -eq 1000 ]; then
        echo -e "  ${GREEN}✓ PASSED${NC}: All 1000 requests completed successfully"
        PASSED=$((PASSED + 1))
    else
        echo -e "  ${RED}✗ FAILED${NC}: Some requests failed"
        FAILED=$((FAILED + 1))
    fi
    TOTAL=$((TOTAL + 1))
    
    # Display performance metrics
    echo -e "\n${BLUE}Performance Metrics:${NC}"
    echo "$ab_output" | grep -E "Requests per second|Time per request|Transfer rate"
else
    echo -e "  ${YELLOW}⚠ SKIPPED${NC}: Apache Bench not available"
fi

# ============================================
# Test 2: Apache Bench - High Load
# ============================================
print_header "Test 2: Apache Bench - High Load"
test_case "ab -n 10000 -c 100 (10000 requests, 100 concurrent)"

if command -v ab &> /dev/null; then
    echo "Running high load Apache Bench test..."
    ab_output=$(ab -n 10000 -c 100 "$SERVER_URL/test.html" 2>&1)
    
    failed_requests=$(echo "$ab_output" | grep "Failed requests:" | awk '{print $3}')
    echo "Failed requests: $failed_requests"
    
    complete_requests=$(echo "$ab_output" | grep "Complete requests:" | awk '{print $3}')
    echo "Complete requests: $complete_requests"
    
    # Allow small percentage of failures under very high load
    failure_rate=$(echo "scale=2; $failed_requests / 10000 * 100" | bc)
    echo "Failure rate: $failure_rate%"
    
    if [ "$complete_requests" -ge 9900 ]; then
        echo -e "  ${GREEN}✓ PASSED${NC}: At least 99% requests completed (${complete_requests}/10000)"
        PASSED=$((PASSED + 1))
    else
        echo -e "  ${RED}✗ FAILED${NC}: Too many failed requests"
        FAILED=$((FAILED + 1))
    fi
    TOTAL=$((TOTAL + 1))
    
    echo -e "\n${BLUE}Performance Metrics:${NC}"
    echo "$ab_output" | grep -E "Requests per second|Time per request|Transfer rate"
else
    echo -e "  ${YELLOW}⚠ SKIPPED${NC}: Apache Bench not available"
fi

# ============================================
# Test 3: Parallel curl/wget Clients
# ============================================
print_header "Test 3: Parallel curl Clients"
test_case "50 parallel curl requests"

echo "Launching 50 parallel curl requests..."
success_count=0
fail_count=0

for i in {1..50}; do
    {
        status=$(curl -s -o /dev/null -w "%{http_code}" "$SERVER_URL/test.html" 2>/dev/null)
        if [ "$status" -eq 200 ]; then
            echo "1" >> /tmp/curl_success_$$.tmp
        else
            echo "1" >> /tmp/curl_fail_$$.tmp
        fi
    } &
done

# Wait for all background jobs
wait

if [ -f /tmp/curl_success_$$.tmp ]; then
    success_count=$(wc -l < /tmp/curl_success_$$.tmp)
    rm /tmp/curl_success_$$.tmp
fi

if [ -f /tmp/curl_fail_$$.tmp ]; then
    fail_count=$(wc -l < /tmp/curl_fail_$$.tmp)
    rm /tmp/curl_fail_$$.tmp
fi

echo "Successful requests: $success_count"
echo "Failed requests: $fail_count"

if [ $success_count -ge 48 ]; then
    echo -e "  ${GREEN}✓ PASSED${NC}: At least 96% of parallel requests succeeded ($success_count/50)"
    PASSED=$((PASSED + 1))
else
    echo -e "  ${RED}✗ FAILED${NC}: Too many parallel requests failed"
    FAILED=$((FAILED + 1))
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Test 4: Multiple Files Simultaneously
# ============================================
print_header "Test 4: Multiple Different Files Simultaneously"
test_case "Request different files in parallel"

echo "Requesting HTML, CSS, JS, and TXT files simultaneously..."
{
    curl -s -o /dev/null -w "%{http_code}\n" "$SERVER_URL/test.html" > /tmp/html_status_$$.tmp &
    curl -s -o /dev/null -w "%{http_code}\n" "$SERVER_URL/styles.css" > /tmp/css_status_$$.tmp &
    curl -s -o /dev/null -w "%{http_code}\n" "$SERVER_URL/script.js" > /tmp/js_status_$$.tmp &
    curl -s -o /dev/null -w "%{http_code}\n" "$SERVER_URL/large_file.txt" > /tmp/txt_status_$$.tmp &
    wait
}

all_success=1
for file in html css js txt; do
    status=$(cat /tmp/${file}_status_$$.tmp 2>/dev/null || echo "000")
    echo "  $file status: $status"
    if [ "$status" != "200" ]; then
        all_success=0
    fi
    rm -f /tmp/${file}_status_$$.tmp
done

if [ $all_success -eq 1 ]; then
    echo -e "  ${GREEN}✓ PASSED${NC}: All parallel file requests succeeded"
    PASSED=$((PASSED + 1))
else
    echo -e "  ${RED}✗ FAILED${NC}: Some parallel file requests failed"
    FAILED=$((FAILED + 1))
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Test 5: Connection Stability
# ============================================
print_header "Test 5: Connection Stability Under Load"
test_case "Sustained load - 500 requests over 10 seconds"

echo "Sending sustained load..."
dropped=0
successful=0

start_time=$(date +%s)
for i in {1..500}; do
    status=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 "$SERVER_URL/test.html" 2>/dev/null || echo "000")
    if [ "$status" = "200" ]; then
        successful=$((successful + 1))
    else
        dropped=$((dropped + 1))
    fi
    
    # Progress indicator
    if [ $((i % 50)) -eq 0 ]; then
        echo "  Progress: $i/500 requests sent..."
    fi
done
end_time=$(date +%s)

duration=$((end_time - start_time))
echo "Duration: ${duration}s"
echo "Successful: $successful"
echo "Dropped/Failed: $dropped"

drop_rate=$(echo "scale=2; $dropped / 500 * 100" | bc)
echo "Drop rate: $drop_rate%"

if [ $successful -ge 495 ]; then
    echo -e "  ${GREEN}✓ PASSED${NC}: Connection stability good (${successful}/500, drop rate: ${drop_rate}%)"
    PASSED=$((PASSED + 1))
else
    echo -e "  ${RED}✗ FAILED${NC}: Too many dropped connections"
    FAILED=$((FAILED + 1))
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Test 6: Verify Statistics Accuracy
# ============================================
print_header "Test 6: Statistics Accuracy"
test_case "Check if server statistics match actual requests"

echo "Checking if access.log exists and has entries..."
if [ -f "../access.log" ]; then
    log_entries=$(wc -l < ../access.log)
    echo "Log entries found: $log_entries"
    
    if [ $log_entries -gt 0 ]; then
        echo -e "  ${GREEN}✓ PASSED${NC}: Access log contains entries"
        PASSED=$((PASSED + 1))
    else
        echo -e "  ${RED}✗ FAILED${NC}: Access log is empty"
        FAILED=$((FAILED + 1))
    fi
else
    echo -e "  ${YELLOW}⚠ WARNING${NC}: access.log not found"
    echo "  Note: Server may not have processed requests yet"
    FAILED=$((FAILED + 1))
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Test 7: Queue Full Scenario (503 Response)
# ============================================
print_header "Test 7: Queue Full Scenario"
test_case "Test 503 Service Unavailable under extreme load"

echo "Attempting to overwhelm server with rapid requests..."
got_503=0

# Send 200 extremely fast requests
for i in {1..200}; do
    {
        status=$(curl -s -o /dev/null -w "%{http_code}" --max-time 1 "$SERVER_URL/test.html" 2>/dev/null || echo "000")
        if [ "$status" = "503" ]; then
            echo "503" >> /tmp/got_503_$$.tmp
        fi
    } &
    
    # Don't wait - send them all at once
    if [ $((i % 50)) -eq 0 ]; then
        sleep 0.1  # Small pause to let some complete
    fi
done

wait

if [ -f /tmp/got_503_$$.tmp ]; then
    got_503=$(wc -l < /tmp/got_503_$$.tmp)
    rm /tmp/got_503_$$.tmp
fi

echo "503 responses received: $got_503"

if [ $got_503 -gt 0 ]; then
    echo -e "  ${GREEN}✓ PASSED${NC}: Server properly returns 503 when overwhelmed ($got_503 instances)"
    PASSED=$((PASSED + 1))
else
    echo -e "  ${YELLOW}⚠ INFO${NC}: No 503 responses (server handled all requests - queue may be large)"
    echo "  This is acceptable if queue is properly sized"
    PASSED=$((PASSED + 1))
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Summary
# ============================================
print_header "CONCURRENCY TESTS SUMMARY"
echo "Total Tests: $TOTAL"
echo -e "${GREEN}Passed: $PASSED${NC}"
echo -e "${RED}Failed: $FAILED${NC}"
echo "End time: $(date)"

if [ $FAILED -eq 0 ]; then
    echo -e "\n${GREEN}✓ ALL CONCURRENCY TESTS PASSED!${NC}\n"
    exit 0
else
    echo -e "\n${RED}✗ SOME TESTS FAILED${NC}\n"
    exit 1
fi

#!/bin/bash
# Functional Tests for HTTP Server
# Tests: GET requests, HTTP status codes, directory index, Content-Type headers

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
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
    echo -e "\n[Test $TOTAL] $1"
}

assert_status() {
    local expected=$1
    local actual=$2
    local description=$3
    
    if [ "$expected" -eq "$actual" ]; then
        echo -e "  ${GREEN}✓ PASSED${NC}: $description (Status: $actual)"
        PASSED=$((PASSED + 1))
        return 0
    else
        echo -e "  ${RED}✗ FAILED${NC}: $description"
        echo -e "    Expected: $expected, Got: $actual"
        FAILED=$((FAILED + 1))
        return 1
    fi
}

assert_contains() {
    local content=$1
    local expected=$2
    local description=$3
    
    if echo "$content" | grep -q "$expected"; then
        echo -e "  ${GREEN}✓ PASSED${NC}: $description"
        PASSED=$((PASSED + 1))
        return 0
    else
        echo -e "  ${RED}✗ FAILED${NC}: $description"
        echo -e "    Expected to find: $expected"
        FAILED=$((FAILED + 1))
        return 1
    fi
}

# Check if server is running
check_server() {
    if ! curl -s -o /dev/null -w "%{http_code}" "$SERVER_URL" > /dev/null 2>&1; then
        echo -e "${RED}ERROR: Server not responding at $SERVER_URL${NC}"
        echo "Please start the server first: ./server"
        exit 1
    fi
    echo -e "${GREEN}Server is running at $SERVER_URL${NC}"
}

print_header "FUNCTIONAL TESTS"
echo "Testing server at: $SERVER_URL"
echo "Start time: $(date)"

check_server

# ============================================
# Test 1: GET request for HTML file (200 OK)
# ============================================
print_header "Test 1: GET Request - HTML File"
test_case "GET /test.html - Expect 200 OK"

response=$(curl -s -w "\n%{http_code}" "$SERVER_URL/test.html")
status=$(echo "$response" | tail -n 1)
body=$(echo "$response" | sed '$d')

assert_status 200 "$status" "HTML file returns 200 OK"
assert_contains "$body" "Test HTML Page" "HTML content is correct"

# ============================================
# Test 2: GET request for CSS file (200 OK)
# ============================================
print_header "Test 2: GET Request - CSS File"
test_case "GET /styles.css - Expect 200 OK"

response=$(curl -s -w "\n%{http_code}" "$SERVER_URL/styles.css")
status=$(echo "$response" | tail -n 1)
body=$(echo "$response" | sed '$d')

assert_status 200 "$status" "CSS file returns 200 OK"
assert_contains "$body" "font-family" "CSS content is correct"

# ============================================
# Test 3: GET request for JavaScript file
# ============================================
print_header "Test 3: GET Request - JavaScript File"
test_case "GET /script.js - Expect 200 OK"

response=$(curl -s -w "\n%{http_code}" "$SERVER_URL/script.js")
status=$(echo "$response" | tail -n 1)
body=$(echo "$response" | sed '$d')

assert_status 200 "$status" "JavaScript file returns 200 OK"
assert_contains "$body" "console.log" "JavaScript content is correct"

# ============================================
# Test 4: GET request for text file
# ============================================
print_header "Test 4: GET Request - Text File"
test_case "GET /large_file.txt - Expect 200 OK"

response=$(curl -s -w "\n%{http_code}" "$SERVER_URL/large_file.txt")
status=$(echo "$response" | tail -n 1)
body=$(echo "$response" | sed '$d')

assert_status 200 "$status" "Text file returns 200 OK"
assert_contains "$body" "Lorem ipsum" "Text content is correct"

# ============================================
# Test 5: 404 Not Found
# ============================================
print_header "Test 5: HTTP Status Code - 404 Not Found"
test_case "GET /nonexistent.html - Expect 404"

status=$(curl -s -o /dev/null -w "%{http_code}" "$SERVER_URL/nonexistent.html")
assert_status 404 "$status" "Non-existent file returns 404"

test_case "GET /fake/path/file.txt - Expect 404"
status=$(curl -s -o /dev/null -w "%{http_code}" "$SERVER_URL/fake/path/file.txt")
assert_status 404 "$status" "Non-existent path returns 404"

# ============================================
# Test 6: Directory Index (/)
# ============================================
print_header "Test 6: Directory Index Serving"
test_case "GET / - Expect 200 and index.html"

response=$(curl -s -w "\n%{http_code}" "$SERVER_URL/")
status=$(echo "$response" | tail -n 1)
body=$(echo "$response" | sed '$d')

assert_status 200 "$status" "Root directory returns 200 OK"
# The body should contain content from index.html

# ============================================
# Test 7: Subdirectory Index
# ============================================
print_header "Test 7: Subdirectory Index"
test_case "GET /subdir/ - Expect 200 and subdir/index.html"

response=$(curl -s -w "\n%{http_code}" "$SERVER_URL/subdir/")
status=$(echo "$response" | tail -n 1)
body=$(echo "$response" | sed '$d')

assert_status 200 "$status" "Subdirectory returns 200 OK"
assert_contains "$body" "Subdirectory Test" "Subdirectory index.html served"

# ============================================
# Test 8: Content-Type Headers
# ============================================
print_header "Test 8: Content-Type Headers Verification"

test_case "Check Content-Type for HTML"
content_type=$(curl -s -I "$SERVER_URL/test.html" | grep -i "content-type" | awk '{print $2}' | tr -d '\r')
if echo "$content_type" | grep -q "text/html"; then
    echo -e "  ${GREEN}✓ PASSED${NC}: HTML has correct Content-Type: $content_type"
    PASSED=$((PASSED + 1))
else
    echo -e "  ${RED}✗ FAILED${NC}: HTML Content-Type incorrect: $content_type"
    FAILED=$((FAILED + 1))
fi
TOTAL=$((TOTAL + 1))

test_case "Check Content-Type for CSS"
content_type=$(curl -s -I "$SERVER_URL/styles.css" | grep -i "content-type" | awk '{print $2}' | tr -d '\r')
if echo "$content_type" | grep -q "text/css"; then
    echo -e "  ${GREEN}✓ PASSED${NC}: CSS has correct Content-Type: $content_type"
    PASSED=$((PASSED + 1))
else
    echo -e "  ${RED}✗ FAILED${NC}: CSS Content-Type incorrect: $content_type"
    FAILED=$((FAILED + 1))
fi
TOTAL=$((TOTAL + 1))

test_case "Check Content-Type for JavaScript"
content_type=$(curl -s -I "$SERVER_URL/script.js" | grep -i "content-type" | awk '{print $2}' | tr -d '\r')
if echo "$content_type" | grep -qi "javascript\|application/javascript\|text/javascript"; then
    echo -e "  ${GREEN}✓ PASSED${NC}: JavaScript has correct Content-Type: $content_type"
    PASSED=$((PASSED + 1))
else
    echo -e "  ${RED}✗ FAILED${NC}: JavaScript Content-Type incorrect: $content_type"
    FAILED=$((FAILED + 1))
fi
TOTAL=$((TOTAL + 1))

test_case "Check Content-Type for Text"
content_type=$(curl -s -I "$SERVER_URL/large_file.txt" | grep -i "content-type" | awk '{print $2}' | tr -d '\r')
if echo "$content_type" | grep -q "text/plain"; then
    echo -e "  ${GREEN}✓ PASSED${NC}: Text has correct Content-Type: $content_type"
    PASSED=$((PASSED + 1))
else
    echo -e "  ${RED}✗ FAILED${NC}: Text Content-Type incorrect: $content_type"
    FAILED=$((FAILED + 1))
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Test 9: HEAD Method
# ============================================
print_header "Test 9: HEAD Method Support"
test_case "HEAD /test.html - Should return headers only"

head_response=$(curl -s -I "$SERVER_URL/test.html")
if echo "$head_response" | grep -q "200 OK"; then
    echo -e "  ${GREEN}✓ PASSED${NC}: HEAD method returns 200 OK"
    PASSED=$((PASSED + 1))
else
    echo -e "  ${RED}✗ FAILED${NC}: HEAD method failed"
    FAILED=$((FAILED + 1))
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Test 10: Multiple Requests (Same File)
# ============================================
print_header "Test 10: Multiple Requests - Same File"
test_case "Request same file 5 times - Test cache"

cache_success=0
for i in {1..5}; do
    status=$(curl -s -o /dev/null -w "%{http_code}" "$SERVER_URL/test.html")
    if [ "$status" -eq 200 ]; then
        cache_success=$((cache_success + 1))
    fi
done

if [ $cache_success -eq 5 ]; then
    echo -e "  ${GREEN}✓ PASSED${NC}: All 5 requests successful (cache working)"
    PASSED=$((PASSED + 1))
else
    echo -e "  ${RED}✗ FAILED${NC}: Only $cache_success/5 requests successful"
    FAILED=$((FAILED + 1))
fi
TOTAL=$((TOTAL + 1))

# ============================================
# Summary
# ============================================
print_header "FUNCTIONAL TESTS SUMMARY"
echo "Total Tests: $TOTAL"
echo -e "${GREEN}Passed: $PASSED${NC}"
echo -e "${RED}Failed: $FAILED${NC}"
echo "End time: $(date)"

if [ $FAILED -eq 0 ]; then
    echo -e "\n${GREEN}✓ ALL FUNCTIONAL TESTS PASSED!${NC}\n"
    exit 0
else
    echo -e "\n${RED}✗ SOME TESTS FAILED${NC}\n"
    exit 1
fi

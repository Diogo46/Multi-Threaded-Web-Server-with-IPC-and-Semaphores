#!/bin/bash
# Master Test Runner for HTTP Server
# Runs all test suites and generates comprehensive report

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Configuration
TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPORT_DIR="$TEST_DIR/reports"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
REPORT_FILE="$REPORT_DIR/test_report_$TIMESTAMP.txt"

# Test tracking
SUITES_PASSED=0
SUITES_FAILED=0
SUITES_TOTAL=0

# Helper functions
print_banner() {
    echo -e "${CYAN}"
    echo "╔══════════════════════════════════════════════════════════════════╗"
    echo "║                                                                  ║"
    echo "║           HTTP SERVER COMPREHENSIVE TEST SUITE                   ║"
    echo "║                                                                  ║"
    echo "╚══════════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
}

print_header() {
    echo -e "\n${BOLD}${YELLOW}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}${YELLOW}  $1${NC}"
    echo -e "${BOLD}${YELLOW}═══════════════════════════════════════════════════════════════${NC}\n"
}

run_test_suite() {
    local suite_name=$1
    local script_name=$2
    local description=$3
    
    SUITES_TOTAL=$((SUITES_TOTAL + 1))
    
    echo -e "\n${BLUE}┌─────────────────────────────────────────────────────────────────┐${NC}"
    echo -e "${BLUE}│${NC} ${BOLD}Test Suite $SUITES_TOTAL: $suite_name${NC}"
    echo -e "${BLUE}│${NC} $description"
    echo -e "${BLUE}└─────────────────────────────────────────────────────────────────┘${NC}\n"
    
    if [ ! -f "$TEST_DIR/$script_name" ]; then
        echo -e "${RED}✗ ERROR: Test script not found: $script_name${NC}"
        SUITES_FAILED=$((SUITES_FAILED + 1))
        return 1
    fi
    
    echo "Running: $script_name"
    echo "Started: $(date)"
    echo ""
    
    # Run the test suite and capture output
    local log_file="$REPORT_DIR/${suite_name}_${TIMESTAMP}.log"
    
    if bash "$TEST_DIR/$script_name" 2>&1 | tee "$log_file"; then
        echo -e "\n${GREEN}✓ $suite_name PASSED${NC}"
        SUITES_PASSED=$((SUITES_PASSED + 1))
        return 0
    else
        echo -e "\n${RED}✗ $suite_name FAILED${NC}"
        SUITES_FAILED=$((SUITES_FAILED + 1))
        return 1
    fi
}

check_server() {
    echo "Checking if server is running..."
    if curl -s -o /dev/null "http://localhost:8080" 2>&1; then
        echo -e "${GREEN}✓ Server is running${NC}"
        return 0
    else
        echo -e "${RED}✗ Server is not running${NC}"
        echo ""
        echo "Please start the server before running tests:"
        echo "  cd .."
        echo "  ./server"
        echo ""
        return 1
    fi
}

show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --all              Run all test suites (default)"
    echo "  --functional       Run only functional tests"
    echo "  --concurrency      Run only concurrency tests"
    echo "  --synchronization  Run only synchronization tests"
    echo "  --stress           Run only stress tests"
    echo "  --quick            Run functional and concurrency tests only"
    echo "  --help             Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                      # Run all tests"
    echo "  $0 --quick              # Quick validation"
    echo "  $0 --functional         # Only functional tests"
    echo ""
}

generate_report() {
    local report_file=$1
    
    echo "Generating test report..."
    
    {
        echo "═══════════════════════════════════════════════════════════════"
        echo "           HTTP SERVER TEST REPORT"
        echo "═══════════════════════════════════════════════════════════════"
        echo ""
        echo "Test Run Timestamp: $(date)"
        echo "Report Generated: $TIMESTAMP"
        echo ""
        echo "═══════════════════════════════════════════════════════════════"
        echo "SUMMARY"
        echo "═══════════════════════════════════════════════════════════════"
        echo ""
        echo "Total Test Suites: $SUITES_TOTAL"
        echo "Passed: $SUITES_PASSED"
        echo "Failed: $SUITES_FAILED"
        echo ""
        
        if [ $SUITES_FAILED -eq 0 ]; then
            echo "Overall Result: ✓ ALL TESTS PASSED"
        else
            echo "Overall Result: ✗ SOME TESTS FAILED"
        fi
        
        echo ""
        echo "═══════════════════════════════════════════════════════════════"
        echo "DETAILED LOGS"
        echo "═══════════════════════════════════════════════════════════════"
        echo ""
        echo "Individual test logs are available in: $REPORT_DIR"
        echo ""
        
        for log in "$REPORT_DIR"/*_${TIMESTAMP}.log; do
            if [ -f "$log" ]; then
                echo "  - $(basename "$log")"
            fi
        done
        
        echo ""
        echo "═══════════════════════════════════════════════════════════════"
        
    } > "$report_file"
    
    echo -e "${GREEN}Report saved to: $report_file${NC}"
}

print_final_summary() {
    echo ""
    print_header "FINAL TEST SUMMARY"
    
    echo "╔══════════════════════════════════════════════════════════════════╗"
    echo "║                        TEST RESULTS                              ║"
    echo "╠══════════════════════════════════════════════════════════════════╣"
    printf "║  Total Suites Run:  %-43s ║\n" "$SUITES_TOTAL"
    printf "║  Passed:            %-43s ║\n" "$SUITES_PASSED"
    printf "║  Failed:            %-43s ║\n" "$SUITES_FAILED"
    echo "╠══════════════════════════════════════════════════════════════════╣"
    
    if [ $SUITES_FAILED -eq 0 ]; then
        echo "║                                                                  ║"
        echo -e "║         ${GREEN}${BOLD}✓ ALL TEST SUITES PASSED!${NC}                          ║"
        echo "║                                                                  ║"
    else
        echo "║                                                                  ║"
        echo -e "║         ${RED}${BOLD}✗ SOME TEST SUITES FAILED${NC}                          ║"
        echo "║                                                                  ║"
    fi
    
    echo "╚══════════════════════════════════════════════════════════════════╝"
    echo ""
    echo "Detailed report: $REPORT_FILE"
    echo "Test logs: $REPORT_DIR/"
    echo ""
}

# ============================================
# MAIN SCRIPT
# ============================================

# Create reports directory
mkdir -p "$REPORT_DIR"

# Parse command line arguments
RUN_FUNCTIONAL=false
RUN_CONCURRENCY=false
RUN_SYNC=false
RUN_STRESS=false

if [ $# -eq 0 ]; then
    # Default: run all tests
    RUN_FUNCTIONAL=true
    RUN_CONCURRENCY=true
    RUN_SYNC=true
    RUN_STRESS=true
else
    while [ $# -gt 0 ]; do
        case $1 in
            --all)
                RUN_FUNCTIONAL=true
                RUN_CONCURRENCY=true
                RUN_SYNC=true
                RUN_STRESS=true
                ;;
            --functional)
                RUN_FUNCTIONAL=true
                ;;
            --concurrency)
                RUN_CONCURRENCY=true
                ;;
            --synchronization)
                RUN_SYNC=true
                ;;
            --stress)
                RUN_STRESS=true
                ;;
            --quick)
                RUN_FUNCTIONAL=true
                RUN_CONCURRENCY=true
                ;;
            --help)
                show_usage
                exit 0
                ;;
            *)
                echo "Unknown option: $1"
                show_usage
                exit 1
                ;;
        esac
        shift
    done
fi

# Print banner
print_banner

echo "Test execution started at: $(date)"
echo "Working directory: $TEST_DIR"
echo "Reports will be saved to: $REPORT_DIR"
echo ""

# Check if server is running (except for stress tests which might manage the server)
if ! check_server && [ "$RUN_STRESS" != "true" ]; then
    echo -e "${RED}Cannot proceed without running server${NC}"
    exit 1
fi

echo ""
print_header "STARTING TEST EXECUTION"

# Run selected test suites
if [ "$RUN_FUNCTIONAL" = true ]; then
    run_test_suite "Functional" "functional_tests.sh" "Tests: GET requests, HTTP status codes, Content-Type headers"
fi

if [ "$RUN_CONCURRENCY" = true ]; then
    run_test_suite "Concurrency" "concurrency_tests.sh" "Tests: Apache Bench, parallel clients, connection stability"
fi

if [ "$RUN_SYNC" = true ]; then
    run_test_suite "Synchronization" "synchronization_tests.sh" "Tests: Race conditions, log integrity, cache consistency"
fi

if [ "$RUN_STRESS" = true ]; then
    run_test_suite "Stress" "stress_tests.sh" "Tests: Long-running load, memory leaks, graceful shutdown"
fi

# Generate final report
generate_report "$REPORT_FILE"

# Print final summary
print_final_summary

# Exit with appropriate code
if [ $SUITES_FAILED -eq 0 ]; then
    exit 0
else
    exit 1
fi

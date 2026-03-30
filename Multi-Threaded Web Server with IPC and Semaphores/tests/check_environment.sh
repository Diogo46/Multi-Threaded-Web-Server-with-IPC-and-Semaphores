#!/bin/bash
# Pre-Test Checker - Verifies environment is ready for testing

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

ISSUES=0

echo -e "${BLUE}╔══════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║           HTTP SERVER TEST ENVIRONMENT CHECKER                   ║${NC}"
echo -e "${BLUE}╚══════════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Check 1: Test scripts exist and are executable
echo -e "${YELLOW}[1] Checking test scripts...${NC}"
scripts=("functional_tests.sh" "concurrency_tests.sh" "synchronization_tests.sh" "stress_tests.sh" "run_all_tests.sh")
for script in "${scripts[@]}"; do
    if [ -f "$script" ]; then
        if [ -x "$script" ]; then
            echo -e "  ${GREEN}✓${NC} $script (executable)"
        else
            echo -e "  ${RED}✗${NC} $script (not executable)"
            echo -e "    Fix: chmod +x $script"
            ISSUES=$((ISSUES + 1))
        fi
    else
        echo -e "  ${RED}✗${NC} $script (missing)"
        ISSUES=$((ISSUES + 1))
    fi
done

# Check 2: Test files in www/
echo -e "\n${YELLOW}[2] Checking test files in www/...${NC}"
test_files=("../www/test.html" "../www/styles.css" "../www/script.js" "../www/large_file.txt" "../www/subdir/index.html")
for file in "${test_files[@]}"; do
    if [ -f "$file" ]; then
        echo -e "  ${GREEN}✓${NC} $(basename $file)"
    else
        echo -e "  ${RED}✗${NC} $(basename $file) (missing)"
        ISSUES=$((ISSUES + 1))
    fi
done

# Check 3: Required commands
echo -e "\n${YELLOW}[3] Checking required commands...${NC}"
commands=("curl:curl" "ab:apache2-utils" "bc:bc" "timeout:coreutils")
for cmd_info in "${commands[@]}"; do
    IFS=':' read -r cmd pkg <<< "$cmd_info"
    if command -v $cmd &> /dev/null; then
        echo -e "  ${GREEN}✓${NC} $cmd"
    else
        echo -e "  ${RED}✗${NC} $cmd (not found)"
        echo -e "    Install: sudo apt-get install $pkg"
        ISSUES=$((ISSUES + 1))
    fi
done

# Check 4: Optional tools for advanced tests
echo -e "\n${YELLOW}[4] Checking optional tools (for manual tests)...${NC}"
opt_commands=("valgrind:valgrind" "pgrep:procps")
for cmd_info in "${opt_commands[@]}"; do
    IFS=':' read -r cmd pkg <<< "$cmd_info"
    if command -v $cmd &> /dev/null; then
        echo -e "  ${GREEN}✓${NC} $cmd"
    else
        echo -e "  ${YELLOW}⚠${NC} $cmd (not found - optional for manual tests)"
        echo -e "    Install: sudo apt-get install $pkg"
    fi
done

# Check 5: Server executable
echo -e "\n${YELLOW}[5] Checking server executable...${NC}"
if [ -f "../server" ]; then
    if [ -x "../server" ]; then
        echo -e "  ${GREEN}✓${NC} server executable exists"
    else
        echo -e "  ${RED}✗${NC} server exists but not executable"
        echo -e "    Fix: chmod +x ../server"
        ISSUES=$((ISSUES + 1))
    fi
else
    echo -e "  ${RED}✗${NC} server executable not found"
    echo -e "    Build it: cd .. && make"
    ISSUES=$((ISSUES + 1))
fi

# Check 6: Server configuration
echo -e "\n${YELLOW}[6] Checking server configuration...${NC}"
if [ -f "../server.conf" ]; then
    echo -e "  ${GREEN}✓${NC} server.conf exists"

    # Check key settings (expect KEY=VALUE format)
    port=$(grep -E '^PORT=' ../server.conf 2>/dev/null | head -n1 | cut -d'=' -f2)
    if [ -n "$port" ]; then
        echo -e "  ${BLUE}ℹ${NC} Configured port: $port"
    fi
else
    echo -e "  ${YELLOW}⚠${NC} server.conf not found (will use defaults)"
fi

# Check 7: Is server running?
echo -e "\n${YELLOW}[7] Checking if server is running...${NC}"
if curl -s -o /dev/null "http://localhost:8080" 2>&1; then
    echo -e "  ${GREEN}✓${NC} Server is responding at http://localhost:8080"
    
    # Get server PID
    server_pid=$(pgrep -f "./server" | head -n 1)
    if [ -n "$server_pid" ]; then
        echo -e "  ${BLUE}ℹ${NC} Server PID: $server_pid"
        
        # Count worker processes
        proc_count=$(pgrep -f "./server" | wc -l)
        echo -e "  ${BLUE}ℹ${NC} Server processes: $proc_count"
    fi
else
    echo -e "  ${RED}✗${NC} Server is not responding"
    echo -e "    Start it: cd .. && ./server"
    echo -e "    ${YELLOW}Tests will fail without a running server!${NC}"
    ISSUES=$((ISSUES + 1))
fi

# Check 8: Port availability (if server not running)
if ! curl -s -o /dev/null "http://localhost:8080" 2>&1; then
    echo -e "\n${YELLOW}[8] Checking port 8080 availability...${NC}"
    # Prefer ss, fall back to netstat if available
    if command -v ss >/dev/null 2>&1; then
        if ss -ltn | grep -q ":8080"; then
            echo -e "  ${YELLOW}⚠${NC} Port 8080 is in use by another process"
            echo -e "    Check: ss -ltn | grep 8080"
        else
            echo -e "  ${GREEN}✓${NC} Port 8080 is available"
        fi
    elif command -v netstat >/dev/null 2>&1; then
        if netstat -tuln 2>/dev/null | grep -q ":8080"; then
            echo -e "  ${YELLOW}⚠${NC} Port 8080 is in use by another process"
            echo -e "    Check: netstat -tuln | grep 8080"
        else
            echo -e "  ${GREEN}✓${NC} Port 8080 is available"
        fi
    else
        echo -e "  ${YELLOW}⚠${NC} Cannot check port (ss/netstat not available)"
    fi
fi

# Check 9: Disk space
echo -e "\n${YELLOW}[9] Checking disk space...${NC}"
available=$(df -h . | tail -1 | awk '{print $4}')
echo -e "  ${BLUE}ℹ${NC} Available space: $available"

# Check 10: Reports directory
echo -e "\n${YELLOW}[10] Checking reports directory...${NC}"
if [ -d "reports" ]; then
    report_count=$(find reports -name "*.txt" -o -name "*.log" 2>/dev/null | wc -l)
    echo -e "  ${GREEN}✓${NC} reports/ directory exists"
    if [ $report_count -gt 0 ]; then
        echo -e "  ${BLUE}ℹ${NC} Found $report_count previous report(s)"
    fi
else
    echo -e "  ${BLUE}ℹ${NC} reports/ directory will be created on first run"
fi

# Summary
echo ""
echo -e "${BLUE}╔══════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║                           SUMMARY                                ║${NC}"
echo -e "${BLUE}╚══════════════════════════════════════════════════════════════════╝${NC}"
echo ""

if [ $ISSUES -eq 0 ]; then
    echo -e "${GREEN}✓ Environment is ready for testing!${NC}"
    echo ""
    echo "You can now run:"
    echo "  ./run_all_tests.sh              # All tests"
    echo "  ./run_all_tests.sh --quick      # Quick validation"
    echo "  ./functional_tests.sh           # Individual suite"
    echo ""
    exit 0
else
    echo -e "${RED}✗ Found $ISSUES issue(s) that need attention${NC}"
    echo ""
    echo "Please fix the issues marked with ${RED}✗${NC} above before running tests."
    echo ""
    exit 1
fi

#!/usr/bin/env bash

set -u

TOTAL_FAILED=0

echo "========================================"
echo "RUNNING ARGUMENT TESTS"
echo "========================================"

./tests/test_args.sh || TOTAL_FAILED=$((TOTAL_FAILED + 1))

echo
echo "========================================"
echo "RUNNING MEMORY TESTS"
echo "========================================"

./tests/test_memory.sh || TOTAL_FAILED=$((TOTAL_FAILED + 1))

echo
echo "========================================"
echo "RUNNING SOURCE OPEN TESTS"
echo "========================================"

./tests/test_open.sh || TOTAL_FAILED=$((TOTAL_FAILED + 1))

echo
echo "========================================"
echo "RUNNING COPY TESTS"
echo "========================================"

./tests/test_copy.sh || TOTAL_FAILED=$((TOTAL_FAILED + 1))

echo
echo "========================================"

if [ "$TOTAL_FAILED" -eq 0 ]; then
    echo "ALL TEST SUITES PASSED"
    exit 0
else
    echo "$TOTAL_FAILED TEST SUITE(S) FAILED"
    exit 1
fi
#!/usr/bin/env bash

PASS=0
FAIL=0

run_test()
{
    description="$1"
    expected_exit="$2"
    shift 2

    echo
    echo "========================================"
    echo "TEST: $description"
    echo "COMMAND: $*"
    echo "EXPECTED EXIT: $expected_exit"
    echo "----------------------------------------"

    "$@"
    actual_exit=$?

    echo "----------------------------------------"
    echo "ACTUAL EXIT: $actual_exit"

    if [ "$actual_exit" -eq "$expected_exit" ]; then
        echo "RESULT: PASS"
        PASS=$((PASS + 1))
    else
        echo "RESULT: FAIL"
        FAIL=$((FAIL + 1))
    fi
}

run_test "No arguments" \
    1 \
    ./my_cp

run_test "Too few arguments" \
    1 \
    ./my_cp source.txt

run_test "Too many arguments" \
    1 \
    ./my_cp a b 4096 extra

run_test "Valid granularity 4096" \
    0 \
    ./my_cp tests/fixtures/source.txt tests/fixtures/output.txt 4096

run_test "Valid minimum granularity 1" \
    0 \
    ./my_cp tests/fixtures/source.txt tests/fixtures/output.txt 1

run_test "Zero granularity" \
    1 \
    ./my_cp a b 0

run_test "Negative granularity" \
    1 \
    ./my_cp a b -1

run_test "Non-numeric granularity" \
    1 \
    ./my_cp a b hello

run_test "Number followed by text" \
    1 \
    ./my_cp a b 4096abc

run_test "Integer overflow" \
    1 \
    ./my_cp a b 999999999999999999999999999999999

echo
echo "========================================"
echo "TEST SUMMARY"
echo "========================================"
echo "PASS: $PASS"
echo "FAIL: $FAIL"
echo "TOTAL: $((PASS + FAIL))"

if [ "$FAIL" -eq 0 ]; then
    echo "ALL TESTS PASSED"
    exit 0
else
    echo "SOME TESTS FAILED"
    exit 1
fi
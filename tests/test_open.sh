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

run_test "Existing source file opens successfully" \
    0 \
    ./my_cp tests/fixtures/source.txt tests/fixtures/output.txt 4096

run_test "Missing source file fails" \
    1 \
    ./my_cp tests/fixtures/file_that_does_not_exist.txt tests/fixtures/output.txt 4096

echo
echo "========================================"
echo "SOURCE OPEN TEST SUMMARY"
echo "========================================"
echo "PASS: $PASS"
echo "FAIL: $FAIL"
echo "TOTAL: $((PASS + FAIL))"

if [ "$FAIL" -eq 0 ]; then
    echo "ALL SOURCE OPEN TESTS PASSED"
    exit 0
else
    echo "SOME SOURCE OPEN TESTS FAILED"
    exit 1
fi
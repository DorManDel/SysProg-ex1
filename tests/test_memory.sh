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

run_test "1-byte allocation" \
    0 \
    ./my_cp tests/fixtures/source.txt tests/fixtures/output.txt  1

run_test "4 KiB allocation" \
    0 \
    ./my_cp tests/fixtures/source.txt tests/fixtures/output.txt 4096

run_test "64 KiB allocation" \
    0 \
    ./my_cp tests/fixtures/source.txt tests/fixtures/output.txt  65536

run_test "1 MiB allocation" \
    0 \
    ./my_cp tests/fixtures/source.txt tests/fixtures/output.txt  1048576


echo
echo "========================================"
echo "TEST: Forced allocation failure"
echo "----------------------------------------"

(
    ulimit -v 131072
    ./my_cp tests/fixtures/source.txt tests/fixtures/output.txt  1073741824
)

actual_exit=$?

if [ "$actual_exit" -ne 0 ]; then
    echo "RESULT: PASS"
    PASS=$((PASS + 1))
else
    echo "RESULT: FAIL"
    FAIL=$((FAIL + 1))
fi


echo
echo "========================================"
echo "MEMORY TEST SUMMARY"
echo "========================================"
echo "PASS: $PASS"
echo "FAIL: $FAIL"
echo "TOTAL: $((PASS + FAIL))"

if [ "$FAIL" -eq 0 ]; then
    echo "ALL MEMORY TESTS PASSED"
    exit 0
else
    echo "SOME MEMORY TESTS FAILED"
    exit 1
fi


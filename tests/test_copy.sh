#!/usr/bin/env bash

set -u

PASS=0
FAIL=0

SOURCE="tests/fixtures/source.txt"
OUTPUT="tests/fixtures/output.txt"

run_copy_test()
{
    description="$1"
    granularity="$2"

    echo
    echo "========================================"
    echo "TEST: $description"
    echo "COMMAND: ./my_cp $SOURCE $OUTPUT $granularity"
    echo "----------------------------------------"

    ./my_cp "$SOURCE" "$OUTPUT" "$granularity"
    copy_exit=$?

    if [ "$copy_exit" -ne 0 ]; then
        echo "RESULT: FAIL"
        echo "Reason: my_cp exited with status $copy_exit"
        FAIL=$((FAIL + 1))
        return
    fi

    # cmp -s performs a byte-for-byte comparison and reports the result
    # only through its exit status: 0 = identical, non-zero = different/error.
    if cmp -s "$SOURCE" "$OUTPUT"; then
        echo "RESULT: PASS"
        PASS=$((PASS + 1))
    else
        echo "RESULT: FAIL"
        echo "Reason: destination differs from source"
        FAIL=$((FAIL + 1))
    fi
}

run_copy_test "Byte-for-byte copy, granularity 1" 1
run_copy_test "Byte-for-byte copy, granularity 4" 4
run_copy_test "Byte-for-byte copy, granularity 4096" 4096
run_copy_test "Byte-for-byte copy, granularity 65536" 65536

echo
echo "========================================"
echo "COPY TEST SUMMARY"
echo "========================================"
echo "PASS: $PASS"
echo "FAIL: $FAIL"
echo "TOTAL: $((PASS + FAIL))"

if [ "$FAIL" -eq 0 ]; then
    echo "ALL COPY TESTS PASSED"
    exit 0
else
    echo "SOME COPY TESTS FAILED"
    exit 1
fi

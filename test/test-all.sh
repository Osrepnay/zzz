#!/bin/sh

if pgrep --quiet zzzclip; then
    echo "another zzzclip instance found, test results may be inaccurate; run tests in a separate compositor if possible." 1>&2
    echo "if the other instance is attached to a different WAYLAND_DISPLAY, you may ignore this warning." 1>&2
fi

if [ -z "$TESTS" ]; then
    tests="persist.sh max-bytes.sh max-entries.sh"
else
    tests="$TESTS"
fi

for test_name in $tests; do
    if test/"$test_name"; then
        echo "$test_name passed"
    else
        echo "$test_name failed"
    fi
    echo
done

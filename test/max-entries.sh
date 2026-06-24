#!/bin/sh
# checks if entries are trimmed

. test/setup.sh

max_entries=5
for i in $(seq 1 $((max_entries + 1)));
do
    echo "test" | wl-copy
done

entries=$(build/zzzclip list | wc -l)
if [ $entries -lt $max_entries ]; then
    echo 'fail: too few entries'
    exit 1
fi
if [ $entries -gt $max_entries ]; then
    echo 'fail: max-entries exceeded'
    exit 1
fi

exit

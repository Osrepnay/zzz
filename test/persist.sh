#!/bin/sh
# makes sure that clipboard content is persisted on exit

. test/setup.sh

# copy the data and exit
copy_str="testtesttest"
wl-copy -f $copy_str &
sleep 0.1
kill $!

# ensure clipboard data still exists
if ! wl-paste 2> /dev/null | grep -Fxq $copy_str; then
    echo 'fail: data was not persisted after program exit'
    exit 1
fi
exit 0

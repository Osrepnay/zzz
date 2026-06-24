#!/bin/sh
# checks if size is indeed limited

. test/setup.sh

max_chars=100

# @ should never show up in metadata
signal_over="@over"
yes $signal_over | head -c $((max_chars + 1)) | wl-copy
sleep 0.1
if ! build/zzzclip list | grep -Fxq $signal_over; then
    echo 'fail: text over max-item-bytes was stored'
    exit 1
fi

# should be stored
signal_under="@under"
yes $signal_under | head -c $max_chars | wl-copy
sleep 0.1
if ! build/zzzclip list | grep -Fxq $signal_under; then
    echo 'fail: text at max-item-bytes was not stored'
    exit 1
fi

exit

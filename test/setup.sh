# some common setup commands for all the tests
export ZZZCLIP_STORE_PATH=test/store
export ZZZCLIP_CONFIG_PATH=test/test-config

# make sure there's no dangling copies
wl-copy -c
wl-copy -p -c
build/zzzclip daemon &
zzzclip_pid=$!
sleep 0.1
trap 'exit_status=$?; kill $zzzclip_pid; rm -rf test/store; exit $exit_status' EXIT

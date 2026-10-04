#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
out="$repo/.cache/djlink-sanitizers"
mkdir -p "$out"
src="$repo/firmware/common/djlink"
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
for test in djlink_test djlink_test2 test_db_bounds djlink_nfs_test test_discovery test_udp test_db_client test_db_tcp; do
    gcc -Wall -Wextra -Werror -std=c11 -g -O1 \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -I"$src/include" -I"$repo/firmware/common/dj_link_core/include" \
        "$repo/tests/djlink/$test.c" "$src"/src/*.c \
        "$repo/firmware/common/dj_link_core/dj_link_discovery.c" \
        "$repo/firmware/common/dj_link_core/dj_link_udp.c" \
        "$repo/firmware/common/dj_link_core/dj_link_db.c" \
        -pthread -lm -o "$out/$test"
    timeout 180 "$out/$test"
done

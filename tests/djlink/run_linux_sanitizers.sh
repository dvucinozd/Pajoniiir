#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
out="$repo/.cache/djlink-sanitizers"
mkdir -p "$out"
src="$repo/firmware/common/djlink"
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
for test in djlink_test djlink_test2 test_db_bounds djlink_nfs_test test_discovery test_udp test_db_client test_db_tcp test_browse test_cache test_pdb test_link_anlz test_sync; do
    gcc -Wall -Wextra -Werror -std=c11 -g -O1 \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -I"$src/include" -I"$repo/firmware/common/dj_link_core/include" \
        -I"$repo/firmware/main-deck-p4/components/media_identity/include" \
        -I"$repo/firmware/main-deck-p4/components/library/include" \
        -I"$repo/firmware/main-deck-p4/components/deck_core/include" \
        -DANLZ_STANDALONE_TEST -DREKORDBOX_PDB_STANDALONE_TEST \
        "$repo/tests/djlink/$test.c" "$src"/src/*.c \
        "$repo/firmware/common/dj_link_core/dj_link_discovery.c" \
        "$repo/firmware/common/dj_link_core/dj_link_udp.c" \
        "$repo/firmware/common/dj_link_core/dj_link_db.c" \
        "$repo/firmware/common/dj_link_core/dj_link_tcp.c" \
        "$repo/firmware/common/dj_link_core/dj_link_browse.c" \
        "$repo/firmware/common/dj_link_core/dj_link_cache.c" \
        "$repo/firmware/common/dj_link_core/dj_link_pdb.c" \
        "$repo/firmware/common/dj_link_core/dj_link_anlz.c" \
        "$repo/firmware/common/dj_link_core/dj_link_sync.c" \
        "$repo/firmware/main-deck-p4/components/media_identity/media_identity.c" \
        "$repo/firmware/main-deck-p4/components/library/rekordbox_pdb.c" \
        "$repo/firmware/main-deck-p4/components/library/rekordbox_anlz.c" \
        -pthread -lm -o "$out/$test"
    timeout 180 "$out/$test"
done
gcc -Wall -Wextra -Werror -std=c11 -g -O1 -DANLZ_STANDALONE_TEST \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$repo/firmware/main-deck-p4/components/library/include" \
    -I"$repo/firmware/main-deck-p4/components/deck_core/include" \
    "$repo/tests/deck_net_sync/test_deck_net_sync.c" \
    "$repo/firmware/main-deck-p4/components/deck_core/deck_net_sync.c" \
    -lm -o "$out/deck_net_sync"
timeout 180 "$out/deck_net_sync"
python3 "$repo/tests/deck_net_sync/test_network_lifecycle.py" --sanitize

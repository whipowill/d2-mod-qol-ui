#!/usr/bin/env bash
# Offline unit tests (native Linux build of the engine pieces). Must print no FAIL.
set -euo pipefail
cd "$(dirname "$0")"; mkdir -p .out
gcc -O1 -Wall -Wextra -Isrc -o .out/sigtest test/sigtest.c
INO=8b6c24108a852a01000084c0744b8a852b010000
.out/sigtest iname-orig $INO
.out/sigtest iname-patched ${INO/8a852a010000/e8aabbccdd90}
.out/sigtest iname-none 8b442404508b44240c68fcb3bf6f6800b4bf6fe8a8f8ffffc208
.out/sigtest iname-none ${INO}${INO}
LOOP=cccccccc83ec20894c240ce8b45dfdff
.out/sigtest loop-orig ${LOOP}
.out/sigtest loop-patched ${LOOP/83ec20894c240c/e8aabbccdd9090}
.out/sigtest loop-none 8b6c24108a852a01000083ec20894c240c
.out/sigtest loop-none ${LOOP}${LOOP}
MAP=83fe06740583fe087505e8b0d3ffffa1b4a7bb6f85c07405e896d5ffff8bcf5f5ee9079ef9ffc3
.out/sigtest map-orig ${MAP}
.out/sigtest map-patched ${MAP/8bcf5f5ee9079ef9ff/e9aabbccdd90909090}
.out/sigtest map-none 83fe06740583fe087505e8b0d3ffffa1b4a7bb6f85c07405e896d5ffff8bcf5f5e
.out/sigtest map-none ${MAP}${MAP}
DRAW=e835daffffe8e0a4ffffe8bbd9ffffe816bdffffe801b4ffff5e5b59c20400
.out/sigtest draw-orig ${DRAW}
.out/sigtest draw-patched ${DRAW/5e5b59c20400/e9aabbccdd90}
.out/sigtest draw-none e835daffffe8e0a4ffffe8bbd9ffffe816bdffff5e5b59c20400
.out/sigtest draw-none ${DRAW}${DRAW}
GOLD=8b0da47fba6f8b15987fba6f6a0153518d4c2440e8aabbccdd5f5d5b5e81c424020000c20800
.out/sigtest gold-ok ${GOLD}
.out/sigtest gold-none ${GOLD:0:40}
.out/sigtest gold-none ${GOLD}${GOLD}
GCP=8d5424348bceffd55f5d5b5e81c424020000c20800
.out/sigtest gcopy-orig ${GCP}
.out/sigtest gcopy-patched ${GCP/8d5424348bceffd5/e8aabbccdd909090}
.out/sigtest gcopy-none ${GCP:0:30}
.out/sigtest gcopy-none ${GCP}${GCP}
CHAT=8a4424718d742470c684247201000000e8aabbccddE9112233ff
CHAT=${CHAT,,}
.out/sigtest chat-ok ${CHAT}
.out/sigtest chat-none ${CHAT:0:50}
.out/sigtest chat-none ${CHAT}${CHAT}
gcc -O1 -Wall -Wextra -Isrc -o .out/filter_test test/filter_test.c src/filter.c && .out/filter_test
gcc -O1 -Wall -Wextra -Isrc -o .out/itemdb_test test/itemdb_test.c src/itemdb.c && .out/itemdb_test

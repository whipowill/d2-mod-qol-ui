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
SH0=5f391dc4c8bc6f7413833d70d0bc6f03
SH1=c20400391dc4c8bc6f0f857204000053538bc5e8
SH2=8b44241c85c05f5d75236a006a006a0033d233c9c705
SH3=8b0dc4c8bc6f33f63bce74af5f5e5b33c05d83c428c21000
for w in 0 1 2 3; do
  v=SH$w; h=${!v}
  .out/sigtest show$w-orig $h
  .out/sigtest show$w-none ${h}${h}
  .out/sigtest show$w-none ${h:0:20}
done
.out/sigtest show0-patched ${SH0/391dc4c8bc6f/e8aabbccdd90}
.out/sigtest show1-patched ${SH1/391dc4c8bc6f/e8aabbccdd90}
.out/sigtest show2-patched ${SH2/8b44241c85c0/e8aabbccdd90}
.out/sigtest show3-patched ${SH3/8b0dc4c8bc6f/e8aabbccdd90}
DESC=8d5424148bcfff15acfab76f5e5d5b81c408080000c20c00
.out/sigtest desc-orig $DESC
.out/sigtest desc-patched ${DESC/81c408080000/e9aabbccdd90}
.out/sigtest desc-none ${DESC:0:30}
.out/sigtest desc-none ${DESC}${DESC}
LBL0=6a008be86a048d43f6508bcde897a2f9ff
LBL1=3844241075046a02eb026a01535756e80d95f9ff
LBL3=663944241c74766a01535755e87295f9ff
.out/sigtest lbl0-ok $LBL0
.out/sigtest lbl0-ok ${LBL0/6a048d/6a0c8d}
.out/sigtest lbl0-none ${LBL0}${LBL0}
.out/sigtest lbl0-none ${LBL0:0:20}
for w in 1 2; do
  .out/sigtest lbl$w-ok $LBL1
  .out/sigtest lbl$w-ok ${LBL1/6a02eb026a01/6a07eb026a05}
  .out/sigtest lbl$w-none ${LBL1}${LBL1}
done
.out/sigtest lbl3-ok $LBL3
.out/sigtest lbl3-ok ${LBL3/6a0153/6a0953}
.out/sigtest lbl3-none ${LBL3}${LBL3}
gcc -O1 -Wall -Wextra -Isrc -o .out/filter_test test/filter_test.c src/filter.c && .out/filter_test
gcc -O1 -Wall -Wextra -Isrc -o .out/itemdb_test test/itemdb_test.c src/itemdb.c && .out/itemdb_test
gcc -O1 -Wall -Wextra -Isrc -o .out/party_test test/party_test.c src/party.c && .out/party_test

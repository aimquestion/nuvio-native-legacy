#!/bin/bash
# Ver tests/mkv_caps.sh: redeurl.c anda junto com rede.c.
set -eu
cd "$(dirname "$0")/.."
cc -Isrc tests/mkv_dv.c src/rede.c src/redeurl.c -o /tmp/nuvio-mkv-dv-tests \
  -I/opt/homebrew/include -Wno-deprecated-declarations 2>/dev/null || \
cc -Isrc tests/mkv_dv.c src/rede.c src/redeurl.c -o /tmp/nuvio-mkv-dv-tests -I/opt/homebrew/include
/tmp/nuvio-mkv-dv-tests

#!/bin/bash
# Off-game tests (native Linux build): survival rules + The Long Dark audio reader against a mock install.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd); T=$(mktemp -d)
python3 "$ROOT/design/preflight.py"
python3 "$ROOT/design/codegen.py" >/dev/null
clang++ -std=c++17 -O1 -Wall -I"$ROOT/src/common" "$ROOT/tests/sim_test.cpp" -o "$T/sim_test" && "$T/sim_test"
clang -c -O1 -w "$ROOT/third_party/stb/stb_vorbis.c" -o "$T/stbv.o"
clang -c -O1 -w "$ROOT/third_party/ww2ogg/crc.c" -o "$T/crc.o"
clang++ -std=c++17 -O1 -w -I"$ROOT/src/common" "$ROOT/tests/audio_test.cpp" "$ROOT/src/launcher/TldAudio.cpp" \
	"$ROOT/third_party/ww2ogg/wwriff.cpp" "$ROOT/third_party/ww2ogg/codebook.cpp" "$T/stbv.o" "$T/crc.o" -o "$T/audio_test"
MOCK_ROOT="$T/mock" "$T/audio_test" | tail -1

#!/bin/bash
# Builds everything into dist/FrostSpore and dist/FrostSpore-<version>.zip.
#   tools/setup_toolchain.sh && tools/prepare_sdk.sh && tools/build.sh
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TC=${TC:-$HOME/frostspore-tc}; SDK=${SDK:-$HOME/frostspore-sdk}
VERSION=$(cat "$ROOT/VERSION")
. "$TC/env.sh"
B=$ROOT/build; OUT=$ROOT/dist/FrostSpore
rm -rf "$B" "$OUT" && mkdir -p "$B"/{detours,core,injector,mod,launcher} "$OUT/licenses"
python3 "$ROOT/design/preflight.py"
python3 "$ROOT/design/codegen.py"
CL="clang --driver-mode=cl $CLFLAGS /O2 /MT /EHsc -Wno-everything /DNOMINMAX"
J=${JOBS:-4}

echo "== Detours (MIT)"
for f in detours modules disasm image creatwth disolx86 disolx64 disolia64 disolarm disolarm64; do
	echo "$f"; done | xargs -P $J -I{} $CL /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x501 /c "$ROOT/third_party/detours/src/{}.cpp" /Fo"$B/detours/{}.obj"
llvm-lib /out:"$B/detours/detours.lib" "$B"/detours/*.obj

echo "== Spore ModAPI core (SporeModAPI.dll, march2017 executable family)"
SDKFLAGS=(/std:c++17 /GR -fms-extensions /DUNICODE /D_UNICODE /DWIN32 /DSDK_BUILD_VER=1000 /DEXECUTABLE_TYPE=2 /DNDEBUG /D_WINDOWS
	"/I$SDK/Detours/include" "/I$SDK/Spore ModAPI" "/I$SDK/EASTL-3.02.01/test/packages/EABase/include/Common"
	"/I$SDK/EASTL-3.02.01/test/packages/EAAssert/include" "/I$SDK/EASTL-3.02.01/test/packages/EAStdC/include" "/I$SDK/EASTL-3.02.01/include")
grep -o 'ClCompile Include="[^"]*"' "$SDK/Spore ModAPI/Spore ModAPI.vcxproj" | sed 's/ClCompile Include="//;s/"$//' | tr '\\' '/' > "$B/core/files.txt"
# compiler flags go through a response file: SDK paths contain spaces
printf '"%s"\n' $CLFLAGS /O2 /MT /EHsc -Wno-everything /DNOMINMAX "${SDKFLAGS[@]}" > "$B/sdk.rsp"
export B
( cd "$SDK/Spore ModAPI" && tr '\n' '\0' < "$B/core/files.txt" | xargs -0 -P $J -I{} sh -c 'o="$B/core/$(echo "$1" | tr "/ " "__").obj"; clang --driver-mode=cl @"$B/sdk.rsp" /D_USRDLL /DMODAPI_DLL_EXPORT /c "$1" /Fo"$o" 2>"$o.err" || { cat "$o.err"; exit 255; }' _ {} )
echo "== Spore ModAPI base library (SporeModAPIBase.lib, linked into the mod)"
mkdir -p "$B/base"
( cd "$SDK/Spore ModAPI" && tr '\n' '\0' < "$B/core/files.txt" | xargs -0 -P $J -I{} sh -c 'o="$B/base/$(echo "$1" | tr "/ " "__").obj"; clang --driver-mode=cl @"$B/sdk.rsp" /D_LIB /c "$1" /Fo"$o" 2>"$o.err" || { cat "$o.err"; exit 255; }' _ {} )
llvm-lib /out:"$B/base/SporeModAPIBase.lib" "$B"/base/*.obj
lld-link /nologo /dll /out:"$OUT/SporeModAPI.dll" /implib:"$B/core/SporeModAPI.lib" "$B"/core/*.obj $LINKLIBPATHS "/libpath:$B/detours" detours.lib kernel32.lib user32.lib gdi32.lib advapi32.lib shell32.lib ole32.lib psapi.lib version.lib d3d9.lib ws2_32.lib /opt:ref

echo "== ModAPI DLL injector (Spore ModAPI Launcher Kit, MIT)"
$CL "/I$ROOT/third_party/detours/src" /c "$ROOT/third_party/modapi-launcher-kit/DLLInjector.cpp" /Fo"$B/injector/DLLInjector.obj"
lld-link /nologo /dll /implib:"$B/injector/inj.lib" /out:"$OUT/ModAPI.DLLInjector.dll" "$B/injector/DLLInjector.obj" $LINKLIBPATHS "/libpath:$B/detours" detours.lib kernel32.lib /export:SetInjectionData=_SetInjectionData@4

echo "== FrostSpore.dll"
for f in FrostSpore Audio Overlay; do
	clang --driver-mode=cl @"$B/sdk.rsp" "/I$ROOT/src/common" /c "$ROOT/src/mod/$f.cpp" /Fo"$B/mod/$f.obj"
done
lld-link /nologo /dll /implib:"$B/mod/FrostSpore.lib" /out:"$OUT/FrostSpore.dll" "$B"/mod/*.obj "$B/base/SporeModAPIBase.lib" "$B/core/SporeModAPI.lib" $LINKLIBPATHS "/libpath:$B/detours" detours.lib kernel32.lib user32.lib winmm.lib advapi32.lib

echo "== FrostSpore.exe launcher"
$CL /std:c++17 "/I$ROOT/src/common" /c "$ROOT/src/launcher/Launcher.cpp" /Fo"$B/launcher/Launcher.obj"
$CL /std:c++14 "/I$ROOT/src/common" /c "$ROOT/src/launcher/TldAudio.cpp" /Fo"$B/launcher/TldAudio.obj"
for f in wwriff codebook; do $CL /std:c++14 /DWIN32_LEAN_AND_MEAN /c "$ROOT/third_party/ww2ogg/$f.cpp" /Fo"$B/launcher/$f.obj"; done
$CL /c "$ROOT/third_party/ww2ogg/crc.c" /Fo"$B/launcher/crc.obj"
$CL /c "$ROOT/third_party/stb/stb_vorbis.c" /Fo"$B/launcher/stb_vorbis.obj"
lld-link /nologo /out:"$OUT/FrostSpore.exe" /subsystem:windows "$B"/launcher/*.obj $LINKLIBPATHS kernel32.lib user32.lib shell32.lib ole32.lib

echo "== package"
cp "$ROOT/third_party/ww2ogg/packed_codebooks_aoTuV_603.bin" "$OUT/"
cp "$ROOT/third_party/ww2ogg/COPYING" "$OUT/licenses/ww2ogg-BSD.txt"
cp "$ROOT/third_party/detours/LICENSE.md" "$OUT/licenses/Detours-MIT.txt"
cp "$ROOT/third_party/modapi-launcher-kit/LICENSE" "$OUT/licenses/ModAPI-Launcher-Kit-MIT.txt"
cp "$SDK/EASTL-3.02.01/LICENSE" "$OUT/licenses/EASTL-BSD.txt"
cp "$ROOT/CREDITS.md" "$OUT/CREDITS.md"
cp "$ROOT/packaging/README.txt" "$OUT/README.txt"
( cd "$ROOT/dist" && rm -f "FrostSpore-$VERSION.zip" && python3 - "$VERSION" <<'PY'
import os, sys, zipfile
v = sys.argv[1]
with zipfile.ZipFile(f'FrostSpore-{v}.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for d, _, fs in os.walk('FrostSpore'):
        for f in sorted(fs):
            p = os.path.join(d, f); z.write(p, os.path.relpath(p, '.'))
PY
)
ls -la "$OUT" "$ROOT/dist"

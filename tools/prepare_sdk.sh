#!/bin/bash
# Fetch the Spore ModAPI SDK at a pinned commit and patch it to build with clang-cl (standard preprocessor,
# standard C++ reference binding). Output: $SDK (default ~/frostspore-sdk).
set -euo pipefail
SDK=${SDK:-$HOME/frostspore-sdk}
COMMIT=2d71289adc71aa5c58f4516c6c835c99f5f9ca19
HERE=$(cd "$(dirname "$0")" && pwd)
. "${TC:-$HOME/frostspore-tc}/env.sh"
if [ ! -d "$SDK/.git" ]; then
	git clone -q https://github.com/Spore-Community/Spore-ModAPI.git "$SDK"
fi
cd "$SDK"
git fetch -q --depth 1 origin "$COMMIT" 2>/dev/null || true
git checkout -q -f "$COMMIT" && git clean -qfdx
# 1. backslashes in #include paths
find . \( -name "*.h" -o -name "*.cpp" -o -name "*.inl" -o -name "*.hpp" \) -print0 | xargs -0 perl -pi -e 'if(/^\s*#\s*include/){ s/\\/\//g }'
# 2. Args(...) relies on MSVC's traditional preprocessor
python3 "$HERE/sdk-patches/deparen.py" "Spore ModAPI/Spore/CppRevEngBase.h" "Spore ModAPI/Spore/Internal.h"
# 3. Windows' IN macro collides with an EASTL template parameter
perl -pi -e 's/\bIN\b/IN_/g' EASTL-3.02.01/include/EASTL/type_traits.h
# 4. out-of-line DETOUR definitions are explicit specializations
grep -rlZ "::DETOUR(" --include=*.cpp . | xargs -0 -r perl -pi -e 's/^(\s*)(?!template)(\S.*?\b\w+::DETOUR\()/$1template<> $2/'
# 5. temporaries bound to non-const reference default arguments
cat >> "Spore ModAPI/Spore/CppRevEngBase.h" <<'H'

#ifndef MODAPI_DEFAULT_TEMP
#define MODAPI_DEFAULT_TEMP
// Standard C++ cannot bind a temporary to a non-const reference default argument (an MSVC extension);
// this hands out a freshly reset per-thread object instead.
template<class T> inline T& ModAPIDefaultTemp() { static thread_local T t; t = T(); return t; }
#endif
H
grep -rlZE "(\w+)& ?\w* ?= ?\1\(\)" --include=*.h --include=*.cpp . | xargs -0 -r perl -pi -e 's/\b((?:[\w:]+::)?(\w+))&(\s*\w*\s*)=\s*\1\(\)/$1&$3= ModAPIDefaultTemp<$1>()/g'
perl -pi -e 's/static constexpr char16_t\* GENERIC_LAYOUT/static constexpr const char16_t* GENERIC_LAYOUT/' "Spore ModAPI/Spore/UI/ScrollFrameVertical.h"
# 6. include-name case on a case-sensitive file system
python3 "$HERE/fixcase.py" "$SDK/Spore ModAPI:$SDK/EASTL-3.02.01/include:$SDK/EASTL-3.02.01/test/packages/EABase/include/Common:$SDK/EASTL-3.02.01/test/packages/EAStdC/include:$SDK/EASTL-3.02.01/test/packages/EAAssert/include:$SDK/Detours/include" "$SDK/Spore ModAPI:$SDK/EASTL-3.02.01"
python3 "$HERE/fixcase.py" "$VC/include:$VC/atlmfc/include:$SDKI/ucrt:$SDKI/um:$SDKI/shared:$SDKI/winrt" "$SDK"
echo "SDK ready at $SDK"

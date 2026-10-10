#!/bin/bash
# Windows x86 cross toolchain on Linux: clang-cl + lld-link with MSVC 14.11 and Windows SDK 10.0.22000
# headers/libs from Microsoft's NuGet packages. Installs into $TC (default ~/frostspore-tc).
set -euo pipefail
TC=${TC:-$HOME/frostspore-tc}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$TC" && cd "$TC"
fetch() {  # id version
	[ -d "$1" ] && return
	curl -sSfL --retry 3 -o "$1.nupkg" "https://api.nuget.org/v3-flatcontainer/$1/$2/$1.$2.nupkg"
	mkdir -p "$1" && (cd "$1" && python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extractall()" "../$1.nupkg")
	rm -f "$1.nupkg"
}
fetch visualcpptools.community.vs2017layout 14.11.25547
fetch microsoft.windows.sdk.cpp 10.0.22000.196
fetch microsoft.windows.sdk.cpp.x86 10.0.22000.196
VC=$TC/visualcpptools.community.vs2017layout/lib/native
rm -rf "$VC"/bin "$VC"/lib/{x64,arm,onecore} "$VC"/redist "$VC"/atlmfc/src "$VC"/ifc
rm -rf "$TC"/microsoft.windows.sdk.cpp/c/{bin,Redist,"Extension SDKs",References,UnionMetadata,Source,DesignTime,Catalogs}
SDKI=$TC/microsoft.windows.sdk.cpp/c/Include/10.0.22000.0
python3 "$HERE/fixcase.py" "$VC/include:$VC/atlmfc/include:$SDKI/ucrt:$SDKI/um:$SDKI/shared:$SDKI/winrt" "$VC/include:$VC/atlmfc/include:$SDKI/ucrt:$SDKI/um:$SDKI/shared:$SDKI/winrt"
# lowercase aliases for libraries (lld-link resolves kernel32.lib etc. case-sensitively)
python3 - "$VC/lib/x86" "$TC/microsoft.windows.sdk.cpp.x86/c/um/x86" "$TC/microsoft.windows.sdk.cpp.x86/c/ucrt/x86" <<'PY'
import os,sys
for d in sys.argv[1:]:
    for n in os.listdir(d):
        l=n.lower()
        if l!=n and not os.path.exists(os.path.join(d,l)): os.symlink(n, os.path.join(d,l))
PY
cat > "$TC/env.sh" <<ENV
VC=$VC
SDKI=$SDKI
SDKL=$TC/microsoft.windows.sdk.cpp.x86/c
export CLFLAGS="--target=i686-pc-windows-msvc -fms-compatibility-version=19.29 -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH /imsvc\$VC/include /imsvc\$VC/atlmfc/include /imsvc\$SDKI/ucrt /imsvc\$SDKI/um /imsvc\$SDKI/shared /imsvc\$SDKI/winrt"
export LINKLIBPATHS="/libpath:\$VC/lib/x86 /libpath:\$SDKL/ucrt/x86 /libpath:\$SDKL/um/x86"
ENV
echo "toolchain ready: source $TC/env.sh"

#!/bin/sh
# Builds a self-contained innoextract 1.9 for the Linux AppImage.
#
#   packaging/linux/build-innoextract.sh build/innoextract
#
# The game's setup wizard unpacks the GOG Inno Setup installer with innoextract,
# so the AppImage carries it as a separate program. It links only against the
# C runtime's libc and libm; Boost, liblzma, zlib and bzip2 are static.
#
# The source is the GitHub tag archive. The release tarball on constexpr.org
# answers 403 to this project's download hosts, so it is not used. The SHA-256
# below was computed from that exact archive; bump both when changing versions.
set -eu

usage()
{
    echo "usage: $0 <out-dir>" >&2
    exit 2
}

if [ "$#" -ne 1 ]; then
    usage
fi

out_dir=$1
if [ -z "$out_dir" ]; then
    usage
fi

innoextract_version=1.9
innoextract_url="https://github.com/dscharrer/innoextract/archive/refs/tags/${innoextract_version}.tar.gz"
innoextract_sha256="576657932e92835343a829c3be1baefc34593d1f84309df93eb1d39a8bb51510"

mkdir -p "$out_dir"
out_dir=$(CDPATH= cd -- "$out_dir" && pwd)

work=$(mktemp -d "${TMPDIR:-/tmp}/jones3d-ex-innoextract.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

archive="$work/innoextract-${innoextract_version}.tar.gz"
if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 -o "$archive" "$innoextract_url"
elif command -v wget >/dev/null 2>&1; then
    wget -O "$archive" "$innoextract_url"
else
    echo "error: curl or wget is needed to download innoextract" >&2
    exit 1
fi
echo "${innoextract_sha256}  ${archive}" | sha256sum -c -

tar xzf "$archive" -C "$work"
src="$work/innoextract-${innoextract_version}"

# Build fixes for current distributions, all confined to the unpacked source:
#
# - Boost 1.89 and newer no longer ship a separate Boost.System library; it is
#   header-only. Dropping the component also works with older Boost, whose
#   system library is only a compatibility shim.
# - Boost.Filesystem 1.90 stopped including directory.hpp from operations.hpp,
#   and slice.cpp uses directory_iterator without including it directly.
# - CMake 4 refuses the 2.8 minimum version in VersionScript.cmake, which is
#   run as a script and does not see CMAKE_POLICY_VERSION_MINIMUM.
# - Boost_NO_BOOST_CMAKE keeps CMake's FindBoost from pulling Boost's CMake
#   config, whose static iostreams/regex targets would add ICU and zstd.
sed -i '/^[[:space:]]*system[[:space:]]*$/d' "$src/CMakeLists.txt"
sed -i 's|#include <boost/filesystem/operations.hpp>|#include <boost/filesystem/directory.hpp>\n#include <boost/filesystem/operations.hpp>|' "$src/src/stream/slice.cpp"
sed -i 's/cmake_minimum_required(VERSION 2.8)/cmake_minimum_required(VERSION 3.5)/' "$src/cmake/VersionScript.cmake"

generator=
if command -v ninja >/dev/null 2>&1; then
    generator="-G Ninja"
fi

cmake -S "$src" -B "$work/build" $generator \
    -DCMAKE_BUILD_TYPE=Release \
    -DUSE_STATIC_LIBS=ON \
    -DBoost_NO_BOOST_CMAKE=ON \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build "$work/build" --parallel

strip --strip-all "$work/build/innoextract" -o "$out_dir/innoextract"

# Everything except the C runtime must be static so the AppImage does not
# depend on libraries that only exist on the build host.
if command -v ldd >/dev/null 2>&1; then
    if ldd "$out_dir/innoextract" | grep -Ev \
        'linux-vdso|libm\.so|libc\.so|ld-linux|not a dynamic executable|statically linked'; then
        echo "error: innoextract linked to libraries other than libc and libm" >&2
        exit 1
    fi
fi

"$out_dir/innoextract" --version >/dev/null

licenses="$out_dir/licenses"
mkdir -p "$licenses"

copy_license()
{
    if [ ! -f "$1" ]; then
        echo "error: license file not found: $1" >&2
        exit 1
    fi
    cp "$1" "$licenses/$2"
}

# innoextract is zlib-licensed; its crypto implementations are public domain.
copy_license "$src/LICENSE" "innoextract-LICENSE.txt"

# Ubuntu/CMake package documentation ships the license texts for the static
# dependencies rather than upstream's LICENSE_1_0.txt files. The Boost
# copyright file embeds the Boost Software License 1.0.
boost_copyright=
for candidate in /usr/share/doc/libboost*-dev/copyright; do
    if [ -f "$candidate" ]; then
        boost_copyright=$candidate
        break
    fi
done
if [ -z "$boost_copyright" ]; then
    echo "error: no Boost copyright file in /usr/share/doc (install a libboost*-dev package)" >&2
    exit 1
fi
copy_license "$boost_copyright" "Boost-copyright.txt"
copy_license /usr/share/doc/liblzma-dev/copyright "liblzma-copyright.txt"
copy_license /usr/share/doc/zlib1g-dev/copyright "zlib-copyright.txt"
copy_license /usr/share/doc/libbz2-dev/copyright "bzip2-copyright.txt"

cat > "$licenses/SOURCES.txt" <<EOF
innoextract-LICENSE.txt  innoextract ${innoextract_version} LICENSE
Boost-copyright.txt      Ubuntu libboost*-dev copyright (contains BSL-1.0)
liblzma-copyright.txt    Ubuntu liblzma-dev copyright
zlib-copyright.txt       Ubuntu zlib1g-dev copyright
bzip2-copyright.txt      Ubuntu libbz2-dev copyright
EOF

printf '%s\n' "$out_dir/innoextract"

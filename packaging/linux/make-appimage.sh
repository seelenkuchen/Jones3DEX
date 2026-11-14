#!/bin/sh
# Builds Jones3D_EX-<version>-x86_64.AppImage from an installed game component:
#
#   cmake --install build --prefix staging --component game
#   packaging/linux/make-appimage.sh staging build/out 0.5.0
#
# appimagetool comes from $APPIMAGETOOL when set, else the pinned release below
# is downloaded into the user's cache and checked against its SHA-256. It runs
# with APPIMAGE_EXTRACT_AND_RUN=1, so no FUSE (and no libfuse2) is needed in a
# container.
set -eu

usage()
{
    echo "usage: $0 <install-prefix> <out-dir> <version>" >&2
    exit 2
}

if [ "$#" -ne 3 ]; then
    usage
fi

prefix=$1
out_dir=$2
version=$3

if [ -z "$version" ]; then
    echo "error: the version must not be empty" >&2
    exit 2
fi
case "$version" in
    *[!0-9A-Za-z._-]*)
        echo "error: the version contains characters not allowed in a file name: $version" >&2
        exit 2
        ;;
esac

if [ ! -x "$prefix/Jones3D" ]; then
    echo "error: no Jones3D executable in $prefix (install the game component first)" >&2
    exit 1
fi
if [ ! -d "$prefix/Data" ]; then
    echo "error: no Data/ directory in $prefix (install the game component first)" >&2
    exit 1
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

mkdir -p "$out_dir"
out_dir=$(CDPATH= cd -- "$out_dir" && pwd)

work=$(mktemp -d "${TMPDIR:-/tmp}/jones3d-ex-appimage.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

appdir="$work/Jones3D_EX.AppDir"
mkdir -p "$appdir/usr/bin" \
    "$appdir/usr/share/applications" \
    "$appdir/usr/share/doc/jones3d-ex" \
    "$appdir/usr/share/licenses/jones3d-ex" \
    "$appdir/usr/share/icons/hicolor/scalable/apps" \
    "$appdir/usr/share/icons/hicolor/256x256/apps"

# The game finds Data/ next to its executable (ADR-0031), so both move to usr/bin.
cp "$prefix/Jones3D" "$appdir/usr/bin/Jones3D"
cp -R "$prefix/Data" "$appdir/usr/bin/Data"

if [ -d "$prefix/licenses" ]; then
    cp -R "$prefix/licenses/." "$appdir/usr/share/licenses/jones3d-ex/"
fi

for document in README.md CHANGELOG.md; do
    if [ -f "$prefix/$document" ]; then
        cp "$prefix/$document" "$appdir/usr/share/doc/jones3d-ex/$document"
    fi
done

cp "$script_dir/jones3d-ex.desktop" "$appdir/jones3d-ex.desktop"
cp "$script_dir/jones3d-ex.desktop" "$appdir/usr/share/applications/jones3d-ex.desktop"
cp "$script_dir/jones3d-ex.svg" "$appdir/jones3d-ex.svg"
cp "$script_dir/jones3d-ex.svg" "$appdir/usr/share/icons/hicolor/scalable/apps/jones3d-ex.svg"
cp "$script_dir/jones3d-ex.png" "$appdir/jones3d-ex.png"
cp "$script_dir/jones3d-ex.png" "$appdir/usr/share/icons/hicolor/256x256/apps/jones3d-ex.png"
cp "$script_dir/jones3d-ex.png" "$appdir/.DirIcon"
cp "$script_dir/AppRun" "$appdir/AppRun"
chmod 755 "$appdir/AppRun"

# appimagetool 1.9.1, a fixed release rather than the moving continuous build.
appimagetool_version=1.9.1
appimagetool_url="https://github.com/AppImage/appimagetool/releases/download/${appimagetool_version}/appimagetool-x86_64.AppImage"
appimagetool_sha256="ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"

appimagetool=${APPIMAGETOOL:-}
if [ -z "$appimagetool" ]; then
    cache_dir="${XDG_CACHE_HOME:-$HOME/.cache}/jones3d-ex"
    mkdir -p "$cache_dir"
    appimagetool="$cache_dir/appimagetool-${appimagetool_version}-x86_64.AppImage"
    if [ ! -x "$appimagetool" ]; then
        partial="$appimagetool.part"
        rm -f "$partial"
        if command -v curl >/dev/null 2>&1; then
            curl -fL --retry 3 -o "$partial" "$appimagetool_url"
        elif command -v wget >/dev/null 2>&1; then
            wget -O "$partial" "$appimagetool_url"
        else
            echo "error: curl or wget is needed to download appimagetool" >&2
            exit 1
        fi
        echo "${appimagetool_sha256}  ${partial}" | sha256sum -c -
        chmod 755 "$partial"
        mv "$partial" "$appimagetool"
    fi
fi

if [ ! -x "$appimagetool" ]; then
    echo "error: no executable appimagetool at $appimagetool" >&2
    exit 1
fi

output="$out_dir/Jones3D_EX-${version}-x86_64.AppImage"
rm -f "$output"
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 "$appimagetool" --no-appstream "$appdir" "$output"
printf '%s\n' "$output"

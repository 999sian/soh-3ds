#!/usr/bin/env bash
# Cross-compile libultraship's non-fetched dependencies for 3DS into
# $DEVKITPRO/portlibs/3ds.
#
# LUS pulls ImGui, prism, monocypher, stb and thread-pool via FetchContent, but
# resolves libzip, nlohmann_json, tinyxml2 and spdlog with find_package — and
# devkitPro packages none of them for 3DS. Neither does it package SDL2 (only
# SDL 1.2), so that is built here too.
#
# Idempotent: skips anything already installed.
#
#   export DEVKITPRO=$HOME/dkp-root/opt/devkitpro
#   ./scripts/build-3ds-deps.sh
set -euo pipefail

: "${DEVKITPRO:?set DEVKITPRO, e.g. export DEVKITPRO=\$HOME/dkp-root/opt/devkitpro}"
TOOLCHAIN="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/cmake/3DS.cmake"
PREFIX="$DEVKITPRO/portlibs/3ds"
WORK="${WORK:-/tmp/soh3ds-deps}"
JOBS="$(nproc)"

mkdir -p "$WORK" "$PREFIX"

have() { [ -e "$PREFIX/lib/$1" ] || [ -e "$PREFIX/include/$1" ]; }

clone() { # url ref dir
    [ -d "$WORK/$3" ] || git clone --depth 1 -b "$2" "$1" "$WORK/$3" 2>&1 | tail -1
}

# cfg <name> <srcdir> <builddir> [extra cmake args...]
cfg() {
    local name="$1" src="$2" bld="$3"; shift 3
    local log="$WORK/$name.log"
    mkdir -p "$bld"
    cmake -S "$src" -B "$bld" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_PREFIX_PATH="$PREFIX" \
        -DBUILD_SHARED_LIBS=OFF \
        "$@" > "$log" 2>&1 \
    && cmake --build "$bld" -j"$JOBS" >> "$log" 2>&1 \
    && cmake --install "$bld" >> "$log" 2>&1 \
    || { echo "FAILED — see $log"; tail -20 "$log"; return 1; }
}

echo "prefix: $PREFIX"

# --- zlib (libzip needs it) --------------------------------------------------
if have libz.a; then echo "zlib          : already installed"; else
    echo "zlib          : building"
    clone https://github.com/madler/zlib.git v1.3.1 zlib
    cfg zlib "$WORK/zlib" "$WORK/zlib/b" -DZLIB_BUILD_EXAMPLES=OFF
    echo "zlib          : ok"
fi

# --- libzip (LUS O2rArchive) -------------------------------------------------
# Everything optional is off: no bzip2/lzma/zstd/openssl on 3DS, and the .o2r
# format only uses deflate.
#
# No HAVE_* overrides are needed. They *were* required until cmake/3DS.cmake was
# fixed to make try_compile link a real executable — before that every
# check_function_exists silently succeeded, so libzip believed newlib implements
# the C11 Annex K family and macOS clonefile, and the build died much later with
# implicit-declaration errors. If you see that class of failure in a new
# dependency, suspect the toolchain's probe linking, not the dependency.
if have libzip.a; then echo "libzip        : already installed"; else
    echo "libzip        : building"
    clone https://github.com/nih-at/libzip.git v1.11.4 libzip
    cfg libzip "$WORK/libzip" "$WORK/libzip/b" \
        -DENABLE_BZIP2=OFF -DENABLE_LZMA=OFF -DENABLE_ZSTD=OFF \
        -DENABLE_OPENSSL=OFF -DENABLE_GNUTLS=OFF -DENABLE_MBEDTLS=OFF \
        -DENABLE_COMMONCRYPTO=OFF -DENABLE_WINDOWS_CRYPTO=OFF \
        -DBUILD_TOOLS=OFF -DBUILD_REGRESS=OFF -DBUILD_EXAMPLES=OFF \
        -DBUILD_DOC=OFF -DLIBZIP_DO_INSTALL=ON
    echo "libzip        : ok"
fi

# --- nlohmann_json (header-only) --------------------------------------------
if have nlohmann; then echo "nlohmann_json : already installed"; else
    echo "nlohmann_json : building"
    clone https://github.com/nlohmann/json.git v3.12.0 json
    cfg json "$WORK/json" "$WORK/json/b" -DJSON_BuildTests=OFF
    echo "nlohmann_json : ok"
fi

# --- tinyxml2 ----------------------------------------------------------------
if have libtinyxml2.a; then echo "tinyxml2      : already installed"; else
    echo "tinyxml2      : building"
    clone https://github.com/leethomason/tinyxml2.git 11.0.0 tinyxml2
    cfg tinyxml2 "$WORK/tinyxml2" "$WORK/tinyxml2/b" -Dtinyxml2_BUILD_TESTING=OFF
    echo "tinyxml2      : ok"
fi

# --- spdlog ------------------------------------------------------------------
if have libspdlog.a; then echo "spdlog        : already installed"; else
    echo "spdlog        : building"
    clone https://github.com/gabime/spdlog.git v1.16.0 spdlog
    cfg spdlog "$WORK/spdlog" "$WORK/spdlog/b" -DSPDLOG_BUILD_EXAMPLE=OFF -DSPDLOG_BUILD_TESTS=OFF
    echo "spdlog        : ok"
fi

# --- SDL2 --------------------------------------------------------------------
# No HAVE_SIGACTION override needed now that cmake/3DS.cmake links try_compile
# properly — the probe correctly reports sigaction as absent (newlib declares it
# but libc.a has no implementation).
if have libSDL2.a; then echo "SDL2          : already installed"; else
    echo "SDL2          : building"
    clone https://github.com/libsdl-org/SDL.git SDL2 SDL
    cfg SDL "$WORK/SDL" "$WORK/SDL/b" -DSDL_SHARED=OFF -DSDL_STATIC=ON
    echo "SDL2          : ok"
fi

echo
echo "installed in $PREFIX/lib:"
ls "$PREFIX/lib" | grep -E '\.a$' || true

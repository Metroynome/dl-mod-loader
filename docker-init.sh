#!/bin/sh
# Prepare an Alpine PS2DEV container. Run with sh; no executable bit required.
set -eu

apk add --no-cache python3 make git build-base ca-certificates
: "${PS2SDK:?Use a PS2DEV image with PS2SDK configured}"
command -v "${EE_PREFIX:-ee-}gcc" >/dev/null

LIBRAC4_DIR=${LIBRAC4_DIR:-/opt/dl-mod-loader-deps/librac4}
LIBDL_DIR=${LIBDL_DIR:-/opt/dl-mod-loader-deps/libdl}
LIBRAC4_REF=${LIBRAC4_REF:-56c3b26c16436986ab6502ee50724216f518e591}
LIBDL_REF=${LIBDL_REF:-3cb909efb6379ea23c0ab6f964ff62f70b63a579}
export LIBRAC4_DIR LIBDL_DIR

clone_library() {
    url=$1
    destination=$2
    reference=$3
    if [ -e "$destination" ]; then
        # Do not reset or discard an existing checkout's work.
        actual=$(git -C "$destination" rev-parse HEAD)
        expected=$(git -C "$destination" rev-parse "$reference^{commit}")
        if [ "$actual" != "$expected" ]; then
            echo "Library revision mismatch in $destination; use a fresh dependency directory." >&2
            exit 1
        fi
        if [ -n "$(git -C "$destination" status --porcelain)" ]; then
            echo "Library checkout is modified: $destination" >&2
            exit 1
        fi
    else
        mkdir -p "$(dirname "$destination")"
        git clone "$url" "$destination"
        git -C "$destination" checkout --detach "$reference"
    fi
}

clone_library https://github.com/Metroynome/librac4.git "$LIBRAC4_DIR" "$LIBRAC4_REF"
clone_library https://github.com/Dnawrkshp/libdl.git "$LIBDL_DIR" "$LIBDL_REF"

# build.py compiles isolated copies of these sources for each supported region.
# Optional arguments let this script initialize and build in a single invocation.
if [ "$#" -gt 0 ]; then
    exec "$@"
fi

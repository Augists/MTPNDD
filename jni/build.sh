#!/bin/bash
# Build libmtpnddjni.so — Go c-shared library providing the JNI native
# methods that org.ants.mtpndd's Java wrapper expects.

set -euo pipefail

cd "$(dirname "$0")"

: "${JAVA_HOME:=/usr/local/java/jdk-23}"
if [[ ! -f "$JAVA_HOME/include/jni.h" ]]; then
    echo "error: JAVA_HOME does not look like a JDK ($JAVA_HOME/include/jni.h missing)" >&2
    exit 1
fi

export CGO_ENABLED=1
export CGO_CFLAGS="-I$JAVA_HOME/include -I$JAVA_HOME/include/linux"

# Build from this directory; the package path must be stable so cgo
# produces predictable symbol names.
go build \
    -buildmode=c-shared \
    -o libmtpnddjni.so \
    .

echo "built $(pwd)/libmtpnddjni.so"

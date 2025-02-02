#!/usr/bin/bash

set -e

DIRNAME="$(dirname "$0")"
IMAGE="$1"
WRAPPER="$2"
WRAPPER_ARGS="$3"
TEST_EXECUTABLE="$4"
TEST_BUILD_DIR="$5"

TEST_RESULT_FILE=$(mktemp -p "$TEST_BUILD_DIR" -t test-result-XXXXXX)
echo 1 > "$TEST_RESULT_FILE"

VIRTME_ENV="\
HOME=$HOME \
LD_LIBRARY_PATH=$LD_LIBRARY_PATH \
XDG_DATA_DIRS=$XDG_DATA_DIRS \
"

virtme-run \
  --memory=256M \
  --rw \
  --pwd \
  --kimg "$IMAGE" \
  --script-sh "sh -c \"env $VIRTME_ENV $DIRNAME/run-kvm-test.sh \\\"$WRAPPER\\\" \\\"$WRAPPER_ARGS\\\" \\\"$TEST_EXECUTABLE\\\" \\\"$TEST_RESULT_FILE\\\"\""

TEST_RESULT="$(cat "$TEST_RESULT_FILE")"
rm "$TEST_RESULT_FILE"

exit "$TEST_RESULT"

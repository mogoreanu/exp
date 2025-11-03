#!/bin/bash
set -e

# This script finds the Bazel binary target associated with the
# currently open file, builds it, and creates a symlink to the
# output for the VSCode debugger to find.

FILE_PATH=$1
if [ -z "$FILE_PATH" ]; then
  echo "Error: No file path provided to build script." >&2
  exit 1
fi

WORKSPACE_ROOT=$(pwd)
# Get the file path relative to the workspace root
FILE_REL_PATH=${FILE_PATH#$WORKSPACE_ROOT/}

echo "Build Task: Finding target for file: '${FILE_REL_PATH}'" >&2

SYMLINK_PATH="${WORKSPACE_ROOT}/.vscode/debug_binary"

if [[ "${FILE_REL_PATH}" = stat/throughput_counter_test.cc || "${FILE_REL_PATH}" = stat/throughput_counter.h || "${FILE_REL_PATH}" = stat/throughput_counter.cc ]]; then
  bazel build -c dbg stat:throughput_counter_test
  ln -sf ${WORKSPACE_ROOT}/bazel-bin/stat/throughput_counter_test ${SYMLINK_PATH}
  exit 0
fi

echo "Error: This build script is only configured to build stat/throughput_counter_test.cc" >&2
exit 1

# # Find the first '..._binary' or '..._test' target that
# # reverse-depends on the current file.
# # We redirect stderr to /dev/null to hide query errors if it finds no target
# TARGETS=$(bazel query "kind('.*_binary|.*_test', rdeps(//..., set(${FILE_REL_PATH})))" --output=label 2>/dev/null)

# if [ -z "$TARGETS" ]; then
#   echo "Error: Could not find a binary/test target for ${FILE_REL_PATH}" >&2
#   echo "Please check that the file is part of a bazel target." >&2
#   exit 1
# fi

# # Take the first target found
# TARGET=$(echo "$TARGETS" | head -n 1)
# echo "Build Task: Found target: $TARGET" >&2

# # Build the target with debug symbols
# if ! bazel build -c dbg $TARGET; then
#   echo "Error: Bazel build failed for $TARGET" >&2
#   exit 1
# fi

# # Get the full path to the output file from cquery
# # We use cquery because it knows the output path for the -c dbg config
# # We redirect stderr to /dev/null to hide cquery noise
# BINARY_PATH=$(bazel cquery "filter('$TARGET', deps($TARGET))" --output=files --config=dbg 2>/dev/null | head -n 1)

# if [ -z "$BINARY_PATH" ]; then
#   echo "Error: Could not get output file path for $TARGET" >&2
#   exit 1
# fi

# # This is the static path the launch.json "program" field points to.
# SYMLINK_PATH="${WORKSPACE_ROOT}/.vscode/debug_binary"

# echo "Build Task: Linking ${BINARY_PATH} to ${SYMLINK_PATH}" >&2
# # Create/update the symlink
# ln -sf ${BINARY_PATH} ${SYMLINK_PATH}

# echo "Build Task: Success. Ready to debug." >&2
# exit 0

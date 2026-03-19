#!/bin/sh -ex

### MAIN ###

mkdir -p "${TFL_PROJECT_DIR}/build"
cd "${TFL_PROJECT_DIR}/build"

cmake \
  -DTOOLCHAIN_PREFIX="${TFL_CMAKE_TOOLCHAIN_PREFIX}" \
  -DCMAKE_BUILD_TYPE="${TFL_CMAKE_BUILD_TYPE}" \
  .. -G Ninja

ninja $TFL_BUILD_TARGETS # Allow word expansion for multiple targets, do not quote this variable

ls -lsa "${TFL_PROJECT_DIR}/build/Projects/${TFL_PROJECT_NAME}"

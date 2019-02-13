#!/bin/bash

# References: llvm.org/docs/GettingStarted.html

set -e

errexit() {
  echo "### Error: $1" && exit 1
}

VERSION=7.0.1

PACKAGE=llvmorg-$VERSION.tar.gz
SRC_URL=https://github.com/llvm/llvm-project/archive/$PACKAGE
SRC_DIR=/tmp/llvm-project-$VERSION
BUILD_DIR=$SRC_DIR/build
INSTALL_DIR=$PWD/llvm

test -d $BUILD_DIR && errexit "$BUILD_DIR already exists!"
test -d $INSTALL_DIR && errexit "$INSTALL_DIR already exists!"

mkdir -p $SRC_DIR $BUILD_DIR $INSTALL_DIR
wget $SRC_URL -O $SRC_DIR/$PACKAGE
tar -zxvf $SRC_DIR/$PACKAGE -C $SRC_DIR --strip-components=1

cd $BUILD_DIR

# clang-tools-extra is enabled with clang
cmake -G "Unix Makefiles" \
  -S $SRC_DIR/llvm \
  -DLLVM_ENABLE_ASSERTIONS=ON \
  -DLLVM_TARGETS_TO_BUILD="X86" \
  -DLLVM_ENABLE_PROJECTS="clang;compiler-rt" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR

cpus=$(getconf _NPROCESSORS_ONLN)
make -j$cpus

make install

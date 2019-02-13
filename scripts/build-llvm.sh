#!/bin/bash

# References: llvm.org/docs/GettingStarted.html

set -e

VERSION=7.0.1

PACKAGE=llvmorg-$VERSION.tar.gz
SRC_URL=https://github.com/llvm/llvm-project/archive/$PACKAGE
SRC_DIR=/tmp/llvm-project-$VERSION
BUILD_DIR=$SRC_DIR/build
INSTALL_DIR=$PWD/llvm

if [ ! -d $SRC_DIR ]; then
  mkdir -p $SRC_DIR
  wget $SRC_URL -O $SRC_DIR/$PACKAGE
  tar -zxvf $SRC_DIR/$PACKAGE -C $SRC_DIR --strip-components=1
fi

if [ ! -d $BUILD_DIR ]; then
  mkdir -p $BUILD_DIR
  # clang-tools-extra is enabled with clang
  cmake -G "Unix Makefiles" \
    -B $BUILD_DIR -S $SRC_DIR/llvm \
    -DLLVM_ENABLE_ASSERTIONS=ON \
    -DLLVM_TARGETS_TO_BUILD="X86" \
    -DLLVM_ENABLE_PROJECTS="clang;compiler-rt" \
    -DLLVM_INSTALL_UTILS=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR
fi

cpus=$(getconf _NPROCESSORS_ONLN)
# cd $BUILD_DIR && make -j$cpus
cmake --build $BUILD_DIR --parallel $cpus

mkdir -p $INSTALL_DIR
# make install
cmake --build $BUILD_DIR --target install

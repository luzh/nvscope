# Analytics and Testing Tools for NVMM Applications

Description.

## Setup
The compiler part depends on LLVM 7.0.1. Refer to `scripts/build-llvm.sh` to
build a customized version. Or acquire its pre-built binary can be acquired as
follows. Place folder `llvm` or create a symbolic link in the project's root
directory.

```
mkdir -p llvm
wget http://releases.llvm.org/7.0.1/clang+llvm-7.0.1-x86_64-linux-gnu-ubuntu-16.04.tar.xz -O llvm.tar.xz
tar -xvf llvm.tar.xz -C llvm --strip-components=1
rm llvm.tar.xz
```

## Build

```
cmake -B build -S .
cmake --build build
```

## Test

```
cd build
make test
...
```

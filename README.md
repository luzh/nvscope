# Analytics and Testing Tools for NVMM Applications

Description.

## Setup
The compiler part requires LLVM 7.0.1. Refer to `scripts/build-llvm.sh` to build
a customized version. Or acquire its pre-built binary as follows. Place folder
`llvm` or create a symbolic link in the project's root directory.

```bash
mkdir -p llvm
wget http://releases.llvm.org/7.0.1/clang+llvm-7.0.1-x86_64-linux-gnu-ubuntu-16.04.tar.xz -O llvm.tar.xz
tar -xvf llvm.tar.xz -C llvm --strip-components=1
rm llvm.tar.xz
```

## Build

```bash
cmake -B build -S .
cmake --build build
```

## Test

```bash
cd build
make test
# Or use ctest
ctest -V
```

All tests should pass.

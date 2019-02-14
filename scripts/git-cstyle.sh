#!/bin/bash

CLANG_FORMAT=$PWD/llvm/bin/clang-format
PAGER="less -FrX"

if [ ! -f $CLANG_FORMAT ]; then
  echo "Error: Invalid CLANG_FORMAT '$CLANG_FORMAT'"
  exit 1
fi

# Check all files in the source tree.
FILES=`git ls-files -- *.{c,cc,cpp,h,hpp}`

# Check modified files, both unstaged and staged.
# FILES=`git diff --name-only HEAD -- *.{c,cc,cpp,h,hpp}`

show_diff() {
  for file in $FILES; do
    $CLANG_FORMAT -style=google $file | git --no-pager diff --color=always --no-index -- $file -
  done
}

show_diff | $PAGER

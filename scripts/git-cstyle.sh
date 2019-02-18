#!/bin/bash

CLANG_FORMAT=$PWD/llvm/bin/clang-format
PAGER="less -FrX"
STYLE=google

if [ ! -f $CLANG_FORMAT ]; then
  echo "Error: Invalid CLANG_FORMAT '$CLANG_FORMAT'" && exit 1
fi

diffone() {
  $CLANG_FORMAT -style=$STYLE $1 | git --no-pager diff --color=always --no-index -- $1 -
}

diffall() {
  for file in $1; do diffone $file; done
}

if [ -z $1 ]; then
  # Check all files in the source tree.
  diffall "`git ls-files -- *.{c,cc,cpp,h,hpp}`" | $PAGER
  # Check modified files, both unstaged and staged.
  # diffall "`git diff --name-only HEAD -- *.{c,cc,cpp,h,hpp}`" | $PAGER
else
  if [ -d $1 ]; then
    files=`git ls-files -- $1/*.{c,cc,cpp,h,hpp}`
  elif [ -f $1 ]; then
    files=$1
  else
    echo "Error: $1 does not exist!" && exit 1
  fi

  if [ -z $2 ]; then # only check the spedified files
    diffall "$files" | $PAGER
  elif [ $2 == "-I" ]; then # modify file in-place
    for file in $files; do
      if ! diffone $file > /dev/null; then
        $CLANG_FORMAT -style=$STYLE -i $file
        echo "Applied clang format to $file."
      fi
    done
  else
    echo "Error: unknown option '$2'" && exit 1
  fi
fi

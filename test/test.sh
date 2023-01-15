#!/bin/bash

test_file() {
    test_num=$1
    file=$2
    bpp=$3
    format=$4
    ./converter "$file.bin" -o "$file.png" -b $bpp -f $format
    ./converter -r "$file.png" -o "$file.2.bin" -b $bpp -f $format
    if [[ $(diff "$file.bin" "$file.2.bin") ]]; then
        echo "test" $test_num "failed"
    else
        echo "test" $test_num "passed"
    fi
    rm "$file.png"
    rm "$file.2.bin"
}

test_file_reverse() {
    test_num=$1
    file=$2
    bpp=$3
    format=$4
    ./converter -r "$file.png" -o "$file.bin" -b $bpp -f $format
    ./converter "$file.bin" -o "$file.2.png" -b $bpp -f $format
    if [[ $(diff "$file.png" "$file.2.png") ]]; then
        echo "test" $test_num "failed"
    else
        echo "test" $test_num "passed"
    fi
    rm "$file.bin"
    rm "$file.2.png"
}

make -C ../example
mv ../example/converter .
test_file 1 "nes_2bpp" 2 planar
test_file 2 "gba_4bpp" 4 gba
rm converter

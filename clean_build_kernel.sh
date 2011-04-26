#!/bin/bash
export LOCALVERSION="-I9000XWJVB-CL118186"
export KBUILD_BUILD_VERSION="Dragon-v6.0-TEST1"

make -j4 clean
make -j4 prepare
make -j4 modules
#find . -name *.ko -exec cp {} usr/initramfs/lib/modules/ \;
make -j4
cp arch/arm/boot/zImage .
ls -lh zImage


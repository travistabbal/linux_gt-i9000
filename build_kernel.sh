#!/bin/bash
export LOCALVERSION="-I9000XWJVB-CL118186"
export KBUILD_BUILD_VERSION="Dragon-v6.0-TEST1"

make arch=arm -j4 prepare
make arch=arm -j4 modules
#find . -name *.ko -exec cp {} ../initramfs-voodoo/lib/modules/ \;
make arch=arm -j4
cp arch/arm/boot/zImage .
ls -lh zImage


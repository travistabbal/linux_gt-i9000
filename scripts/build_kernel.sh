#!/bin/sh

#export KBUILD_BUILD_VERSION="Voodoo_stable_5.1"

cd Kernel 2>/dev/null
nice -n 20 make -j8
cd ..
cp Kernel/arch/arm/boot/zImage .

tar cvf voodoo.tar zImage
ls -lh zImage

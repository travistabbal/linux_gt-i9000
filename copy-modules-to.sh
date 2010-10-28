# copy the kernel module into a ramdisk
if test "$1" != "" && test -d "$1/lib/modules"; then
	find -name *.ko -exec cp -v {} $1/lib/modules/ \;
else
	echo usage: ./copy-modules-to.sh ../my-ramdisk/
fi

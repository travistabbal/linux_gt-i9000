#/bin/bash

echo "$1 $2 $3"

while true
do
  case $# in 0) break ;; esac
    case "$1" in
	    Clean)
		    echo "********************************************************************************"
		    echo "* Clean Kernel                                                                 *"
		    echo "********************************************************************************"

		    pushd linux-2.6.29
		    make clean
		    popd
		    pushd modules
		    make clean
		    popd
		    echo " It's done... "
		    exit
		    ;;
	    nokernel)
		    nokernel=true
		    shift
		    ;;
	    defconfig)
		    defconfig=true
		    PROJECT_NAME=aries
		    HW_BOARD_REV="03"
		    shift
		    ;;
	    updatezip|updatezip/)
		    build_updatezip=true
		    shift
		    ;;
	    tar)
		    build_tar=true
		    shift
		    ;;
	    modules)
		    build_modules=true
		    shift
		    ;;
	    help)
		    PRINT_USAGE
		    shift
		    ;;
	    
	    -|--|*)
		    echo "input error"
		    PRINT_USAGE
		    exit
		    ;;
    esac
done


if [ "$CPU_JOB_NUM" = "" ] ; then
	CPU_JOB_NUM=8
fi

TOOLCHAIN=`pwd`/../../toolchain/arm-voodoo-eabi/bin
TOOLCHAIN_PREFIX=arm-voodoo-eabi-

KERNEL_BUILD_DIR=Kernel

export PRJROOT=$PWD
export PROJECT_NAME
export HW_BOARD_REV

export LD_LIBRARY_PATH=.:${TOOLCHAIN}/../lib

echo "************************************************************"
echo "* EXPORT VARIABLE		                            	 *"
echo "************************************************************"
echo "PRJROOT=$PRJROOT"
echo "PROJECT_NAME=$PROJECT_NAME"
echo "HW_BOARD_REV=$HW_BOARD_REV"
echo "************************************************************"

BUILD_MODULE()
{
	echo "************************************************************"
	echo "* BUILD_MODULE	                                       	 *"
	echo "************************************************************"
	echo

	pushd modules

		make ARCH=arm CROSS_COMPILE=$TOOLCHAIN/$TOOLCHAIN_PREFIX

	popd
}

BUILD_KERNEL()
{
	echo "************************************************************"
	echo "*        BUILD_KERNEL                                      *"
	echo "************************************************************"
	echo

	if  [ "$build_modules" = "true" ]; then
	    BUILD_MODULE
	fi

	pushd $KERNEL_BUILD_DIR

	export KDIR=`pwd`

	if  [ "$defconfig" = "true" ]; then
	    make ARCH=arm $PROJECT_NAME"_rev"$HW_BOARD_REV"_defconfig"
	fi

	if  [ ! "$nokernel" = "true" ]; then
	    # make kernel with codesourcery:
	    # make -j$CPU_JOB_NUM HOSTCFLAGS="-g -O2" CROSS_COMPILE=$TOOLCHAIN/$TOOLCHAIN_PREFIX
	    # make kernel with gnu gcc:
	    make -j$CPU_JOB_NUM HOSTCFLAGS="-g -O2" ARCH=arm CROSS_COMPILE=$TOOLCHAIN/$TOOLCHAIN_PREFIX
	fi
	
	if [ "$build_tar" = "true" ]; then
	    tar -cC arch/arm/boot/ -f ../zImage.tar zImage
	fi
	
	popd
	
	if [ "$build_updatezip" = "true" ]; then
	    pushd updatezip	  
	    cp ../$KERNEL_BUILD_DIR/arch/arm/boot/zImage ./
	    echo "make update.zip"
	    ./build-update-zip.sh
	    popd
	fi
}

# print title
PRINT_USAGE()
{
	echo "************************************************************"
	echo "* USAGE                                                    *"
	echo "************************************************************"
	echo "* Options:                                                 *"
	echo "* nokernel   disable kernel build                          *"
	echo "* modules    build modules                                 *"
	echo "* defconfig  generate default config                       *"
	echo "* updatezip  generate update.zip                           *"
	echo "* tar        generate zImage.tar for heimdall/odin         *"
	echo "* help       shows this                                    *"
	echo "************************************************************"
}

PRINT_TITLE()
{
	echo
	echo "************************************************************"
	echo "*                     MAKE PACKAGES"
	echo "************************************************************"
	i=1
if  [ "$defconfig" = "true" ]; then
	    echo "* $i. generate : standardconfig"
	    i=$(($i+1))
fi
if  [ ! "$nokernel" = "true" ]; then
	    echo "* $i. kernel : zImage"
	    i=$(($i+1))
fi
if  [ "$build_modules" = "true" ]; then
	echo "* $i. build : modules"
	i=$(($i+1))
fi
if [ "$build_LEDNotification" = "true" ]; then
	echo "* $i. build : update.zip"
	i=$(($i+1))
fi

if [ "$build_tar" = "true" ]; then
	echo "* $i. build : tar"
	i=$(($i+1))
fi
	echo "************************************************************"
}

##############################################################
#                   MAIN FUNCTION                            #
##############################################################

START_TIME=`date +%s`

PRINT_TITLE

BUILD_KERNEL $1
END_TIME=`date +%s`
let "ELAPSED_TIME=$END_TIME-$START_TIME"
echo "Total compile time is $ELAPSED_TIME seconds"

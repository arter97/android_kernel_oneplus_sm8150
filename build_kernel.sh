#!/bin/bash
export KERNELDIR=`readlink -f .`
export RAMFS_SOURCE=`readlink -f $KERNELDIR/ramdisk`
export PARTITION_SIZE=100663296

export OS="10.0.0"
export SPL="2020-09"

echo "kerneldir = $KERNELDIR"
echo "ramfs_source = $RAMFS_SOURCE"

RAMFS_TMP="/tmp/arter97-op7-ramdisk"

echo "ramfs_tmp = $RAMFS_TMP"
cd $KERNELDIR

stock=0
if [[ "${1}" == "stock" ]] ; then
	stock=1
	shift
fi

if [[ "${1}" == "skip" ]] ; then
	echo "Skipping Compilation"
else
	if [[ "$stock" == "1" ]] ; then
		sed -i -e 's@"want_initramfs"@"skip_initramfs"@g' init/initramfs.c
	fi
	echo "Compiling kernel"
	cp defconfig .config
	make "$@" || exit 1
	if [[ "$stock" == "1" ]] ; then
		sed -i -e 's@"skip_initramfs"@"want_initramfs"@g' init/initramfs.c
	fi
fi

echo "Building new ramdisk"
#remove previous ramfs files
rm -rf '$RAMFS_TMP'*
rm -rf $RAMFS_TMP
rm -rf $RAMFS_TMP.cpio
#copy ramfs files to tmp directory
cp -axpP $RAMFS_SOURCE $RAMFS_TMP
cd $RAMFS_TMP

#clear git repositories in ramfs
find . -name .git -exec rm -rf {} \;
find . -name EMPTY_DIRECTORY -exec rm -rf {} \;

if [[ "$stock" == "1" ]] ; then
	# Don't use Magisk
	mv .backup/init init
	rm -rf .backup
fi

$KERNELDIR/ramdisk_fix_permissions.sh 2>/dev/null

cd $KERNELDIR
rm -rf $RAMFS_TMP/tmp/*

cd $RAMFS_TMP
find . | fakeroot cpio -H newc -o | lz4 -l -9 > $RAMFS_TMP.cpio.lz4
ls -lh $RAMFS_TMP.cpio.lz4
cd $KERNELDIR

echo "Making new boot image"
find arch/arm64/boot/dts -name '*.dtb' -exec cat {} + > $RAMFS_TMP.dtb
mkbootimg \
    --kernel $KERNELDIR/arch/arm64/boot/Image.gz \
    --ramdisk $RAMFS_TMP.cpio.lz4 \
    --cmdline 'androidboot.hardware=qcom androidboot.console=ttyMSM0 androidboot.memcg=1 lpm_levels.sleep_disabled=1 video=vfb:640x400,bpp=32,memsize=3072000 msm_rtb.filter=0x237 service_locator.enable=1 swiotlb=2048 firmware_class.path=/vendor/firmware_mnt/image loop.max_part=7 androidboot.usbcontroller=a600000.dwc3 buildvariant=user printk.devkmsg=on' \
    --base           0x00000000 \
    --pagesize       4096 \
    --kernel_offset  0x00008000 \
    --ramdisk_offset 0x01000000 \
    --second_offset  0x00f00000 \
    --tags_offset    0x00000100 \
    --dtb            $RAMFS_TMP.dtb \
    --dtb_offset     0x01f00000 \
    --os_version     $OS \
    --os_patch_level $SPL \
    --header_version 2 \
    -o $KERNELDIR/boot.img

GENERATED_SIZE=$(stat -c %s boot.img)
if [[ $GENERATED_SIZE -gt $PARTITION_SIZE ]]; then
	echo "boot.img size larger than partition size!" 1>&2
	exit 1
fi

echo "done"
ls -al boot.img
echo ""

#!/bin/bash

if [[ "${1}" != "skip" ]] ; then
	./build_clean.sh
	./build_kernel.sh stock "$@" || exit 1
fi

VERSION="$(cat version)-$(date +%F | sed s@-@@g)"

if [ -e boot.img ] ; then
	rm arter97-kernel-$VERSION.zip 2>/dev/null
	cp boot.img arter97-kernel-$VERSION.img

	# Pack AnyKernel2
	rm -rf kernelzip
	mkdir -p kernelzip/dtbs
	cp arch/arm64/boot/Image.gz kernelzip/
	find arch/arm64/boot -name '*.dtb' -exec cp {} kernelzip/dtbs/ \;
	echo "
kernel.string=arter97 kernel $(cat version) @ xda-developers
do.devicecheck=1
do.modules=0
do.cleanup=1
do.cleanuponabort=0
device.name1=OnePlus7
device.name2=OnePlus7Pro
device.name3=OnePlus7ProNR
device.name4=guacamoleb
device.name5=guacamole
device.name6=guacamoleg
block=/dev/block/bootdevice/by-name/boot
is_slot_device=1
ramdisk_compression=lz4-l
" > kernelzip/props
	cp -rp ~/android/anykernel/* kernelzip/
	cd kernelzip/
	7z a -mx0 arter97-kernel-$VERSION-tmp.zip *
	zipalign -v 4 arter97-kernel-$VERSION-tmp.zip ../arter97-kernel-$VERSION.zip
	rm arter97-kernel-$VERSION-tmp.zip
	cd ..
	ls -al arter97-kernel-$VERSION.zip
fi

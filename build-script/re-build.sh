#!/bin/bash

CPU=4
KERNEL_VERSION="6.1.61"

case $KERNEL_VERSION in
    "6.1.61")
      KERNEL_COMMIT="d1ba55dafdbd33cfb938bca7ec325aafc1190596"
      PATCH="motivo-6.1.x.patch"
      ;;
    "6.1.58")
      KERNEL_COMMIT="7b859959a6642aff44acdfd957d6d66f6756021e"
      PATCH="motivo-6.1.x.patch"
      ;;
    "5.15.92")
      KERNEL_COMMIT="f5c4fc199c8d8423cb427e509563737d1ac21f3c"
      PATCH="motivo-5.15.x.patch"
      ;;
    "5.10.95")
      KERNEL_COMMIT="770ca2c26e9cf341db93786d3f03c89964b1f76f"
      PATCH="motivo-5.10.x.patch"
      ;;
    "5.10.92")
      KERNEL_COMMIT="ea9e10e531a301b3df568dccb3c931d52a469106"
      PATCH="motivo-5.10.x.patch"
      ;;
    "5.10.90")
      KERNEL_COMMIT="9a09c1dcd4fae55422085ab6a87cc650e68c4181"
      PATCH="motivo-5.10.x.patch"
      ;;
esac

echo "!!!  Build modules for kernel ${KERNEL_VERSION}  !!!"

echo "!!!  Build CM4 32-bit kernel and modules  !!!"
cd linux-${KERNEL_VERSION}-v7l+/
KERNEL=kernel7l
make -j${CPU} ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- bcm2711_defconfig
make -j${CPU} ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- zImage modules dtbs
cd ..
echo "!!!  CM4 32-bit build done  !!!"
echo "-------------------------"

echo "!!!  Build CM4 64-bit kernel and modules  !!!"
cd linux-${KERNEL_VERSION}-v8+/
KERNEL=kernel8
make -j${CPU} ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- bcm2711_defconfig
make -j${CPU} ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- Image modules dtbs
cd ..
echo "!!!  CM4 64-bit build done  !!!"
echo "-------------------------"

echo "!!!  Creating archive  !!!"
rm -rf modules-rpi-${KERNEL_VERSION}-motivo/
mkdir -p modules-rpi-${KERNEL_VERSION}-motivo/boot/overlays
mkdir -p modules-rpi-${KERNEL_VERSION}-motivo/lib/modules/${KERNEL_VERSION}-v7l+/kernel/drivers/gpu/drm/panel/
mkdir -p modules-rpi-${KERNEL_VERSION}-motivo/lib/modules/${KERNEL_VERSION}-v7l+/kernel/sound/usb/
mkdir -p modules-rpi-${KERNEL_VERSION}-motivo/lib/modules/${KERNEL_VERSION}-v8+/kernel/drivers/gpu/drm/panel/
mkdir -p modules-rpi-${KERNEL_VERSION}-motivo/lib/modules/${KERNEL_VERSION}-v8+/kernel/sound/usb/
cp linux-${KERNEL_VERSION}-v7l+/arch/arm/boot/dts/overlays/motivo*.dtbo modules-rpi-${KERNEL_VERSION}-motivo/boot/overlays
cp linux-${KERNEL_VERSION}-v7l+/drivers/gpu/drm/panel/panel-ilitek-ili9881c.ko modules-rpi-${KERNEL_VERSION}-motivo/lib/modules/${KERNEL_VERSION}-v7l+/kernel/drivers/gpu/drm/panel/
cp linux-${KERNEL_VERSION}-v7l+/sound/usb/snd-usb-audio.ko modules-rpi-${KERNEL_VERSION}-motivo/lib/modules/${KERNEL_VERSION}-v7l+/kernel/sound/usb/
cp linux-${KERNEL_VERSION}-v8+/drivers/gpu/drm/panel/panel-ilitek-ili9881c.ko modules-rpi-${KERNEL_VERSION}-motivo/lib/modules/${KERNEL_VERSION}-v8+/kernel/drivers/gpu/drm/panel/
cp linux-${KERNEL_VERSION}-v8+/sound/usb/snd-usb-audio.ko modules-rpi-${KERNEL_VERSION}-motivo/lib/modules/${KERNEL_VERSION}-v8+/kernel/sound/usb/
tar -czvf modules-rpi-${KERNEL_VERSION}-motivo.tar.gz modules-rpi-${KERNEL_VERSION}-motivo/ --owner=0 --group=0
md5sum modules-rpi-${KERNEL_VERSION}-motivo.tar.gz > modules-rpi-${KERNEL_VERSION}-motivo.md5sum.txt
sha1sum modules-rpi-${KERNEL_VERSION}-motivo.tar.gz > modules-rpi-${KERNEL_VERSION}-motivo.sha1sum.txt
rm -rf modules-rpi-${KERNEL_VERSION}-motivo/
mkdir -p ../output
mv modules-rpi-${KERNEL_VERSION}-motivo* ../output/

echo "!!!  Done  !!!"

SUMMARY = "Castle gimbal raspberry Pi image"
LICENSE = "MIT"

inherit core-image

IMAGE_FEATYRES += "ssh-server-openssh"

IMAGE_INSTALL += " \
	packagegroup-core-boot \
	kernel-modules \
	linux-firmware-rpidistro-bcm43430 \
	v4l-utils \
	i2c-tools \
	iw \
	wpa-supplicant \
"

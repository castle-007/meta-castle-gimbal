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
	castle-wifi-config \
	castle-gimbal-controller \
	packagegroup-core-ssh-openssh \
	libgpiod-tools \
	castle-rtsp-server \
	gstreamer1.0 \
	gstreamer1.0-rtsp-server \
	gstreamer1.0-plugins-good-video4linux2 \
	gstreamer1.0-plugins-good-rtp \
	gstreamer1.0-plugins-bad-videoparsersbad \
"

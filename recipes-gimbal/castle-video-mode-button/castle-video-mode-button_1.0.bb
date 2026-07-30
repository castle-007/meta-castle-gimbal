SUMMARY = "GPIO button service for selecting record or RTSP mode"
DESCRIPTION = "Reads a GPIO button and switches between OFF, RECORD, and RTSP services"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS = "libgpiod"

SRC_URI = " \
	file://Makefile \
	file://castle-video-mode-button.c \
	file://castle-video-mode-button.service \
"

S = "${WORKDIR}"

inherit systemd

SYSTEMD_SERVICE:${PN} = "castle-video-mode-button.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"
FILES:${PN} += "${systemd_system_unitdir}/castle-video-mode-button.service"

do_compile() {
	oe_runmake
}

do_install() {
	install -d ${D}${bindir}
	install -m 0755 castle-video-mode-button ${D}${bindir}/castle-video-mode-button

	install -d ${D}${systemd_system_unitdir}
	install -m 0644 ${WORKDIR}/castle-video-mode-button.service \
		${D}${systemd_system_unitdir}/castle-video-mode-button.service
}

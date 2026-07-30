SUMMARY = "GPIO power button service for safe shutdown"
DESCRIPTION = "Reads a GPIO button and powers off the Raspberry Pi when held for 2 seconds"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS = "libgpiod"

SRC_URI = " \
	file://Makefile \
	file://castle-power-button.c \
	file://castle-power-button.service \
"

S = "${WORKDIR}"

inherit systemd

SYSTEMD_SERVICE:${PN} = "castle-power-button.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"
FILES:${PN} += "${systemd_system_unitdir}/castle-power-button.service"

do_compile() {
	oe_runmake
}

do_install() {
	install -d ${D}${bindir}
	install -m 0755 castle-power-button ${D}${bindir}/castle-power-button

	install -d ${D}${systemd_system_unitdir}
	install -m 0644 ${WORKDIR}/castle-power-button.service \
		${D}${systemd_system_unitdir}/castle-power-button.service
}

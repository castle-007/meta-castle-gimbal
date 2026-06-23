SUMMARY = "Castle Wi-Fi connection helper"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit systemd

SRC_URI = " \
    file://castle-wifi-connect \
    file://castle-wifi-connect.service \
    file://wpa_supplicant.conf.example \
"

S = "${WORKDIR}"

RDEPENDS:${PN} += " \
    busybox \
    iproute2 \
    wpa-supplicant \
"

SYSTEMD_SERVICE:${PN} = "castle-wifi-connect.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${WORKDIR}/castle-wifi-connect ${D}${bindir}/castle-wifi-connect

    install -d ${D}${sysconfdir}
    install -m 0644 ${WORKDIR}/wpa_supplicant.conf.example ${D}${sysconfdir}/wpa_supplicant.conf.example

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/castle-wifi-connect.service ${D}${systemd_system_unitdir}/castle-wifi-connect.service
}

FILES:${PN} += " \
    ${bindir}/castle-wifi-connect \
    ${sysconfdir}/wpa_supplicant.conf.example \
    ${systemd_system_unitdir}/castle-wifi-connect.service \
"

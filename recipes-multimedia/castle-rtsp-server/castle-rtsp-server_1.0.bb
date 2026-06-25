SUMMARY = "Castle gimbal RTSP video server"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit pkgconfig systemd

SYSTEMD_SERVICE:${PN} = "castle-rtsp-server.service"
SYSTEMD_AUTO_ENABLE:${PN} = "disable"

DEPENDS = " \
    glib-2.0 \
    gstreamer1.0 \
    gstreamer1.0-rtsp-server \
"

SRC_URI = " \
    file://Makefile \
    file://castle-rtsp-server.c \
	file://castle-rtsp-server.service \
"

S = "${WORKDIR}"

do_compile() {
    oe_runmake
}

do_install() {
	# RTSP 서버 실행 파일을 /usr/bin에 설치한다.
    oe_runmake DESTDIR=${D} install

	# systemd 서비스 파일을 설치할 디렉터리를 만든다.
	install -d ${D}${systemd_system_unitdir}

	# RTSP 서버 서비스 파일을 설치한다.
	install -m 0644 ${WORKDIR}/castle-rtsp-server.service \
		${D}${systemd_system_unitdir}/castle-rtsp-server.service
}

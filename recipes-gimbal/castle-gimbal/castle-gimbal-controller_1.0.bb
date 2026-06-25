SUMMARY = "Castle gimbal controller"
LICENSE = "MIT"

# DEPENDS = "libgpiod": 프로그램을 컴파일할 때 필요
DEPENDS = "libgpiod"

# RDEPENDS:${PN}: 보드에서 프로그램을 실행할 때 필요
RDEPENDS:${PN} += "libgpiod"

inherit systemd

SYSTEMD_SERVICE:${PN} = "castle-gimbal.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://Makefile \
    file://src/main.c \
    file://src/camera.c \
    file://src/camera.h \
	file://castle-gimbal.service \
"

S = "${WORKDIR}"

do_compile() {
    oe_runmake
}

do_install() {
	# C 프로그램을 /usr/bin 에 설치.
    oe_runmake DESTDIR=${D} install

	# systemd 서비스 파일을 설치할 디렉터리를 만든다.
	install -d ${D}${systemd_system_unitdir}

	# 서비스 파일을 읽기 가능한 권한으로 설치한다.
	install -m 0644 ${WORKDIR}/castle-gimbal.service \
		${D}${systemd_system_unitdir}/castle-gimbal.service
}

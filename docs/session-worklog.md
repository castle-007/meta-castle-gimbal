# Castle Gimbal Session Worklog

이 문서는 이번 개발 세션에서 진행한 작업을 처음부터 끝까지 따라갈 수 있도록 정리한 기록이다.

기존 문서와의 차이:

```text
development-summary.md : 기능 개발 결과 중심 요약
debugging-summary.md   : 문제/원인/해결 중심 요약
software-design.md     : 소프트웨어 설계 중심 문서
session-worklog.md     : 실제 진행 순서와 파일/폴더/명령 중심 작업 기록
```

## 1. 기본 진행 방식

사용자는 직접 수정하고, 나는 한 단계씩 설명하는 방식으로 진행했다.

원칙:

```text
1. 한 번에 한 단계씩 진행
2. 사용자가 ok를 보내면 다음 단계 진행
3. 파일을 왜 만드는지, 각 줄이 왜 필요한지 설명
4. 가능하면 custom recipe/meta layer 안에서 관리
5. GitHub에 올려 다른 환경에서도 다시 사용할 수 있게 구성
```

## 2. Yocto 기본 구조

사용한 Yocto 버전:

```text
scarthgap
```

받은 layer:

```text
poky
meta-raspberrypi
meta-openembedded
meta-castle-gimbal
```

주요 경로:

```text
/home/castle/yocto-rpi
/home/castle/yocto-rpi/build-castle-test
/home/castle/yocto-rpi/meta-castle-gimbal
```

custom layer:

```text
meta-castle-gimbal
```

layer collection 이름:

```text
castlegimbal
```

설명:

`BBFILE_COLLECTIONS`에서 하이픈이 들어간 이름은 override 변수명과 충돌하거나 다루기 불편할 수 있어 `castlegimbal`처럼 단순한 이름을 사용했다.

## 3. Build Folder 자동 설정

빌드 폴더를 만들 때 자동 설정되도록 `conf` 파일과 layer 구성을 정리했다.

주요 목적:

```text
MACHINE 설정
Raspberry Pi boot config 설정
image recipe 선택
필요 package 포함
WiFi/camera/PWM/I2C 활성화
```

빌드 진입 명령:

```sh
cd ~/yocto-rpi
source poky/oe-init-build-env build-castle-test
```

## 4. Image Recipe

custom image recipe:

```text
recipes-core/images/castle-gimbal-image.bb
```

주요 역할:

```text
기본 rootfs 구성
castle-gimbal-controller 포함
libgpiod tools 포함
camera/v4l2 관련 도구 포함
WiFi 설정 포함
RTSP server 관련 package 포함
```

## 5. Raspberry Pi Boot 설정

활성화한 기능:

```text
I2C
SPI
PWM 2 channel
Camera
UART
audio off
gpu_mem 128
```

대표 설정:

```text
dtparam=i2c_arm=on
dtparam=i2c1=on
dtparam=spi=on
dtparam=audio=off
dtoverlay=pwm-2chan
start_x=1
gpu_mem=128
```

PWM overlay 문제로 `pwm-2chan.dtbo`가 boot partition에 들어가도록 `IMAGE_BOOT_FILES`를 확인하고 수정했다.

결과:

```text
/boot/overlays/pwm-2chan.dtbo
/sys/class/pwm/pwmchip0
npwm = 2
```

## 6. SD Card 작업

사용한 SD card device:

```text
/dev/sdg
```

초기 상태:

```text
sdg1 : boot
sdg2 : root, 약 200MB
나머지 공간 미사용
```

root partition 확장:

```text
parted로 partition 2를 100%까지 확장
e2fsck 실행
resize2fs 실행
```

Host `e2fsck`가 구버전이라 Yocto native tool을 사용했다.

사용한 native tool:

```text
e2fsprogs-native 1.47.0
```

결과:

```text
/dev/sdg2 약 29G
```

## 7. WiFi / SSH 설정

WiFi 설정 recipe:

```text
recipes-connectivity/castle-wifi-config/castle-wifi-config_1.0.bb
recipes-connectivity/castle-wifi-config/files/castle-wifi-connect
recipes-connectivity/castle-wifi-config/files/castle-wifi-connect.service
recipes-connectivity/castle-wifi-config/files/wpa_supplicant.conf.example
```

오류:

```text
wpa_supplicant.conf.example file could not be found
```

원인:

```text
파일명이 wap_supplicant.conf.example로 잘못 되어 있었음
```

해결:

```text
wpa_supplicant.conf.example로 이름 수정
```

SSH:

```text
sshd.socket enabled
/usr/sbin/sshd 존재
```

네트워크 문제:

```text
wlan0과 eth0이 같은 subnet에 있어 routing 혼선 발생
```

해결:

```text
유선 SSH 테스트 시 wlan0 down
eth0 IP 사용
```

보드 IP 예:

```text
192.168.120.12
```

## 8. GPIO / libgpiod Tools

보드에 `gpioinfo`, `gpioget`가 없었다.

빌드 결과 위치:

```text
tmp/work/cortexa7t2hf-neon-vfpv4-poky-linux-gnueabi/libgpiod/2.1.3/packages-split/libgpiod-tools/usr/bin/gpioinfo
tmp/work/cortexa7t2hf-neon-vfpv4-poky-linux-gnueabi/libgpiod/2.1.3/packages-split/libgpiod-tools/usr/bin/gpioget
```

보드에 복사:

```sh
scp .../gpioinfo root@192.168.120.12:/tmp/gpioinfo
scp .../gpioget root@192.168.120.12:/tmp/gpioget
chmod +x /tmp/gpioinfo /tmp/gpioget
```

확인한 GPIO:

```text
GPIO17 : active-low record button
GPIO20 : roll DIR
GPIO21 : pitch DIR
```

## 9. Controller Recipe 생성

controller recipe:

```text
recipes-gimbal/castle-gimbal/castle-gimbal-controller_1.0.bb
```

주요 내용:

```bitbake
SUMMARY = "Castle gimbal controller"
LICENSE = "MIT"
DEPENDS = "libgpiod"
RDEPENDS:${PN} += "libgpiod"
inherit systemd
SYSTEMD_SERVICE:${PN} = "castle-gimbal.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"
```

소스 등록:

```bitbake
file://Makefile
file://src/main.c
file://src/camera.c
file://src/camera.h
file://src/mpu6050.c
file://src/mpu6050.h
file://castle-gimbal.service
```

## 10. Makefile 구성

Makefile 위치:

```text
recipes-gimbal/castle-gimbal/files/Makefile
```

소스:

```make
SRCS = \
    src/main.c \
    src/camera.c \
    src/mpu6050.c
```

링크 라이브러리:

```make
LDLIBS += -lgpiod -lm
```

설명:

```text
-lgpiod : GPIO 버튼과 DIR 출력 제어용
-lm     : atan2f(), sqrtf() 같은 math 함수용
```

## 11. Camera Recording Module

파일:

```text
recipes-gimbal/castle-gimbal/files/src/camera.c
recipes-gimbal/castle-gimbal/files/src/camera.h
```

기능:

```text
camera_start_recording()
camera_stop_recording()
camera_is_recording()
```

사용 도구:

```text
/usr/bin/v4l2-ctl
```

녹화 설정:

```text
/dev/video0
1280x720
H264
```

녹화 경로:

```text
/root/recordings/gimbal-YYYYMMDD-HHMMSS.h264
```

## 12. Record Button

버튼:

```text
GPIO17 active-low
```

연결:

```text
GPIO17 physical pin 11
GND physical pin 9
```

동작:

```text
누르면 녹화 시작
다시 누르면 녹화 정지
```

## 13. PWM 초기화

PWM sysfs:

```text
/sys/class/pwm/pwmchip0/pwm0
/sys/class/pwm/pwmchip0/pwm1
```

초기화 순서:

```text
export
enable 확인
켜져 있으면 disable
period 설정
duty_cycle 0 설정
```

PWM period:

```text
100000 ns = 10 kHz
```

## 14. MPU6050 Module 생성

파일:

```text
recipes-gimbal/castle-gimbal/files/src/mpu6050.c
recipes-gimbal/castle-gimbal/files/src/mpu6050.h
```

헤더 구조체:

```c
struct mpu6050_data
struct mpu6050_angle
struct mpu6050_calibration
```

주요 함수:

```c
mpu6050_init()
mpu6050_read()
mpu6050_calibrate_gyro()
mpu6050_apply_gyro_calibration()
mpu6050_calculate_angle()
mpu6050_close()
```

I2C 정보:

```text
device  : /dev/i2c-1
address : 0x68
```

센서 wake-up:

```text
PWR_MGMT_1 register 0x6B에 0x00 기록
```

## 15. MPU6050 테스트

처음 오류:

```text
mpu6050: cannot write register 0x6B: Input/output error
i2cget read failed
i2cset write failed
```

확인:

```sh
i2cdetect -y 1
```

`0x68`은 보였지만 레지스터 통신이 실패했다.

해결:

```text
배선/전원 재확인
VCC 3.3V
GND GND
SDA GPIO2 physical pin 3
SCL GPIO3 physical pin 5
```

정상 출력 예:

```text
mpu6050: initialized on /dev/i2c-1 address 0x68
mpu6050: accel x=... y=... z=... g
mpu6050: gyro x=... y=... z=... deg/s
mpu6050: temperature ... C
```

## 16. Gyro Calibration

정지 상태에서도 gyro가 0이 아니었다.

예:

```text
gyro x=-3.xx y=0.xx z=-0.xx deg/s
```

해결:

```text
시작 시 100 sample 평균을 offset으로 저장
이후 gyro 값에서 offset 제거
```

결과:

```text
정지 상태 gyro 값이 0 근처로 감소
```

## 17. Roll/Pitch 계산

가속도 기반 각도 계산:

```text
roll  = atan2(accel_x, sqrt(accel_y^2 + accel_z^2))
pitch = atan2(accel_y, sqrt(accel_x^2 + accel_z^2))
```

확인 로그:

```text
mpu6050: angle roll=... pitch=... deg
```

## 18. Gimbal Control 계산

목표:

```text
roll target  = 0 deg
pitch target = 0 deg
```

오차:

```text
error = target - current
```

P 제어:

```text
control = error * Kp
```

deadband:

```text
1도 이하 작은 오차는 0으로 처리
```

PWM 변환:

```text
0%       : 정지
30~60%   : 동작 범위
```

방향:

```text
control > 0 -> direction 1
control < 0 -> direction -1
control = 0 -> direction 0
```

## 19. BLDC Driver / TXS0108E 연결

사용 레벨 시프터:

```text
TXS0108E
```

전원:

```text
VCCA -> Raspberry Pi 3.3V
VCCB -> Raspberry Pi 5V
OE   -> Raspberry Pi 3.3V
GND  -> Pi GND, BLDC Control GND 공통
```

신호:

```text
GPIO18/PWM0 -> A1 -> B1 -> Roll VSP
GPIO19/PWM1 -> A2 -> B2 -> Pitch VSP
GPIO20      -> A3 -> B3 -> Roll DIR
GPIO21      -> A4 -> B4 -> Pitch DIR
```

드라이버 설명:

```text
DIR high = CCW
DIR low  = CW
VSP      = 1~20 kHz, 30~100% duty
```

## 20. 배선 문서 생성

생성한 SVG:

```text
docs/wiring/raspberry-pi3-txs0108e-bldc-control-wiring.svg
```

기존 wiring 자료:

```text
docs/wiring/bldc-driver.webp
docs/wiring/bldc-pin-info.webp
docs/wiring/txs0108e-module.webp
docs/wiring/raspberry-pi3-bldc-wiring.svg
docs/wiring/raspberry-pi3-txs0108e-bldc-wiring.svg
```

## 21. 모터 연결 전 체크리스트

생성한 엑셀:

```text
docs/wiring/bldc-motor-preflight-checklist.xlsx
```

포함 내용:

```text
전원 OFF 확인
TXS0108E 전원 연결
GND 공통 연결
VSP/DIR 신호선 확인
BLDC 전원 극성 확인
U/V/W 모터선 확인
첫 전원 인가 전 안전 확인
```

## 22. 모터 방향 반전 문서

생성한 문서:

```text
docs/wiring/motor-direction-test.md
```

설명:

```text
모터가 기울기를 줄이면 정상
모터가 기울기를 키우면 해당 축 invert 1로 변경
```

설정:

```c
#define GIMBAL_ROLL_DIRECTION_INVERT 0
#define GIMBAL_PITCH_DIRECTION_INVERT 0
```

## 23. Systemd Service

서비스 파일:

```text
recipes-gimbal/castle-gimbal/files/castle-gimbal.service
```

자동 실행:

```text
SYSTEMD_AUTO_ENABLE:${PN} = "enable"
```

안전 종료:

```ini
ExecStopPost=/bin/sh -c 'echo 0 > /sys/class/pwm/pwmchip0/pwm0/enable'
ExecStopPost=/bin/sh -c 'echo 0 > /sys/class/pwm/pwmchip0/pwm1/enable'
```

확인 명령:

```sh
systemctl status castle-gimbal.service --no-pager
journalctl -u castle-gimbal.service -n 30 --no-pager
systemctl stop castle-gimbal.service
```

정지 후 확인:

```sh
cat /sys/class/pwm/pwmchip0/pwm0/enable
cat /sys/class/pwm/pwmchip0/pwm1/enable
/tmp/gpioget GPIO20
/tmp/gpioget GPIO21
```

정상 결과:

```text
0
0
"GPIO20"=inactive
"GPIO21"=inactive
```

## 24. RTSP Server 작업

RTSP server recipe:

```text
recipes-multimedia/castle-rtsp-server/castle-rtsp-server_1.0.bb
```

소스:

```text
recipes-multimedia/castle-rtsp-server/files/castle-rtsp-server.c
recipes-multimedia/castle-rtsp-server/files/Makefile
recipes-multimedia/castle-rtsp-server/files/castle-rtsp-server.service
```

RTSP 주소:

```text
rtsp://<board-ip>:8554/gimbal
```

주의:

```text
camera는 castle-gimbal recording 기능과 RTSP server가 동시에 점유할 수 있으므로 service conflict가 필요하다.
```

## 25. GitHub 작업

Repository:

```text
https://github.com/castle-007/meta-castle-gimbal.git
```

주요 Git 명령:

```sh
git status --short
git add <files>
git commit -m "Add MPU6050 gimbal control"
git push
```

인증 문제:

```text
GitHub password 인증 불가
Personal Access Token 필요
```

credential 정리:

```sh
printf "protocol=https\nhost=github.com\n\n" | git credential reject
```

push 완료:

```text
65be74f Add MPU6050 gimbal control
```

## 26. 생성한 정리/설계 문서

이번 세션 마지막에 생성한 문서:

```text
docs/development-summary.md
docs/debugging-summary.md
docs/software-design.md
docs/session-worklog.md
```

역할:

```text
development-summary.md : 개발 기능 정리
debugging-summary.md   : 문제 해결 기록
software-design.md     : 설계 문서
session-worklog.md     : 이번 세션 전체 작업 흐름
```

## 27. 현재 완료 상태

완료된 항목:

```text
Yocto custom layer 기반 개발 구조
castle-gimbal-controller recipe
systemd 자동 실행
GPIO17 녹화 버튼
카메라 H264 녹화
PWM0/PWM1 출력
MPU6050 I2C 센서 읽기
gyro offset 보정
roll/pitch 계산
deadband + P 제어
PWM duty 0 또는 30~60% 변환
DIR GPIO20/GPIO21 출력
TXS0108E/BLDC 배선 문서
모터 연결 전 엑셀 체크리스트
모터 방향 반전 문서
GitHub push
```

## 28. Yocto 이미지 빌드를 위해 작성한 내용 상세

이 프로젝트는 보드에서 직접 파일을 수정해서 끝내는 방식이 아니라, Yocto layer 안에 모든 내용을 recipe로 넣고 image를 다시 만들 수 있게 구성했다.

이렇게 한 이유:

```text
1. SD card를 새로 만들어도 같은 기능이 자동 포함됨
2. GitHub에서 meta-castle-gimbal을 다시 받아도 재현 가능
3. /usr/bin, systemd service, boot config, WiFi config를 수동 복사하지 않아도 됨
4. 개발 PC와 보드의 상태 차이를 줄일 수 있음
```

### 28.1 Layer가 필요한 이유

작성 위치:

```text
meta-castle-gimbal/
```

역할:

```text
우리 프로젝트 전용 Yocto layer
castle-gimbal image, controller, WiFi config, RTSP server, 문서를 모두 이 layer에 보관
```

왜 필요한가:

Yocto에서는 `poky`, `meta-raspberrypi`, `meta-openembedded` 같은 외부 layer를 직접 수정하지 않는 것이 좋다.  
외부 layer를 수정하면 나중에 업데이트하거나 다른 PC에서 다시 받을 때 변경 내용이 사라지거나 충돌하기 쉽다.

그래서 프로젝트 전용 layer인 `meta-castle-gimbal`을 만들고, 모든 custom 작업을 이 안에 넣었다.

### 28.2 layer.conf를 작성한 이유

작성 위치:

```text
meta-castle-gimbal/conf/layer.conf
```

역할:

```text
BitBake에게 이 layer 안의 recipe를 어디서 찾을지 알려줌
이 layer가 scarthgap과 호환된다는 정보 제공
```

중요 개념:

```text
BBFILES              : 이 layer 안에서 .bb/.bbappend 파일을 찾을 경로
BBFILE_COLLECTIONS  : layer collection 이름
BBFILE_PATTERN_*    : 해당 collection의 실제 layer 경로 pattern
LAYERSERIES_COMPAT  : 호환 Yocto release 이름
```

왜 `castlegimbal` 이름을 사용했는가:

```text
meta-castle-gimbal에는 하이픈이 있지만,
BitBake override 변수 이름에는 하이픈이 불편하거나 문제를 만들 수 있음
그래서 collection 이름은 castlegimbal처럼 단순하게 사용
```

관련 오류:

```text
ERROR: BBFILE_PATTERN_castlegimbal not defined
```

의미:

`BBFILE_COLLECTIONS`에 `castlegimbal`을 등록했지만, 그에 대응하는 `BBFILE_PATTERN_castlegimbal`이 없다는 뜻이다.

### 28.3 bblayers.conf에 layer를 추가한 이유

작성 위치:

```text
build-castle-test/conf/bblayers.conf
```

역할:

```text
이번 build directory에서 사용할 layer 목록 지정
```

왜 필요한가:

`meta-castle-gimbal` 폴더가 존재해도 `bblayers.conf`에 등록되어 있지 않으면 BitBake가 이 layer의 recipe를 읽지 않는다.

확인 명령:

```sh
cd ~/yocto-rpi
source poky/oe-init-build-env build-castle-test
bitbake-layers show-layers
```

정상 기대:

```text
meta-castle-gimbal ... /home/castle/yocto-rpi/meta-castle-gimbal
```

### 28.4 local.conf를 작성한 이유

작성 위치:

```text
build-castle-test/conf/local.conf
```

역할:

```text
이번 build directory의 MACHINE, image 기능, license 허용, boot config 등을 설정
```

대표 설정:

```text
MACHINE = "raspberrypi3"
LICENSE_FLAGS_ACCEPTED += "synaptics-killswitch"
RPI_EXTRA_CONFIG += "..."
```

왜 필요한가:

Raspberry Pi 3용 이미지를 만들려면 `MACHINE`이 맞아야 한다.  
WiFi firmware는 restricted license flag가 있어서 `LICENSE_FLAGS_ACCEPTED`가 필요했다.  
I2C/PWM/camera는 Raspberry Pi boot config에 들어가야 커널 부팅 시 장치가 활성화된다.

### 28.5 image recipe를 작성한 이유

작성 위치:

```text
meta-castle-gimbal/recipes-core/images/castle-gimbal-image.bb
```

역할:

```text
최종 rootfs에 어떤 package를 넣을지 결정
```

왜 필요한가:

Yocto에서 프로그램 recipe를 만들었다고 해서 자동으로 image에 들어가지는 않는다.  
최종 SD card image에 포함하려면 image recipe의 `IMAGE_INSTALL`에 package를 넣어야 한다.

예:

```bitbake
IMAGE_INSTALL:append = " castle-gimbal-controller"
```

이렇게 해야 `/usr/bin/castle-gimbal`과 `castle-gimbal.service`가 rootfs에 들어간다.

추가했던 대표 package:

```text
castle-gimbal-controller
castle-wifi-config
libgpiod-tools
v4l-utils
castle-rtsp-server
gstreamer 관련 package
```

확인 명령:

```sh
oe-pkgdata-util list-pkgs | grep castle
```

또는 보드에서:

```sh
ls /usr/bin/castle-gimbal
systemctl list-unit-files | grep castle
```

### 28.6 controller recipe를 작성한 이유

작성 위치:

```text
meta-castle-gimbal/recipes-gimbal/castle-gimbal/castle-gimbal-controller_1.0.bb
```

역할:

```text
C 소스를 빌드해서 /usr/bin/castle-gimbal로 설치
systemd service 파일을 rootfs에 설치
컴파일/실행 dependency 정의
```

중요 항목:

```bitbake
DEPENDS = "libgpiod"
RDEPENDS:${PN} += "libgpiod"
inherit systemd
SYSTEMD_SERVICE:${PN} = "castle-gimbal.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"
SRC_URI = "..."
```

각 항목을 작성한 이유:

```text
DEPENDS
  빌드할 때 gpiod.h와 libgpiod link library가 필요해서 작성

RDEPENDS:${PN}
  보드에서 실행할 때 libgpiod shared library가 필요해서 작성

inherit systemd
  Yocto가 systemd service 설치/enable 처리를 할 수 있게 작성

SYSTEMD_SERVICE:${PN}
  이 package가 설치할 service 파일 이름을 알려주기 위해 작성

SYSTEMD_AUTO_ENABLE:${PN} = "enable"
  이미지 부팅 후 castle-gimbal.service가 자동 시작되게 작성

SRC_URI
  BitBake가 소스 파일과 service 파일을 WORKDIR로 가져오게 하기 위해 작성
```

왜 `SRC_URI`가 중요한가:

`meta-castle-gimbal` 폴더에 파일이 있어도 `SRC_URI`에 없으면 BitBake 작업 폴더로 복사되지 않는다.  
그래서 `mpu6050.c/h`를 새로 만들었을 때 recipe에도 다음을 추가했다.

```bitbake
file://src/mpu6050.c
file://src/mpu6050.h
```

관련 오류 예:

```text
Unable to get checksum for ... file could not be found
```

의미:

`SRC_URI`에 적은 파일 이름과 실제 `files/` 아래 파일 이름이 다르다는 뜻이다.

### 28.7 Makefile을 작성한 이유

작성 위치:

```text
meta-castle-gimbal/recipes-gimbal/castle-gimbal/files/Makefile
```

역할:

```text
main.c, camera.c, mpu6050.c를 하나의 castle-gimbal 실행 파일로 컴파일
```

중요 내용:

```make
SRCS = \
    src/main.c \
    src/camera.c \
    src/mpu6050.c

LDLIBS += -lgpiod -lm
```

왜 필요한가:

C 파일은 폴더에 있다고 자동으로 컴파일되지 않는다.  
Makefile의 `SRCS`에 들어간 파일만 object로 빌드된다.

`-lgpiod`가 필요한 이유:

```text
GPIO17 버튼 입력
GPIO20/GPIO21 DIR 출력
```

`-lm`이 필요한 이유:

```text
atan2f()
sqrtf()
```

이 함수들은 `<math.h>`에 선언되어 있지만 link 단계에서 math library가 필요하다.

### 28.8 systemd service 파일을 작성한 이유

작성 위치:

```text
meta-castle-gimbal/recipes-gimbal/castle-gimbal/files/castle-gimbal.service
```

역할:

```text
부팅 시 /usr/bin/castle-gimbal 자동 실행
systemctl start/stop으로 제어
stop 후 PWM 강제 off
```

중요 내용:

```ini
ExecStart=/usr/bin/castle-gimbal
ExecStopPost=/bin/sh -c 'echo 0 > /sys/class/pwm/pwmchip0/pwm0/enable'
ExecStopPost=/bin/sh -c 'echo 0 > /sys/class/pwm/pwmchip0/pwm1/enable'
```

왜 `ExecStopPost`를 넣었는가:

프로그램 내부에서도 PWM을 끄지만, systemd stop 상황에서는 신호 처리나 종료 순서 문제로 PWM이 남을 수 있었다.  
그래서 서비스 종료 후 systemd가 한 번 더 PWM enable에 `0`을 쓰도록 이중 안전장치를 넣었다.

확인 명령:

```sh
systemctl stop castle-gimbal.service
cat /sys/class/pwm/pwmchip0/pwm0/enable
cat /sys/class/pwm/pwmchip0/pwm1/enable
```

정상:

```text
0
0
```

### 28.9 boot file 설정을 작성한 이유

작성 위치:

```text
meta-castle-gimbal 또는 build conf의 Raspberry Pi boot 설정
```

대표적으로 반영한 내용:

```text
dtoverlay=pwm-2chan
dtparam=i2c_arm=on
dtparam=i2c1=on
start_x=1
gpu_mem=128
```

왜 필요한가:

Linux userspace에서 `/sys/class/pwm`이나 `/dev/i2c-1`을 쓰려면 커널과 device tree가 해당 장치를 활성화해야 한다.

파일만 설치한다고 되는 것이 아니라 boot config에 overlay/parameter가 들어가야 한다.

PWM overlay 확인:

```sh
ls /boot/overlays | grep pwm
find /proc/device-tree -iname '*pwm*'
ls /sys/class/pwm
```

## 29. 파일별 작성 위치와 이유 상세

### 29.1 `src/main.c`

작성 위치:

```text
recipes-gimbal/castle-gimbal/files/src/main.c
```

작성한 이유:

```text
프로그램 전체 흐름을 담당하는 main controller
버튼, PWM, DIR, MPU6050, camera, service 종료 처리를 통합
```

주요 기능:

```text
record button 초기화
PWM0/PWM1 초기화
DIR GPIO20/GPIO21 초기화
MPU6050 초기화와 gyro 보정
gimbal control loop 실행
button press로 camera recording toggle
종료 시 PWM off, DIR low, 녹화 정지
```

### 29.2 `src/mpu6050.c`

작성 위치:

```text
recipes-gimbal/castle-gimbal/files/src/mpu6050.c
```

작성한 이유:

```text
MPU6050 관련 I2C 통신과 센서 계산을 main.c에서 분리
main.c가 너무 길어지는 것을 방지
센서 모듈만 따로 테스트/수정 가능
```

구현한 기능:

```text
register write
register burst read
raw 16-bit 값 조합
가속도/자이로/온도 단위 변환
gyro offset calibration
roll/pitch angle 계산
```

### 29.3 `src/mpu6050.h`

작성 위치:

```text
recipes-gimbal/castle-gimbal/files/src/mpu6050.h
```

작성한 이유:

```text
main.c에서 mpu6050.c 함수와 구조체를 사용할 수 있게 선언 제공
```

포함 내용:

```text
struct mpu6050_data
struct mpu6050_angle
struct mpu6050_calibration
mpu6050_* 함수 선언
```

### 29.4 `src/camera.c`

작성 위치:

```text
recipes-gimbal/castle-gimbal/files/src/camera.c
```

작성한 이유:

```text
카메라 녹화 시작/정지 로직을 main.c에서 분리
v4l2-ctl 실행과 pid 관리를 독립적으로 관리
```

### 29.5 `src/camera.h`

작성 위치:

```text
recipes-gimbal/castle-gimbal/files/src/camera.h
```

작성한 이유:

```text
main.c에서 camera_start_recording(), camera_stop_recording(), camera_is_recording() 사용
```

### 29.6 `docs/wiring/*.svg`

작성 위치:

```text
docs/wiring/
```

작성한 이유:

```text
하드웨어 연결은 나중에 다시 볼 때 실수하기 쉬움
그림으로 Raspberry Pi, TXS0108E, BLDC driver 연결을 남김
GitHub에 함께 올려 다른 PC에서도 확인 가능
```

대표 파일:

```text
raspberry-pi3-txs0108e-bldc-control-wiring.svg
```

### 29.7 `docs/wiring/bldc-motor-preflight-checklist.xlsx`

작성 위치:

```text
docs/wiring/bldc-motor-preflight-checklist.xlsx
```

작성한 이유:

```text
실제 모터 전원 연결 전 확인할 항목을 체크리스트로 관리
전원 극성, GND 공통, TXS0108E 전원, VSP/DIR 신호, U/V/W 연결 확인
```

### 29.8 `docs/wiring/motor-direction-test.md`

작성 위치:

```text
docs/wiring/motor-direction-test.md
```

작성한 이유:

```text
실제 모터 연결 후 방향이 반대일 때 어떤 define을 바꿔야 하는지 기록
```

사용할 define:

```c
#define GIMBAL_ROLL_DIRECTION_INVERT 0
#define GIMBAL_PITCH_DIRECTION_INVERT 0
```

## 30. 작성 내용을 이미지에 반영하는 전체 흐름

Yocto에서 파일을 수정한 뒤 보드에 반영되는 경로는 다음과 같다.

```text
meta-castle-gimbal/files/src/main.c 수정
-> castle-gimbal-controller_1.0.bb의 SRC_URI에 포함
-> bitbake castle-gimbal-controller
-> Makefile로 castle-gimbal 빌드
-> do_install에서 /usr/bin/castle-gimbal 설치
-> image recipe가 castle-gimbal-controller package 포함
-> bitbake castle-gimbal-image
-> wic image 생성
-> SD card flash
-> 보드 부팅
-> systemd가 castle-gimbal.service 실행
```

개발 중에는 전체 image를 매번 굽지 않고 빠르게 테스트했다.

빠른 테스트 흐름:

```sh
cd ~/yocto-rpi
source poky/oe-init-build-env build-castle-test
bitbake castle-gimbal-controller

cd ~/yocto-rpi/build-castle-test
scp tmp/work/cortexa7t2hf-neon-vfpv4-poky-linux-gnueabi/castle-gimbal-controller/1.0/package/usr/bin/castle-gimbal \
    root@192.168.120.12:/tmp/castle-gimbal-mpu
```

보드에서:

```sh
systemctl stop castle-gimbal.service
chmod +x /tmp/castle-gimbal-mpu
/tmp/castle-gimbal-mpu
```

검증 후 실제 서비스 반영:

```sh
systemctl stop castle-gimbal.service
cp /tmp/castle-gimbal-mpu /usr/bin/castle-gimbal
chmod +x /usr/bin/castle-gimbal
systemctl start castle-gimbal.service
```

왜 이렇게 했는가:

```text
전체 image 빌드와 SD card flashing은 시간이 오래 걸림
controller binary만 바뀐 경우에는 /tmp로 복사해 빠르게 테스트 가능
충분히 검증한 뒤 /usr/bin에 반영
```

주의:

이 빠른 테스트는 개발 중 편의를 위한 방식이다.  
최종 배포용 SD card에는 반드시 image recipe와 package에 반영한 뒤 image를 다시 빌드해야 한다.

## 31. 다음에 진행할 만한 작업

추천 순서:

```text
1. 실제 모터 연결 전 체크리스트 확인
2. 모터 전원 없이 TXS0108E B-side 신호 전압 확인
3. 모터 전원 연결 후 짧은 테스트
4. 방향 반대 축 invert 설정
5. Kp 조정
6. 최대 duty 조정
7. complementary filter 추가
8. PID 제어 추가
9. BRK 핀 제어 추가
10. FG 속도 피드백 추가
```

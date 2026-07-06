# Castle Gimbal Beginner Step-by-Step Guide

이 문서는 Castle Gimbal 프로젝트를 처음 시작하는 초보자가 같은 작업을 다시 따라 할 수 있도록 만든 전체 가이드이다.

문서 구성:

```text
1부: 처음부터 개발 환경 만들기
2부: Yocto layer와 image 구성
3부: 보드 기능 설정
4부: WiFi/SSH 설정
5부: C controller recipe 만들기
6부: 카메라 녹화 기능
7부: PWM 기능
8부: MPU6050 센서 기능
9부: 짐벌 제어 기능
10부: TXS0108E/BLDC 연결
11부: RTSP 영상 전송
12부: systemd 서비스 적용
13부: GitHub 업로드
14부: 디버깅 기록
```

## 0. 이 프로젝트에서 만든 결과물

최종 목표:

```text
Raspberry Pi 3 + Yocto scarthgap
MPU6050 자세 센서
BLDC motor driver 2개
TXS0108E level shifter
camera recording
RTSP video streaming
systemd 자동 실행
GitHub에 올려 재사용 가능한 custom meta layer
```

최종 custom layer:

```text
/home/castle/yocto-rpi/meta-castle-gimbal
```

GitHub:

```text
https://github.com/castle-007/meta-castle-gimbal.git
```

## 1. 기본 Yocto 폴더 준비

작업 기준 폴더:

```text
/home/castle/yocto-rpi
```

이 폴더가 필요한 이유:

```text
Yocto 관련 source layer와 build folder를 한 곳에 모아 관리하기 위해서
```

기본 구조:

```text
yocto-rpi/
  poky/
  meta-raspberrypi/
  meta-openembedded/
  meta-castle-gimbal/
  build-castle-test/
```

각 폴더 역할:

```text
poky
  Yocto의 기본 build system과 OE-Core recipe가 들어 있음

meta-raspberrypi
  Raspberry Pi 보드 지원 layer

meta-openembedded
  추가 package와 library recipe 제공

meta-castle-gimbal
  이 프로젝트 전용 custom layer

build-castle-test
  실제 빌드 결과와 conf/local.conf, conf/bblayers.conf가 들어가는 build folder
```

## 2. scarthgap branch로 layer 받기

Yocto 버전은 scarthgap을 사용한다.

예시:

```sh
mkdir -p ~/yocto-rpi
cd ~/yocto-rpi

git clone -b scarthgap git://git.yoctoproject.org/poky
git clone -b scarthgap https://github.com/agherzan/meta-raspberrypi.git
git clone -b scarthgap https://github.com/openembedded/meta-openembedded.git
```

왜 branch를 맞추는가:

```text
poky, meta-raspberrypi, meta-openembedded는 같은 Yocto release branch를 쓰는 것이 안전하다.
서로 다른 branch를 섞으면 recipe 문법, package 이름, dependency가 맞지 않을 수 있다.
```

## 3. custom layer 만들기

폴더 위치:

```text
~/yocto-rpi/meta-castle-gimbal
```

Yocto에서 custom layer를 만들 때는 직접 `mkdir`만 하는 것보다 `bitbake-layers create-layer` 명령을 사용하는 것이 좋다.

이 명령을 쓰는 이유:

```text
기본 layer.conf를 자동으로 만들어 줌
COPYING.MIT, README 같은 기본 파일을 만들어 줌
Yocto가 인식할 수 있는 layer 기본 구조를 만들어 줌
처음 시작할 때 실수할 가능성을 줄여 줌
```

### 3.1 build 환경에 들어가기

먼저 Yocto 명령을 사용할 수 있도록 build 환경에 들어간다.

```sh
cd ~/yocto-rpi
source poky/oe-init-build-env build-castle-test
```

설명:

```text
source poky/oe-init-build-env build-castle-test
```

이 명령을 실행하면:

```text
현재 shell에서 bitbake, bitbake-layers 같은 Yocto 명령을 사용할 수 있게 됨
build-castle-test 폴더로 이동함
build-castle-test/conf/local.conf 생성
build-castle-test/conf/bblayers.conf 생성
```

처음에는 `build`라는 이름을 써도 된다.

예:

```sh
source poky/oe-init-build-env build
```

하지만 이 프로젝트에서는 테스트 build folder 이름을 명확하게 하기 위해 `build-castle-test`를 사용했다.

### 3.2 bitbake-layers create-layer로 layer 만들기

`oe-init-build-env`를 실행하면 현재 위치가 build folder로 바뀐다.

예:

```text
~/yocto-rpi/build-castle-test
```

이 위치에서 custom layer를 만든다.

```sh
bitbake-layers create-layer ../meta-castle-gimbal
```

왜 `../meta-castle-gimbal`인가:

```text
현재 위치가 ~/yocto-rpi/build-castle-test 이기 때문에
../meta-castle-gimbal 은 ~/yocto-rpi/meta-castle-gimbal 을 의미함
```

명령이 성공하면 대략 이런 파일이 생긴다.

```text
~/yocto-rpi/meta-castle-gimbal/
  conf/layer.conf
  COPYING.MIT
  README
  recipes-example/
```

### 3.3 생성된 layer 확인

```sh
ls -l ../meta-castle-gimbal
ls -l ../meta-castle-gimbal/conf/layer.conf
```

정상이라면 `conf/layer.conf`가 있어야 한다.

### 3.4 불필요한 example recipe 정리

`create-layer`가 예제 recipe를 만들 수 있다.

예:

```text
recipes-example/
```

프로젝트에서 사용하지 않는다면 삭제해도 된다.

```sh
rm -rf ../meta-castle-gimbal/recipes-example
```

왜 삭제하는가:

```text
예제 recipe가 남아 있으면 프로젝트와 관련 없는 파일이 Git에 올라갈 수 있음
초보자가 나중에 어떤 파일이 실제 필요한 파일인지 헷갈릴 수 있음
```

### 3.5 프로젝트용 폴더 추가 생성

`create-layer`는 기본 layer 구조만 만든다.  
우리가 사용할 image recipe, controller recipe, WiFi recipe, RTSP recipe, 문서 폴더는 직접 만들어야 한다.

```sh
cd ~/yocto-rpi
mkdir -p meta-castle-gimbal/recipes-core/images
mkdir -p meta-castle-gimbal/recipes-gimbal/castle-gimbal/files/src
mkdir -p meta-castle-gimbal/recipes-connectivity/castle-wifi-config/files
mkdir -p meta-castle-gimbal/recipes-multimedia/castle-rtsp-server/files
mkdir -p meta-castle-gimbal/docs/wiring
```

각 폴더를 만든 이유:

```text
recipes-core/images
  최종 Yocto image recipe를 넣는 위치

recipes-gimbal/castle-gimbal
  C로 작성한 짐벌 제어 프로그램 recipe를 넣는 위치

recipes-gimbal/castle-gimbal/files/src
  main.c, camera.c, mpu6050.c 같은 실제 C 소스를 넣는 위치

recipes-connectivity/castle-wifi-config
  WiFi 자동 연결 script와 service recipe를 넣는 위치

recipes-multimedia/castle-rtsp-server
  RTSP 영상 전송 server recipe를 넣는 위치

docs/wiring
  배선 그림, 체크리스트, 모터 방향 테스트 문서를 넣는 위치
```

### 3.6 layer를 bblayers.conf에 추가

custom layer를 만들기만 하면 BitBake가 자동으로 사용하지 않는다.  
현재 build folder에 이 layer를 등록해야 한다.

```sh
cd ~/yocto-rpi
source poky/oe-init-build-env build-castle-test
bitbake-layers add-layer ../meta-castle-gimbal
```

왜 필요한가:

```text
meta-castle-gimbal 폴더가 있어도 bblayers.conf에 등록되어 있지 않으면
BitBake가 castle-gimbal-image나 castle-gimbal-controller recipe를 찾지 못함
```

확인:

```sh
bitbake-layers show-layers
```

정상이라면 목록에 아래가 보여야 한다.

```text
meta-castle-gimbal
```

### 3.7 layer.conf 이름 수정

`bitbake-layers create-layer`가 만든 기본 `layer.conf`는 collection 이름이 원하는 이름과 다를 수 있다.

이 프로젝트에서는 collection 이름을 다음처럼 맞췄다.

```text
castlegimbal
```

수정 파일:

```text
meta-castle-gimbal/conf/layer.conf
```

수정해야 하는 이유:

```text
BBFILE_COLLECTIONS에 등록한 이름과
BBFILE_PATTERN_<이름>, BBFILE_PRIORITY_<이름>, LAYERSERIES_COMPAT_<이름>이
같은 이름을 사용해야 하기 때문
```

예:

```bitbake
BBFILE_COLLECTIONS += "castlegimbal"
BBFILE_PATTERN_castlegimbal = "^${LAYERDIR}/"
BBFILE_PRIORITY_castlegimbal = "6"
LAYERSERIES_COMPAT_castlegimbal = "scarthgap"
```

왜 custom layer를 만드는가:

```text
외부 layer를 직접 수정하지 않기 위해서
프로젝트 전용 recipe, service, source code, 문서를 한 곳에 보관하기 위해서
GitHub에 이 layer만 올리면 다른 PC에서도 같은 프로젝트를 다시 빌드할 수 있게 하기 위해서
```

## 4. layer.conf 작성

파일 위치:

```text
meta-castle-gimbal/conf/layer.conf
```

파일 확장자:

```text
.conf
```

이 파일이 필요한 이유:

```text
BitBake에게 meta-castle-gimbal이 Yocto layer라는 것을 알려주기 위해서
이 layer 안에서 .bb, .bbappend recipe를 찾을 위치를 알려주기 위해서
scarthgap과 호환된다는 정보를 주기 위해서
```

핵심 내용:

```bitbake
BBPATH .= ":${LAYERDIR}"

BBFILES += "${LAYERDIR}/recipes-*/*/*.bb \
            ${LAYERDIR}/recipes-*/*/*.bbappend"

BBFILE_COLLECTIONS += "castlegimbal"
BBFILE_PATTERN_castlegimbal = "^${LAYERDIR}/"
BBFILE_PRIORITY_castlegimbal = "6"

LAYERSERIES_COMPAT_castlegimbal = "scarthgap"
```

왜 `castlegimbal` 이름을 쓰는가:

```text
meta-castle-gimbal에는 하이픈이 있지만,
BitBake override 변수 이름에는 하이픈이 들어가면 다루기 불편하다.
그래서 collection 이름은 castlegimbal처럼 단순한 이름으로 쓴다.
```

## 5. build folder 만들기

명령:

```sh
cd ~/yocto-rpi
source poky/oe-init-build-env build-castle-test
```

생성되는 폴더:

```text
~/yocto-rpi/build-castle-test
```

중요 파일:

```text
build-castle-test/conf/bblayers.conf
build-castle-test/conf/local.conf
```

왜 build folder를 따로 쓰는가:

```text
source code layer와 빌드 결과물을 분리하기 위해서
같은 source로 여러 설정의 build folder를 만들 수 있게 하기 위해서
```

## 6. bblayers.conf에 layer 추가

파일 위치:

```text
build-castle-test/conf/bblayers.conf
```

이 파일이 필요한 이유:

```text
이번 build folder에서 어떤 layer를 사용할지 지정하는 파일
meta-castle-gimbal 폴더가 있어도 bblayers.conf에 없으면 BitBake가 읽지 않음
```

추가할 layer:

```text
poky/meta
poky/meta-poky
poky/meta-yocto-bsp
meta-raspberrypi
meta-openembedded/meta-oe
meta-openembedded/meta-python
meta-openembedded/meta-networking
meta-openembedded/meta-multimedia
meta-castle-gimbal
```

확인:

```sh
bitbake-layers show-layers
```

정상이라면:

```text
meta-castle-gimbal
```

이 목록에 보여야 한다.

## 7. local.conf 설정

파일 위치:

```text
build-castle-test/conf/local.conf
```

이 파일이 필요한 이유:

```text
이번 build folder의 MACHINE, license, Raspberry Pi boot config, package format 등을 설정하기 위해서
```

중요 설정:

```bitbake
MACHINE = "raspberrypi3"
```

왜 필요한가:

```text
Raspberry Pi 3용 kernel, boot files, device tree, tuning을 선택하기 위해서
```

WiFi firmware license:

```bitbake
LICENSE_FLAGS_ACCEPTED += "synaptics-killswitch"
```

왜 필요한가:

```text
Raspberry Pi WiFi firmware package가 restricted license flag를 요구하기 때문에
```

Raspberry Pi boot config:

```bitbake
RPI_EXTRA_CONFIG = " \
dtparam=i2c_arm=on\n\
dtparam=i2c1=on\n\
dtparam=spi=on\n\
dtparam=audio=off\n\
dtoverlay=pwm-2chan\n\
start_x=1\n\
gpu_mem=128\n\
"
```

왜 필요한가:

```text
I2C  : MPU6050 센서 사용
PWM  : BLDC driver VSP 제어
Camera : 영상 녹화
audio off : PWM/audio 충돌 가능성 줄임
gpu_mem 128 : camera 기능에 필요한 GPU memory 확보
```

## 8. image recipe 작성

파일 위치:

```text
meta-castle-gimbal/recipes-core/images/castle-gimbal-image.bb
```

파일 확장자:

```text
.bb
```

이 파일이 필요한 이유:

```text
최종 SD card image에 어떤 package를 넣을지 결정하기 위해서
```

중요 개념:

```text
recipe를 만들었다고 자동으로 image에 들어가지 않는다.
image recipe의 IMAGE_INSTALL에 넣어야 rootfs에 포함된다.
```

예:

```bitbake
SUMMARY = "Castle gimbal image"

inherit core-image

IMAGE_INSTALL:append = " \
    castle-gimbal-controller \
    castle-wifi-config \
    libgpiod-tools \
    v4l-utils \
"
```

왜 넣는가:

```text
castle-gimbal-controller : 우리가 만든 /usr/bin/castle-gimbal 프로그램
castle-wifi-config      : WiFi 자동 연결 설정
libgpiod-tools          : gpioinfo, gpioget 테스트 도구
v4l-utils               : v4l2-ctl camera 테스트 도구
```

## 9. PWM overlay boot file 반영

문제:

```text
/sys/class/pwm은 있는데 pwmchip0이 없음
/boot/overlays에 pwm-2chan.dtbo가 없음
```

확인:

```sh
find tmp/work -name 'pwm-2chan.dtbo' | head
find tmp/deploy/images/raspberrypi3 -name '*pwm*dtbo*'
```

원인:

```text
pwm-2chan.dtbo는 rpi-bootfiles work directory에는 있었지만,
boot partition에 복사되지 않았음
```

해결:

```text
IMAGE_BOOT_FILES에 pwm-2chan.dtbo;overlays/pwm-2chan.dtbo를 추가
```

왜 필요한가:

```text
dtoverlay=pwm-2chan 설정이 있어도 실제 dtbo 파일이 boot partition에 없으면 적용되지 않음
```

정상 확인:

```sh
ls /boot/overlays | grep pwm
ls /sys/class/pwm
cat /sys/class/pwm/pwmchip0/npwm
```

정상 결과:

```text
pwm-2chan.dtbo
pwmchip0
2
```

## 10. WiFi 설정 recipe 작성

폴더:

```text
meta-castle-gimbal/recipes-connectivity/castle-wifi-config
```

파일:

```text
castle-wifi-config_1.0.bb
files/castle-wifi-connect
files/castle-wifi-connect.service
files/wpa_supplicant.conf.example
```

왜 이 위치인가:

```text
WiFi는 connectivity 기능이므로 recipes-connectivity 아래에 둠
설정 script와 service는 recipe의 files 폴더에 둠
```

왜 `.example` 파일을 쓰는가:

```text
실제 WiFi SSID/비밀번호를 GitHub에 올리면 안 되기 때문에
예시 파일만 repository에 포함
```

중요:

```text
wpa_supplicant.conf.example 이름이 SRC_URI와 실제 파일명에서 반드시 같아야 함
```

오류 예:

```text
Unable to get checksum for ... wpa_supplicant.conf.example: file could not be found
```

원인:

```text
실제 파일 이름이 wap_supplicant.conf.example로 잘못 되어 있었음
```

## 11. controller recipe 작성

폴더:

```text
meta-castle-gimbal/recipes-gimbal/castle-gimbal
```

recipe 파일:

```text
castle-gimbal-controller_1.0.bb
```

왜 `recipes-gimbal`인가:

```text
이 프로젝트의 핵심 기능이 짐벌 제어이므로 별도 recipes-gimbal 분류를 만들었음
```

왜 recipe 이름을 `castle-gimbal-controller`로 했는가:

```text
기존에 PN override 충돌 문제가 있었기 때문에
단순한 castle-gimbal 대신 controller 성격이 드러나는 이름으로 분리
```

중요 내용:

```bitbake
SUMMARY = "Castle gimbal controller"
LICENSE = "MIT"
DEPENDS = "libgpiod"
RDEPENDS:${PN} += "libgpiod"

inherit systemd

SYSTEMD_SERVICE:${PN} = "castle-gimbal.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"
```

왜 필요한가:

```text
DEPENDS
  빌드 중 gpiod.h와 libgpiod가 필요

RDEPENDS
  보드 실행 중 libgpiod.so가 필요

inherit systemd
  service 파일을 Yocto 방식으로 설치/enable하기 위해 필요

SYSTEMD_AUTO_ENABLE
  부팅 시 castle-gimbal.service 자동 시작
```

SRC_URI:

```bitbake
SRC_URI = " \
    file://Makefile \
    file://src/main.c \
    file://src/camera.c \
    file://src/camera.h \
    file://src/mpu6050.c \
    file://src/mpu6050.h \
    file://castle-gimbal.service \
"
```

왜 필요한가:

```text
BitBake는 SRC_URI에 있는 파일만 WORKDIR로 복사해서 빌드함
src 폴더에 파일이 있어도 SRC_URI에 없으면 빌드에 들어가지 않음
```

## 12. Makefile 작성

파일:

```text
meta-castle-gimbal/recipes-gimbal/castle-gimbal/files/Makefile
```

왜 필요한가:

```text
C 파일 여러 개를 castle-gimbal 실행 파일 하나로 빌드하기 위해서
Yocto recipe의 do_compile에서 oe_runmake를 호출하기 위해서
```

중요 내용:

```make
CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
LDLIBS += -lgpiod -lm

TARGET = castle-gimbal

SRCS = \
    src/main.c \
    src/camera.c \
    src/mpu6050.c
```

왜 `src/mpu6050.c`를 추가했는가:

```text
MPU6050 코드를 새 파일로 만들었기 때문에 Makefile이 컴파일하도록 등록해야 함
```

왜 `-lgpiod`가 필요한가:

```text
GPIO17 버튼과 GPIO20/GPIO21 DIR 제어에 libgpiod 사용
```

왜 `-lm`이 필요한가:

```text
roll/pitch 계산에서 atan2f(), sqrtf() 사용
```

## 13. systemd service 작성

파일:

```text
meta-castle-gimbal/recipes-gimbal/castle-gimbal/files/castle-gimbal.service
```

왜 필요한가:

```text
보드 부팅 시 castle-gimbal을 자동 실행하기 위해서
systemctl start/stop으로 프로그램을 관리하기 위해서
```

핵심:

```ini
[Service]
ExecStart=/usr/bin/castle-gimbal
ExecStopPost=/bin/sh -c 'echo 0 > /sys/class/pwm/pwmchip0/pwm0/enable'
ExecStopPost=/bin/sh -c 'echo 0 > /sys/class/pwm/pwmchip0/pwm1/enable'
```

왜 `ExecStopPost`를 넣었는가:

```text
systemctl stop 후 PWM이 1로 남는 문제가 있었음
프로그램 내부 정리 외에 systemd가 한 번 더 PWM을 끄는 안전장치를 추가함
```

## 14. main.c 작성

파일:

```text
meta-castle-gimbal/recipes-gimbal/castle-gimbal/files/src/main.c
```

왜 여기에 작성하는가:

```text
castle-gimbal 프로그램의 전체 동작 흐름을 담당하는 main source
```

담당 기능:

```text
record button GPIO17 초기화
PWM0/PWM1 초기화
DIR GPIO20/GPIO21 초기화
MPU6050 초기화와 보정
짐벌 제어 루프
카메라 녹화 버튼 처리
종료 시 안전 정리
```

중요 상수:

```c
#define GIMBAL_TARGET_ROLL_DEG 0.0f
#define GIMBAL_TARGET_PITCH_DEG 0.0f
#define GIMBAL_ROLL_KP 1.0f
#define GIMBAL_PITCH_KP 1.0f
#define GIMBAL_STOP_DUTY_PERCENT 0.0f
#define GIMBAL_MIN_RUN_DUTY_PERCENT 30.0f
#define GIMBAL_MAX_DUTY_PERCENT 60.0f
#define GIMBAL_DEADBAND_DEG 1.0f
#define GIMBAL_CONTROL_INTERVAL_US 20000
#define GIMBAL_LOG_INTERVAL_COUNT 25
#define GIMBAL_ROLL_DIR_GPIO 20
#define GIMBAL_PITCH_DIR_GPIO 21
```

왜 필요한가:

```text
TARGET_*       : 짐벌이 맞추려는 목표 각도
KP             : P 제어 게인
DUTY           : BLDC 드라이버 VSP 출력 범위
DEADBAND       : 작은 흔들림 무시
INTERVAL       : 제어 주기 20ms
LOG_INTERVAL   : 로그를 0.5초마다 출력
DIR_GPIO       : 방향 제어 GPIO 번호
```

## 15. camera.c / camera.h 작성

파일:

```text
src/camera.c
src/camera.h
```

왜 분리했는가:

```text
카메라 녹화 시작/정지 로직을 main.c에서 분리해 관리하기 위해서
```

기능:

```c
int camera_start_recording(const char *output_path);
int camera_stop_recording(void);
int camera_is_recording(void);
```

내부 동작:

```text
fork/exec로 v4l2-ctl 실행
H264 stream을 파일로 저장
녹화 중이면 pid를 관리
```

## 16. mpu6050.c / mpu6050.h 작성

파일:

```text
src/mpu6050.c
src/mpu6050.h
```

왜 분리했는가:

```text
I2C 센서 통신과 roll/pitch 계산을 main.c에서 분리하기 위해서
센서 코드만 독립적으로 수정/테스트하기 위해서
```

구조체:

```c
struct mpu6050_data
struct mpu6050_angle
struct mpu6050_calibration
```

함수:

```c
mpu6050_init()
mpu6050_read()
mpu6050_calibrate_gyro()
mpu6050_apply_gyro_calibration()
mpu6050_calculate_angle()
mpu6050_close()
```

왜 `mpu6050.h`가 필요한가:

```text
main.c에서 mpu6050.c의 함수와 구조체를 알 수 있게 선언을 제공
```

## 17. 빌드 방법

controller만 빠르게 빌드:

```sh
cd ~/yocto-rpi
source poky/oe-init-build-env build-castle-test
bitbake castle-gimbal-controller
```

왜 controller만 빌드하는가:

```text
C 코드 수정 확인은 전체 image보다 controller recipe만 빌드하는 것이 빠름
```

최종 image 빌드:

```sh
bitbake castle-gimbal-image
```

왜 image를 빌드하는가:

```text
SD card에 넣을 rootfs와 boot files를 모두 포함한 최종 결과물을 만들기 위해서
```

## 18. 빠른 보드 테스트 방법

빌드된 binary 위치:

```text
~/yocto-rpi/build-castle-test/tmp/work/cortexa7t2hf-neon-vfpv4-poky-linux-gnueabi/castle-gimbal-controller/1.0/package/usr/bin/castle-gimbal
```

보드로 복사:

```sh
cd ~/yocto-rpi/build-castle-test
scp tmp/work/cortexa7t2hf-neon-vfpv4-poky-linux-gnueabi/castle-gimbal-controller/1.0/package/usr/bin/castle-gimbal \
    root@192.168.120.12:/tmp/castle-gimbal-mpu
```

보드에서 실행:

```sh
systemctl stop castle-gimbal.service
chmod +x /tmp/castle-gimbal-mpu
/tmp/castle-gimbal-mpu
```

왜 `/tmp`로 테스트하는가:

```text
기존 /usr/bin/castle-gimbal을 바로 덮어쓰지 않고 안전하게 테스트하기 위해서
```

검증 후 실제 반영:

```sh
systemctl stop castle-gimbal.service
cp /tmp/castle-gimbal-mpu /usr/bin/castle-gimbal
chmod +x /usr/bin/castle-gimbal
systemctl start castle-gimbal.service
```

## 19. 카메라 녹화 테스트

확인:

```sh
v4l2-ctl -d /dev/video0 --list-formats-ext
```

H264가 보이면 녹화 가능하다.

녹화 결과:

```text
/root/recordings/gimbal-YYYYMMDD-HHMMSS.h264
```

버튼:

```text
GPIO17 active-low
```

## 20. PWM 테스트

확인:

```sh
cat /sys/class/pwm/pwmchip0/npwm
```

정상:

```text
2
```

PWM sysfs:

```text
/sys/class/pwm/pwmchip0/pwm0
/sys/class/pwm/pwmchip0/pwm1
```

확인:

```sh
cat /sys/class/pwm/pwmchip0/pwm0/period
cat /sys/class/pwm/pwmchip0/pwm0/duty_cycle
cat /sys/class/pwm/pwmchip0/pwm0/enable
```

## 21. MPU6050 테스트

I2C 주소 확인:

```sh
i2cdetect -y 1
```

정상:

```text
68
```

WHO_AM_I:

```sh
i2cget -y 1 0x68 0x75
```

정상:

```text
0x68
```

프로그램 로그:

```text
mpu6050: initialized on /dev/i2c-1 address 0x68
mpu6050: accel ...
mpu6050: gyro ...
mpu6050: angle roll=... pitch=...
```

## 22. 짐벌 제어 테스트

정상 로그:

```text
gimbal: angle roll=... pitch=... deg, pwm roll=...% pitch=...%, direction roll=... pitch=...
```

PWM 값 규칙:

```text
deadband 안쪽 -> 0.00%
deadband 밖   -> 최소 30.00%
최대           -> 60.00%
```

확인:

```sh
cat /sys/class/pwm/pwmchip0/pwm0/duty_cycle
cat /sys/class/pwm/pwmchip0/pwm0/enable
cat /sys/class/pwm/pwmchip0/pwm1/duty_cycle
cat /sys/class/pwm/pwmchip0/pwm1/enable
```

30%일 때:

```text
30000
1
30000
1
```

## 23. TXS0108E와 BLDC 연결

문서:

```text
docs/wiring/raspberry-pi3-txs0108e-bldc-control-wiring.svg
```

연결:

```text
Pi GPIO18/PWM0 -> TXS0108E A1 -> B1 -> Roll BLDC VSP
Pi GPIO19/PWM1 -> TXS0108E A2 -> B2 -> Pitch BLDC VSP
Pi GPIO20      -> TXS0108E A3 -> B3 -> Roll BLDC DIR
Pi GPIO21      -> TXS0108E A4 -> B4 -> Pitch BLDC DIR
```

전원:

```text
TXS0108E VCCA -> Pi 3.3V
TXS0108E VCCB -> Pi 5V
TXS0108E OE   -> Pi 3.3V
TXS0108E GND  -> Pi GND, BLDC Control GND 공통
```

왜 GND 공통이 필요한가:

```text
Pi와 BLDC driver가 같은 0V 기준을 가져야 DIR/PWM 신호의 high/low가 올바르게 인식됨
```

## 24. 모터 연결 전 체크리스트

파일:

```text
docs/wiring/bldc-motor-preflight-checklist.xlsx
```

왜 만들었는가:

```text
모터 전원은 위험할 수 있으므로 전원 극성, GND 공통, 신호선, U/V/W 연결을 빠짐없이 확인하기 위해서
```

## 25. 방향 반전 방법

파일:

```text
docs/wiring/motor-direction-test.md
```

코드 설정:

```c
#define GIMBAL_ROLL_DIRECTION_INVERT 0
#define GIMBAL_PITCH_DIRECTION_INVERT 0
```

기준:

```text
모터가 기울기를 줄이면 정상
모터가 기울기를 키우면 해당 축 invert를 1로 변경
```

## 26. RTSP 영상 전송

폴더:

```text
recipes-multimedia/castle-rtsp-server
```

파일:

```text
castle-rtsp-server_1.0.bb
files/castle-rtsp-server.c
files/Makefile
files/castle-rtsp-server.service
```

왜 필요한가:

```text
PC 또는 Android 앱에서 카메라 영상을 실시간으로 보기 위해서
```

RTSP 주소:

```text
rtsp://<board-ip>:8554/gimbal
```

주의:

```text
camera는 동시에 여러 프로그램이 잡으면 충돌할 수 있음
castle-gimbal 녹화 기능과 RTSP server는 동시에 사용하지 않는 구조로 시작
```

### 26.1 RTSP 시청

PC 또는 Android에서 볼 주소:

```text
rtsp://192.168.121.129:8554/gimbal
```

PC에서 `ffplay`로 낮은 지연 테스트:

```sh
ffplay \
  -rtsp_transport tcp \
  -fflags nobuffer \
  -flags low_delay \
  -framedrop \
  -probesize 32 \
  -analyzeduration 0 \
  rtsp://192.168.121.129:8554/gimbal
```

확인된 상태:

```text
1280x720
30fps
H264
지연 약 3.5초
```

### 26.2 PC에서 RTSP 녹화

직접 명령:

```sh
ffmpeg \
  -rtsp_transport tcp \
  -i rtsp://192.168.121.129:8554/gimbal \
  -c copy \
  -movflags +faststart \
  gimbal-rtsp-recording.mp4
```

왜 이렇게 하는가:

```text
-rtsp_transport tcp
  네트워크 안정성을 위해 TCP로 RTSP 수신

-c copy
  H264 영상을 다시 인코딩하지 않고 그대로 저장
  CPU 사용이 적고 화질 손실이 없음

-movflags +faststart
  mp4 파일 재생 호환성을 좋게 하기 위한 옵션
```

녹화 종료:

```text
ffmpeg 터미널에서 q 입력
```

### 26.3 PC 녹화 스크립트

파일:

```text
tools/record-rtsp.sh
```

왜 만들었는가:

```text
매번 긴 ffmpeg 명령을 입력하지 않기 위해서
파일명을 날짜/시간으로 자동 생성하기 위해서
```

사용:

```sh
cd ~/yocto-rpi/meta-castle-gimbal
tools/record-rtsp.sh
```

저장 폴더 지정:

```sh
tools/record-rtsp.sh rtsp://192.168.121.129:8554/gimbal ~/Videos
```

출력 파일 예:

```text
gimbal-rtsp-20260706-134600.mp4
```

### 26.4 Android에서 RTSP 녹화

가장 쉬운 방법:

```text
1. Android VLC 또는 RTSP player 앱 실행
2. rtsp://192.168.121.129:8554/gimbal 입력
3. 영상 확인
4. Android 기본 화면 녹화 기능으로 녹화
```

원본 stream 저장이 필요하면 Play Store에서 아래 키워드로 앱을 찾는다.

```text
RTSP recorder
IP camera recorder
RTSP player recorder
```

권장 설정:

```text
Transport : TCP
Codec     : copy 또는 original
Format    : mp4
```

## 27. GitHub 업로드

초기화:

```sh
cd ~/yocto-rpi/meta-castle-gimbal
git status
git add <필요 파일>
git commit -m "Add MPU6050 gimbal control"
git push
```

왜 `git add .`를 피했는가:

```text
WiFi 비밀번호나 임시 파일이 실수로 올라가는 것을 막기 위해서
```

민감 정보 확인:

```sh
grep -RIn "psk=\\|password\\|ssid\\|PASS\\|SECRET" \
  recipes-gimbal docs/wiring \
  --exclude='*.png' \
  --exclude='*.webp' \
  --exclude='*.xlsx'
```

GitHub 인증:

```text
GitHub password가 아니라 Personal Access Token 필요
```

## 28. 최종 문서

생성한 문서:

```text
docs/development-summary.md
docs/debugging-summary.md
docs/software-design.md
docs/session-worklog.md
docs/beginner-step-by-step-guide.md
```

이 문서의 목적:

```text
처음부터 그대로 따라 할 수 있는 전체 개발 가이드
뒤쪽에 디버깅 기록 포함
```

# 디버깅 기록

아래는 개발 중 발생한 문제와 해결 방법을 따로 정리한 것이다.

## D1. BBFILE_PATTERN_castlegimbal not defined

오류:

```text
ERROR: BBFILE_PATTERN_castlegimbal not defined
```

원인:

```text
BBFILE_COLLECTIONS에 castlegimbal을 등록했지만
BBFILE_PATTERN_castlegimbal 변수가 없었음
```

해결:

```bitbake
BBFILE_COLLECTIONS += "castlegimbal"
BBFILE_PATTERN_castlegimbal = "^${LAYERDIR}/"
```

## D2. WiFi firmware license 오류

오류:

```text
linux-firmware-rpidistro-bcm43430 skipped
restricted license 'synaptics-killswitch'
```

해결:

```bitbake
LICENSE_FLAGS_ACCEPTED += "synaptics-killswitch"
```

## D3. TMPDIR changed location

오류:

```text
Error, TMPDIR has changed location
```

원인:

```text
기존 build folder의 tmp 위치와 현재 설정이 달라짐
```

해결:

```text
기존 tmp를 삭제하거나 새 build folder 기준으로 다시 빌드
```

## D4. pwmchip0이 안 보임

증상:

```text
ls /sys/class/pwm
결과 없음
```

확인:

```sh
find /boot/overlays | grep pwm
find tmp/work -name 'pwm-2chan.dtbo'
```

원인:

```text
pwm-2chan.dtbo가 boot partition에 복사되지 않음
```

해결:

```text
IMAGE_BOOT_FILES에 pwm-2chan.dtbo;overlays/pwm-2chan.dtbo 추가
```

## D5. PWM duty_cycle Invalid argument

오류:

```text
cannot write duty_cycle: Invalid argument
```

원인:

```text
period/duty/enable 설정 순서 문제
```

해결 순서:

```text
export
enable이 켜져 있으면 disable
period 설정
duty_cycle 설정
enable 설정
```

## D6. pseudo path mismatch

오류:

```text
pseudo abort
path mismatch
```

해결:

```text
해당 recipe clean 후 재빌드
```

## D7. SSH No route to host

증상:

```text
ssh: No route to host
```

원인:

```text
wlan0과 eth0이 같은 subnet에 있어 routing 혼선 발생
```

해결:

```sh
ip link set wlan0 down
```

또는 routing을 정리한다.

## D8. rootfs 저장 공간 부족

증상:

```text
not enough recording space: 72 MB free
```

원인:

```text
SD card 용량은 크지만 root partition은 약 200MB만 사용 중
```

해결:

```text
parted로 partition 2 확장
Yocto native e2fsck/resize2fs로 filesystem 확장
```

## D9. e2fsck FEATURE_C12 오류

오류:

```text
unsupported feature(s): FEATURE_C12
Get a newer version of e2fsck
```

원인:

```text
Host e2fsck가 구버전
```

해결:

```sh
bitbake e2fsprogs-native -c addto_recipe_sysroot
oe-run-native e2fsprogs-native e2fsck -f /dev/sdg2
oe-run-native e2fsprogs-native resize2fs /dev/sdg2
```

## D10. gpioinfo not found

원인:

```text
보드 image에 libgpiod-tools가 없거나 command가 설치되지 않음
```

해결:

```text
image에 libgpiod-tools 추가
또는 빌드 결과의 gpioinfo/gpioget를 /tmp로 복사
```

## D11. gpiod.h not found

오류:

```text
fatal error: gpiod.h: No such file or directory
```

해결:

```bitbake
DEPENDS = "libgpiod"
```

## D12. gpiod_line_request_set_value 인자 오류

오류:

```text
too few arguments to function 'gpiod_line_request_set_value'
```

원인:

```text
libgpiod v2는 request, line_offset, value를 받음
```

해결:

```c
gpiod_line_request_set_value(gpio->request,
                             gpio->line_offset,
                             line_value)
```

## D13. GPIO17 busy

오류:

```text
cannot request GPIO17: Device or resource busy
```

원인:

```text
castle-gimbal.service가 이미 GPIO17을 사용 중
```

해결:

```sh
systemctl stop castle-gimbal.service
```

## D14. MPU6050 write/read failed

오류:

```text
cannot write register 0x6B: Input/output error
i2cget read failed
i2cset write failed
```

원인:

```text
I2C 주소는 보였지만 실제 레지스터 통신 실패
배선/전원 접촉 문제 가능성
```

해결:

```text
VCC 3.3V
GND GND
SDA GPIO2
SCL GPIO3
배선 재확인
```

## D15. gyro 값이 0이 아님

증상:

```text
정지 상태에서도 gyro x=-3.xx deg/s
```

해결:

```text
시작 시 100 sample 평균을 offset으로 계산
이후 gyro 값에서 offset 제거
```

## D16. PWM 적용했는데 sysfs가 0

원인:

```text
프로그램이 종료되면서 정리 코드가 PWM을 꺼버림
```

해결:

```text
실행 중 다른 터미널에서 확인
임시 sleep으로 확인
```

## D17. systemctl stop 후 PWM이 남음

증상:

```text
systemctl stop 후 pwm enable이 1
```

해결:

```text
force_disable_pwm_channel() 추가
service ExecStopPost 추가
```

## D18. 방향 GPIO 적용 실패

오류:

```text
gimbal: cannot apply roll direction
gimbal: control loop failed
```

원인:

```text
run_gimbal_control_loop가 init_output_gpio보다 먼저 실행됨
```

해결:

초기화 순서 변경:

```text
button
PWM
PWM 0 적용
DIR GPIO
MPU6050
calibration
control loop
```

## D19. gpioget cannot find line gpiochip0

원인:

```text
libgpiod v2 gpioget 사용법 차이
```

해결:

```sh
/tmp/gpioinfo | grep GPIO20
/tmp/gpioget GPIO20
```

## D20. GitHub password authentication failed

오류:

```text
Password authentication is not supported for Git operations
```

해결:

```text
GitHub Personal Access Token 사용
```

credential 삭제:

```sh
printf "protocol=https\nhost=github.com\n\n" | git credential reject
```

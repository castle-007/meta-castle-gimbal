# Castle Gimbal Development Summary

이 문서는 `meta-castle-gimbal`에서 지금까지 진행한 프로그램 개발 내용을 정리한다.

## 개발 환경

```text
Yocto branch        : scarthgap
Target board        : Raspberry Pi 3
Custom layer        : meta-castle-gimbal
Image recipe        : castle-gimbal-image
Controller recipe   : castle-gimbal-controller_1.0.bb
Controller binary   : /usr/bin/castle-gimbal
Service             : castle-gimbal.service
```

## 주요 소스 파일

```text
recipes-gimbal/castle-gimbal/files/src/main.c
recipes-gimbal/castle-gimbal/files/src/camera.c
recipes-gimbal/castle-gimbal/files/src/camera.h
recipes-gimbal/castle-gimbal/files/src/mpu6050.c
recipes-gimbal/castle-gimbal/files/src/mpu6050.h
recipes-gimbal/castle-gimbal/files/Makefile
recipes-gimbal/castle-gimbal/files/castle-gimbal.service
recipes-gimbal/castle-gimbal/castle-gimbal-controller_1.0.bb
```

## 1. 보드 기본 설정

Raspberry Pi 3에서 필요한 하드웨어 기능을 활성화했다.

```text
I2C      : MPU6050 연결용
PWM      : BLDC 드라이버 VSP 입력용
Camera   : H264 녹화용
GPIO     : 녹화 버튼과 DIR 출력용
WiFi/SSH : 보드 접속 및 테스트용
```

PWM은 `pwm-2chan.dtbo`를 boot partition에 포함하도록 처리했고, 보드에서 다음 경로가 확인되었다.

```text
/sys/class/pwm/pwmchip0/pwm0
/sys/class/pwm/pwmchip0/pwm1
```

## 2. 영상 녹화 버튼

GPIO17을 active-low 물리 버튼으로 사용했다.

```text
GPIO17 physical pin 11
GND    physical pin 9
```

버튼이 눌리면 녹화를 시작하거나 정지한다.

녹화 저장 위치:

```text
/root/recordings/gimbal-YYYYMMDD-HHMMSS.h264
```

저장 공간 부족 시 녹화를 거부하도록 `statvfs()` 기반 여유 공간 검사를 추가했다.

## 3. Camera H264 Recording

`camera.c`에서 `v4l2-ctl`을 실행해 H264 파일을 생성한다.

주요 설정:

```text
device      : /dev/video0
resolution  : 1280x720
format      : H264
stream mode : mmap
```

카메라 테스트 결과 H264 파일 생성이 확인되었다.

## 4. MPU6050 센서 모듈

`mpu6050.c/h`를 새로 추가했다.

기능:

```text
/dev/i2c-1 open
I2C address 0x68 선택
PWR_MGMT_1 레지스터에 0x00 기록해서 센서 wake-up
가속도/온도/자이로 14바이트 연속 읽기
raw 값을 g, deg/s, C 단위로 변환
자이로 offset 보정
가속도 기반 roll/pitch 계산
```

주요 API:

```c
int mpu6050_init(void);
int mpu6050_read(struct mpu6050_data *data);
int mpu6050_calibrate_gyro(struct mpu6050_calibration *calibration,
                           int sample_count);
void mpu6050_apply_gyro_calibration(struct mpu6050_data *data,
                                    const struct mpu6050_calibration *calibration);
int mpu6050_calculate_angle(const struct mpu6050_data *data,
                            struct mpu6050_angle *angle);
void mpu6050_close(void);
```

## 5. Gimbal Control 계산

현재 제어 흐름은 다음과 같다.

```text
MPU6050 읽기
-> gyro offset 보정
-> roll/pitch 각도 계산
-> 목표 각도와 오차 계산
-> deadband 적용
-> P 제어값 계산
-> PWM duty 후보값 계산
-> 모터 방향 후보값 계산
-> DIR GPIO 적용
-> PWM duty 적용
```

현재 목표 각도:

```c
#define GIMBAL_TARGET_ROLL_DEG 0.0f
#define GIMBAL_TARGET_PITCH_DEG 0.0f
```

현재 P gain:

```c
#define GIMBAL_ROLL_KP 1.0f
#define GIMBAL_PITCH_KP 1.0f
```

deadband:

```c
#define GIMBAL_DEADBAND_DEG 1.0f
```

## 6. BLDC PWM 출력

Raspberry Pi PWM 출력 매핑:

```text
Roll  VSP -> PWM0 / GPIO18 / physical pin 12
Pitch VSP -> PWM1 / GPIO19 / physical pin 35
```

PWM period:

```text
100000 ns = 10 kHz
```

BLDC 드라이버 VSP 설명에 맞춰 PWM duty는 다음 규칙으로 변환한다.

```text
제어 출력 없음 -> 0%
제어 출력 있음 -> 최소 30%
최대 출력      -> 60%
```

현재 설정:

```c
#define GIMBAL_STOP_DUTY_PERCENT 0.0f
#define GIMBAL_MIN_RUN_DUTY_PERCENT 30.0f
#define GIMBAL_MAX_DUTY_PERCENT 60.0f
```

## 7. BLDC DIR 출력

Raspberry Pi GPIO 출력 매핑:

```text
Roll  DIR -> GPIO20 / physical pin 38
Pitch DIR -> GPIO21 / physical pin 40
```

TXS0108E를 통해 3.3V GPIO 신호를 5V 제어 신호로 변환한다.

```text
GPIO20 -> TXS0108E A3 -> B3 -> Roll BLDC DIR
GPIO21 -> TXS0108E A4 -> B4 -> Pitch BLDC DIR
```

방향 반전 설정:

```c
#define GIMBAL_ROLL_DIRECTION_INVERT 0
#define GIMBAL_PITCH_DIRECTION_INVERT 0
```

실제 모터가 기울기를 줄이는 방향이 아니라 키우는 방향으로 돌면 해당 축의 invert 값을 `1`로 변경한다.

## 8. Systemd 서비스

서비스 파일:

```text
recipes-gimbal/castle-gimbal/files/castle-gimbal.service
```

설치 위치:

```text
/usr/lib/systemd/system/castle-gimbal.service
```

자동 실행:

```bitbake
SYSTEMD_AUTO_ENABLE:${PN} = "enable"
```

서비스 종료 후 PWM을 강제로 끄는 이중 안전장치를 추가했다.

```ini
ExecStopPost=/bin/sh -c 'echo 0 > /sys/class/pwm/pwmchip0/pwm0/enable'
ExecStopPost=/bin/sh -c 'echo 0 > /sys/class/pwm/pwmchip0/pwm1/enable'
```

## 9. 문서 산출물

추가된 문서:

```text
docs/wiring/raspberry-pi3-txs0108e-bldc-control-wiring.svg
docs/wiring/bldc-motor-preflight-checklist.xlsx
docs/wiring/motor-direction-test.md
```

## 10. RTSP Viewing and Recording

RTSP stream address:

```text
rtsp://192.168.121.129:8554/gimbal
```

Verified stream:

```text
H264
1280x720
30fps
about 3.5 seconds delay in the current setup
```

PC recording script:

```text
tools/record-rtsp.sh
```

The script records the RTSP stream to an MP4 file with timestamped file name.

Example:

```sh
tools/record-rtsp.sh rtsp://192.168.121.129:8554/gimbal ~/Videos
```

## 11. Git 반영

MPU6050 기반 제어 기능은 GitHub에 push 완료되었다.

```text
Repository : https://github.com/castle-007/meta-castle-gimbal.git
Commit     : 65be74f Add MPU6050 gimbal control
```

# Castle Gimbal Debugging Summary

이 문서는 개발 중 발생한 주요 문제와 해결 내용을 정리한다.

## 1. Layer 설정 오류

### 문제

```text
ERROR: BBFILE_PATTERN_castlegimbal not defined
```

### 원인

`conf/layer.conf`의 collection 이름과 `BBFILE_PATTERN_*` 이름이 맞지 않았다.

### 해결

`BBFILE_COLLECTIONS` 값을 `castlegimbal`로 맞추고, 관련 변수 이름도 같은 collection 이름을 사용하도록 수정했다.

## 2. Raspberry Pi WiFi Firmware License 오류

### 문제

```text
Nothing RPROVIDES 'linux-firmware-rpidistro-bcm43430'
restricted license 'synaptics-killswitch'
```

### 원인

Raspberry Pi WiFi firmware에 제한 license flag가 필요했다.

### 해결

`LICENSE_FLAGS_ACCEPTED`에 필요한 license flag를 허용했다.

## 3. TMPDIR 변경 오류

### 문제

```text
Error, TMPDIR has changed location
```

### 원인

빌드 폴더 또는 TMPDIR 위치가 이전 빌드와 달라졌다.

### 해결

기존 tmp를 정리하거나 새 build folder 기준으로 다시 설정했다.

## 4. PWM overlay 누락

### 문제

```text
/sys/class/pwm empty
pwmchip0 없음
find /boot/overlays | grep pwm 결과 없음
```

### 원인

`pwm-2chan.dtbo`가 boot partition에 설치되지 않았다.

### 해결

`rpi-bootfiles` 작업 디렉터리에 존재하는 `pwm-2chan.dtbo`를 `IMAGE_BOOT_FILES`를 통해 boot partition의 `overlays/`로 포함했다.

### 결과

```text
/sys/class/pwm/pwmchip0
npwm = 2
```

## 5. PWM duty write Invalid argument

### 문제

```text
cannot write /sys/class/pwm/pwmchip0/pwm0/duty_cycle: Invalid argument
```

### 원인

PWM sysfs는 `period`, `duty_cycle`, `enable` 설정 순서가 중요하다.  
enable 상태에서 period/duty를 잘못 바꾸면 오류가 날 수 있다.

### 해결

초기화 순서를 다음처럼 정리했다.

```text
export
enable 확인 후 disable
period 설정
duty_cycle 0 설정
필요할 때 duty 설정 후 enable
```

## 6. pseudo path mismatch

### 문제

```text
pseudo abort
path mismatch
```

### 원인

Yocto `tmp/work` 내부 파일과 pseudo DB 상태가 꼬였다.

### 해결

해당 recipe 작업 디렉터리를 clean 후 재빌드했다.

## 7. SSH 접속 문제

### 문제

```text
ssh: connect to host ... No route to host
Connection refused
```

### 원인

`wlan0`과 `eth0`이 같은 subnet에 잡히면서 routing이 꼬였다.  
또한 openssh는 `sshd.socket` 방식으로 동작 중이었다.

### 해결

유선 SSH 테스트 시 `wlan0`을 내리거나 routing을 정리했다.  
`sshd.socket` 상태를 확인했다.

## 8. SD card rootfs 용량 부족

### 문제

```text
not enough recording space: 72 MB free
/dev/root 약 190 MB
```

### 원인

WIC 이미지의 root partition이 SD card 전체 용량으로 확장되지 않았다.

### 해결

Host에서 partition 2를 SD card 끝까지 확장하고, Yocto native `e2fsprogs` 1.47.0으로 filesystem을 확장했다.

결과:

```text
/dev/sdg2 약 29G
```

## 9. e2fsck host version 문제

### 문제

```text
/dev/sdg2 has unsupported feature(s): FEATURE_C12
e2fsck: Get a newer version of e2fsck!
```

### 원인

Host OS의 `e2fsck` 1.46.5가 Yocto에서 만든 ext4 feature를 지원하지 않았다.

### 해결

Yocto native `e2fsprogs-native` 1.47.0을 사용했다.

## 10. libgpiod tools 없음

### 문제

```text
gpioinfo: not found
rpm: not found
```

### 해결

빌드 결과물의 `packages-split/libgpiod-tools/usr/bin/gpioinfo`, `gpioget`를 보드 `/tmp`로 복사해서 테스트했다.

## 11. libgpiod header 누락

### 문제

```text
fatal error: gpiod.h: No such file or directory
```

### 원인

Recipe의 `DEPENDS`에 `libgpiod`가 없었다.

### 해결

```bitbake
DEPENDS = "libgpiod"
RDEPENDS:${PN} += "libgpiod"
```

## 12. libgpiod v2 set_value 인자 오류

### 문제

```text
too few arguments to function 'gpiod_line_request_set_value'
```

### 원인

libgpiod v2의 `gpiod_line_request_set_value()`는 request, line offset, value를 받는다.

### 해결

`struct output_gpio`에 `line_offset`을 저장하고 set_value 호출 시 함께 넘겼다.

## 13. GPIO17 busy

### 문제

```text
cannot request GPIO17: Device or resource busy
```

### 원인

기존 `castle-gimbal.service`가 실행 중이라 GPIO17 버튼 라인을 이미 점유하고 있었다.

### 해결

테스트 전 서비스를 정지했다.

```sh
systemctl stop castle-gimbal.service
```

## 14. MPU6050 I2C read/write 실패

### 문제

```text
mpu6050: cannot write register 0x6B: Input/output error
i2cget -y 1 0x68 0x75: Read failed
i2cset -y 1 0x68 0x6B 0x00: Write failed
```

### 원인

주소 스캔에는 `0x68`이 보였지만 실제 레지스터 통신이 실패했다.  
배선 또는 전원 접촉 문제가 의심되었다.

### 해결

MPU6050 배선을 재확인했다.

```text
VCC -> 3.3V
GND -> GND
SCL -> GPIO3/SCL1 physical pin 5
SDA -> GPIO2/SDA1 physical pin 3
```

### 결과

```text
mpu6050: initialized on /dev/i2c-1 address 0x68
```

## 15. 자이로 offset 문제

### 현상

정지 상태에서도 자이로 값이 0이 아니었다.

```text
gyro x=-3.xx y=0.xx z=-0.xx deg/s
```

### 해결

시작 시 100개 샘플을 읽어 평균 offset을 계산하고, 이후 gyro 값에서 offset을 뺐다.

## 16. PWM 적용 후 바로 0으로 보이는 문제

### 문제

`apply_gimbal_pwm_output()` 호출 후 sysfs를 확인하면 모두 0으로 보였다.

### 원인

프로그램이 종료되면서 정리 코드가 PWM을 다시 껐다.

### 해결

테스트 시 프로그램 실행 중 다른 터미널에서 확인하거나 임시 `sleep()`을 넣어 적용 순간을 확인했다.

## 17. systemctl stop 후 PWM 남는 문제

### 문제

`systemctl stop castle-gimbal.service` 후 PWM enable이 남는 경우가 있었다.

### 해결

프로그램 내부에 `force_disable_pwm_channel()`을 추가하고, systemd 서비스에 `ExecStopPost`를 추가했다.

```ini
ExecStopPost=/bin/sh -c 'echo 0 > /sys/class/pwm/pwmchip0/pwm0/enable'
ExecStopPost=/bin/sh -c 'echo 0 > /sys/class/pwm/pwmchip0/pwm1/enable'
```

## 18. 방향 GPIO 적용 실패

### 문제

```text
gimbal: cannot apply roll direction
gimbal: control loop failed
```

### 원인

`run_gimbal_control_loop()`가 방향 GPIO 초기화보다 먼저 실행되고 있었다.

### 해결

초기화 순서를 변경했다.

```text
버튼 초기화
PWM 초기화
PWM 0 적용
DIR GPIO 초기화
MPU6050 초기화
gyro 보정
제어 루프 시작
```

## 19. DIR GPIO stop 후 output 유지

### 현상

서비스 stop 후 `gpioinfo`에서 GPIO20/GPIO21이 output으로 보였다.

### 확인

`gpioget GPIO20`, `gpioget GPIO21`로 inactive 상태를 확인했다.

```text
"GPIO20"=inactive
"GPIO21"=inactive
```

### 조치

`release_output_gpio()`에서 request 해제 전에 low를 먼저 쓰도록 수정했다.

## 20. GitHub token 인증 실패

### 문제

```text
remote: Invalid username or token.
Password authentication is not supported for Git operations.
```

### 해결

저장된 credential을 지우고 GitHub Personal Access Token으로 다시 push했다.


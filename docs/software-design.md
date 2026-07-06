# Castle Gimbal Controller Software Design

## 1. Purpose

`castle-gimbal`은 Raspberry Pi 3에서 실행되는 짐벌 제어 프로그램이다.

주요 목적:

```text
MPU6050으로 roll/pitch 자세 측정
BLDC 드라이버 VSP PWM 제어
BLDC 드라이버 DIR 방향 제어
GPIO17 active-low 버튼으로 영상 녹화 제어
systemd 서비스로 자동 실행
서비스 종료 시 PWM 안전 차단
```

## 2. Target Hardware

```text
Controller board : Raspberry Pi 3
IMU              : MPU6050, I2C address 0x68
Level shifter    : TXS0108E
Motor driver     : BLDC driver with VSP/DIR/BRK/FG/GND/5V control port
Camera           : V4L2 camera, /dev/video0
Record button    : GPIO17 active-low
```

## 3. Hardware Mapping

### I2C

```text
MPU6050 SDA -> Raspberry Pi GPIO2 / SDA1 / physical pin 3
MPU6050 SCL -> Raspberry Pi GPIO3 / SCL1 / physical pin 5
MPU6050 VCC -> Raspberry Pi 3.3V
MPU6050 GND -> Raspberry Pi GND
```

### PWM

```text
Roll  VSP -> Raspberry Pi GPIO18 / PWM0 / physical pin 12
Pitch VSP -> Raspberry Pi GPIO19 / PWM1 / physical pin 35
```

### Direction GPIO

```text
Roll  DIR -> Raspberry Pi GPIO20 / physical pin 38
Pitch DIR -> Raspberry Pi GPIO21 / physical pin 40
```

### TXS0108E

```text
VCCA -> Raspberry Pi 3.3V
VCCB -> Raspberry Pi 5V
OE   -> Raspberry Pi 3.3V
GND  -> Raspberry Pi GND and BLDC Control GND common

A1/B1 -> Roll VSP
A2/B2 -> Pitch VSP
A3/B3 -> Roll DIR
A4/B4 -> Pitch DIR
```

## 4. Yocto Integration

Recipe:

```text
recipes-gimbal/castle-gimbal/castle-gimbal-controller_1.0.bb
```

Build dependencies:

```bitbake
DEPENDS = "libgpiod"
RDEPENDS:${PN} += "libgpiod"
```

Source files:

```bitbake
file://Makefile
file://src/main.c
file://src/camera.c
file://src/camera.h
file://src/mpu6050.c
file://src/mpu6050.h
file://castle-gimbal.service
```

Installed binary:

```text
/usr/bin/castle-gimbal
```

Systemd:

```bitbake
inherit systemd
SYSTEMD_SERVICE:${PN} = "castle-gimbal.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"
```

## 5. Process Architecture

The program is a single native C process.

```text
main.c
  storage and recording path handling
  GPIO17 record button
  GPIO20/GPIO21 DIR output
  PWM sysfs control
  gimbal control loop

mpu6050.c
  I2C communication
  raw sensor conversion
  gyro calibration
  roll/pitch calculation

camera.c
  fork/exec v4l2-ctl
  start/stop recording
```

## 6. Startup Sequence

Expected runtime startup order:

```text
1. Print start log
2. Initialize GPIO17 record button
3. Export and configure PWM0/PWM1
4. Force PWM output to 0
5. Initialize GPIO20/GPIO21 direction outputs
6. Initialize MPU6050
7. Calibrate gyro offset with 100 samples
8. Print one sensor/control state
9. Start continuous gimbal control loop
10. Handle record button while service is running
11. On shutdown, stop recording, disable PWM, release GPIO, close MPU6050
```

Note:

현재 코드에서는 짐벌 제어 루프가 long-running 동작이다.  
종료 신호 처리 위치는 추후 `run_gimbal_control_loop()` 시작 전에 등록하는 구조로 개선하는 것이 좋다.  
현재 systemd stop 안전은 `ExecStopPost`로 보강되어 있다.

## 7. MPU6050 Module Design

### Data Structures

```c
struct mpu6050_data {
    float accel_x;
    float accel_y;
    float accel_z;
    float gyro_x;
    float gyro_y;
    float gyro_z;
    float temperature;
};

struct mpu6050_angle {
    float roll;
    float pitch;
};

struct mpu6050_calibration {
    float gyro_x_offset;
    float gyro_y_offset;
    float gyro_z_offset;
};
```

### Read Flow

```text
read 14 bytes from register 0x3B
combine high/low bytes into signed 16-bit raw values
convert accel raw / 16384.0
convert gyro raw / 131.0
convert temperature raw / 340.0 + 36.53
```

### Angle Calculation

현재 roll/pitch는 가속도 기반으로 계산한다.

```text
roll  = atan2(accel_x, sqrt(accel_y^2 + accel_z^2))
pitch = atan2(accel_y, sqrt(accel_x^2 + accel_z^2))
```

제한:

```text
빠른 움직임이나 진동에서는 가속도 기반 각도가 흔들릴 수 있다.
추후 gyro integration + complementary filter 적용이 필요하다.
```

## 8. Control Algorithm

### Constants

```c
#define GIMBAL_TARGET_ROLL_DEG 0.0f
#define GIMBAL_TARGET_PITCH_DEG 0.0f
#define GIMBAL_ROLL_KP 1.0f
#define GIMBAL_PITCH_KP 1.0f
#define GIMBAL_DEADBAND_DEG 1.0f
```

### Flow

```text
error = target - current_angle
deadband 적용
control_output = error * Kp
direction = sign(control_output)
pwm_duty = abs(control_output) converted to BLDC VSP range
```

### Deadband

작은 센서 노이즈나 미세 흔들림에 모터가 반응하지 않도록 한다.

```text
abs(error) < 1.0 degree -> control output 0
```

## 9. PWM Output Design

PWM sysfs path:

```text
/sys/class/pwm/pwmchip0/pwm0
/sys/class/pwm/pwmchip0/pwm1
```

Period:

```text
100000 ns = 10 kHz
```

Duty conversion:

```text
duty_cycle_ns = PWM_PERIOD_NS * duty_percent / 100
```

BLDC VSP range:

```text
0%       -> stop
30~60%   -> run range used by current software
```

Reason:

드라이버 설명에 따르면 VSP는 30~100% duty를 속도 제어 범위로 사용하고, 낮은 duty에서는 shutdown될 수 있다.  
초기 안전을 위해 최대 duty는 60%로 제한했다.

## 10. Direction Output Design

Direction values:

```text
+1 -> high
-1 -> low
 0 -> low
```

BLDC driver description:

```text
DIR high = CCW
DIR low  = CW
```

Direction inversion:

```c
#define GIMBAL_ROLL_DIRECTION_INVERT 0
#define GIMBAL_PITCH_DIRECTION_INVERT 0
```

If motor correction direction is reversed, set the corresponding constant to `1`.

## 11. Recording Design

Record button:

```text
GPIO17 active-low
```

Behavior:

```text
button pressed while idle      -> start recording
button pressed while recording -> stop recording
```

Storage:

```text
/root/recordings/
```

File format:

```text
gimbal-YYYYMMDD-HHMMSS.h264
```

Recording backend:

```text
v4l2-ctl /dev/video0 H264 stream
```

## 12. Safety Design

### Startup Safety

PWM channels are initialized disabled and duty 0.

```text
configure_pwm_channel()
apply_pwm_output(channel, 0.0f)
```

### Runtime Safety

Duty is clamped.

```text
minimum run duty : 30%
maximum duty     : 60%
```

### Shutdown Safety

Program-level:

```text
force_disable_pwm_channel(0)
force_disable_pwm_channel(1)
release_output_gpio() lowers DIR GPIO before release
```

Systemd-level:

```ini
ExecStopPost=/bin/sh -c 'echo 0 > /sys/class/pwm/pwmchip0/pwm0/enable'
ExecStopPost=/bin/sh -c 'echo 0 > /sys/class/pwm/pwmchip0/pwm1/enable'
```

Verified stop state:

```text
PWM0 enable = 0
PWM1 enable = 0
GPIO20 = inactive
GPIO21 = inactive
```

## 13. Logging

Control loop log interval:

```c
#define GIMBAL_LOG_INTERVAL_COUNT 25
```

With 20 ms loop interval, logs are printed about every 0.5 seconds.

Example:

```text
gimbal: angle roll=1.14 pitch=4.43 deg, pwm roll=30.00% pitch=30.00%, direction roll=-1 pitch=-1
```

## 14. Known Limitations

1. Current roll/pitch uses accelerometer-based angle calculation only.
2. No PID I/D term is implemented yet.
3. No complementary filter is implemented yet.
4. Yaw is not controlled.
5. BLDC FG speed feedback is not used yet.
6. BRK pin is not controlled yet.
7. TXS0108E may not be ideal for all PWM/load conditions; signal quality should be checked with the real wiring.
8. Signal handler registration should be reviewed so graceful shutdown is available before entering the long-running control loop.

## 15. Recommended Next Work

```text
1. Move signal registration before long-running control loop
2. Add complementary filter for accel + gyro angle estimation
3. Add PID control
4. Add per-axis Kp tuning constants
5. Add motor enable/disable mode
6. Add BRK control if driver behavior requires it
7. Add FG speed measurement
8. Add runtime configuration file for target angle and gains
9. Add service log level control
10. Add hardware test mode command-line options
```


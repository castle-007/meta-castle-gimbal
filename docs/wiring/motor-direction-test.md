# Motor Direction Test

This note explains how to decide whether the roll or pitch motor direction must be inverted in software.

## Current Direction Settings

The controller uses these constants in `main.c`.

```c
#define GIMBAL_ROLL_DIRECTION_INVERT 0
#define GIMBAL_PITCH_DIRECTION_INVERT 0
```

Meaning:

```text
0 = use current direction
1 = invert direction
```

## Test Rule

The motor must move in the direction that reduces the tilt.

```text
Correct:
  Board tilts away from target
  Motor moves to reduce that tilt

Wrong:
  Board tilts away from target
  Motor moves further in the same tilt direction
```

## Roll Axis

If the roll motor makes the roll angle worse, invert only the roll direction.

```c
#define GIMBAL_ROLL_DIRECTION_INVERT 1
#define GIMBAL_PITCH_DIRECTION_INVERT 0
```

## Pitch Axis

If the pitch motor makes the pitch angle worse, invert only the pitch direction.

```c
#define GIMBAL_ROLL_DIRECTION_INVERT 0
#define GIMBAL_PITCH_DIRECTION_INVERT 1
```

## Both Axes

If both axes move in the wrong correction direction, invert both.

```c
#define GIMBAL_ROLL_DIRECTION_INVERT 1
#define GIMBAL_PITCH_DIRECTION_INVERT 1
```

## Safety

Start without the camera payload if possible.

Use short tests first.

Stop the service immediately if the motor increases the tilt, vibrates strongly, heats up, or makes abnormal noise.

```sh
systemctl stop castle-gimbal.service
```

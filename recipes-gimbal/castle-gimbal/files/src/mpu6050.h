#ifndef CASTLE_MPU6050_H
#define CASTLE_MPU6050_H

/*
 * MPU6050에서 읽은 가속도와 자이로 값을 저장한다.
 */
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

/*
 * 센서 기준값 보정
 */
struct mpu6050_calibration {
	float gyro_x_offset;
	float gyro_y_offset;
	float gyro_z_offset;
};

/*
 * /dev/i2c-1을 열고 MPU6050을 초기화한다.
 */
int mpu6050_init(void);

/*
 * 센서의 가속도, 자이로, 온도 값을 읽는다.
 */
int mpu6050_read(struct mpu6050_data *data);

/* 보드가 정지한 상태에서 자이로 값을 여러 번 읽고 평균을 내는 함수 */
int mpu6050_calibrate_gyro(struct mpu6050_calibration *calibration, int sample_count);

/* 보정값을 실제 읽은 자이로 값에 적용하는 함수 */
void mpu6050_apply_gyro_calibration(struct mpu6050_data *data, const struct mpu6050_calibration *calibration);

/* raw 데이터를 이용해서 사람이 이해하기 쉬운 기울기 각도로 바꾸는 함수 */
int mpu6050_calculate_angle(const struct mpu6050_data *data, struct mpu6050_angle *angle);
/*
 * MPU6050과 I2C 장치를 닫는다.
 */
void mpu6050_close(void);

#endif

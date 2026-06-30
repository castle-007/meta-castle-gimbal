#include "mpu6050.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MPU6050_I2C_DEVICE "/dev/i2c-1"
#define MPU6050_I2C_ADDRESS 0x68

#define MPU6050_REG_PWR_MGMT_1 0x6B
#define MPU6050_REG_ACCEL_XOUT_H 0x3B

#define MPU6050_ACCEL_SCALE 16384.0f
#define MPU6050_GYRO_SCALE 131.0f

/*
 * MPU6050과 통신하는 I2C 파일 디스크립터다.
 * -1은 아직 장치를 열지 않았다는 뜻이다.
 */
static int i2c_fd = -1;

/*
 * MPU6050의 한 레지스터에 1바이트 값을 기록한다.
 */
static int write_register(unsigned char register_address,
			  unsigned char value)
{
	unsigned char buffer[2];

	buffer[0] = register_address;
	buffer[1] = value;

	if (write(i2c_fd, buffer, sizeof(buffer)) != sizeof(buffer)) {
		fprintf(stderr,
			"mpu6050: cannot write register 0x%02X: %s\n",
			register_address, strerror(errno));
		return -1;
	}

	return 0;
}

/*
 * MPU6050의 시작 레지스터부터 여러 바이트를 연속으로 읽는다.
 */
static int read_registers(unsigned char start_register,
			unsigned char *buffer,
			size_t length)
{
	if (write(i2c_fd, &start_register, 1) != 1) {
		fprintf(stderr, "mpu6050: cannot select register 0x%02X: %s\n",
				start_register, strerror(errno));
		return -1;
	}

	if (read(i2c_fd, buffer, length) != (ssize_t)length) {
		fprintf(stderr, "mpu6050: cannot read %zu bytes from 0x%02X: %s\n",
				length, start_register, strerror(errno));
		return -1;
	}

	return 0;
}

/*
 * MPU6050 센서값은 high byte, low byte 두 바이트로 나뉘어 있다.
 * 두 바이트를 합쳐서 signed 16비트 원시 센서값으로 만든다.
 */
static short combine_bytes(unsigned char high, unsigned char low)
{
	return (short)((high << 8) | low);
}

/*
 * MPU6050을 사용할 수 있도록 I2C 장치를 열고 센서를 깨운다.
 */
int mpu6050_init(void)
{
	i2c_fd = open(MPU6050_I2C_DEVICE, O_RDWR);
	if (i2c_fd < 0) {
		fprintf(stderr, "mpu6050: cannot open %s: %s\n",
				MPU6050_I2C_DEVICE, strerror(errno));
		return -1;
	}

	if (ioctl(i2c_fd, I2C_SLAVE, MPU6050_I2C_ADDRESS) < 0) {
		fprintf(stderr, "mpu6050: cannot select i2c address 0x%02X: %s\n",
				MPU6050_I2C_ADDRESS, strerror(errno));
		close(i2c_fd);
		i2c_fd = -1;
		return -1;
	}

	if (write_register(MPU6050_REG_PWR_MGMT_1, 0x00) < 0) {
		close(i2c_fd);
		i2c_fd = -1;
		return -1;
	}

	usleep(100000);

	printf("mpu6050: initialized on %s address 0x%02X\n",
			MPU6050_I2C_DEVICE, MPU6050_I2C_ADDRESS);

	return 0;
}

/*
 * MPU6050에서 가속도, 온도, 자이로 값을 읽어서 사람이 보기 쉬운 단위로 변환한다.
 */
int mpu6050_read(struct mpu6050_data *data)
{
	unsigned char buffer[14];

	short raw_accel_x;
	short raw_accel_y;
	short raw_accel_z;
	short raw_temperature;
	short raw_gyro_x;
	short raw_gyro_y;
	short raw_gyro_z;

	if (data == NULL) {
		fprintf(stderr, "mpu6050: data pointer is NULL\n");
		return -1;
	}

	if (i2c_fd < 0) {
		fprintf(stderr, "mpu6050: device is not initialized\n");
		return -1;
	}

	if (read_registers(MPU6050_REG_ACCEL_XOUT_H, buffer, sizeof(buffer)) < 0) {
		return -1;
	}

	raw_accel_x = combine_bytes(buffer[0], buffer[1]);
	raw_accel_y = combine_bytes(buffer[2], buffer[3]);
	raw_accel_z = combine_bytes(buffer[4], buffer[5]);
	raw_temperature = combine_bytes(buffer[6], buffer[7]);
	raw_gyro_x = combine_bytes(buffer[8], buffer[9]);
	raw_gyro_y = combine_bytes(buffer[10], buffer[11]);
	raw_gyro_z = combine_bytes(buffer[12], buffer[13]);

	data->accel_x = raw_accel_x / MPU6050_ACCEL_SCALE;
	data->accel_y = raw_accel_y / MPU6050_ACCEL_SCALE;
	data->accel_z = raw_accel_z / MPU6050_ACCEL_SCALE;

	data->temperature = raw_temperature / 340.0f + 36.53f;

	data->gyro_x = raw_gyro_x / MPU6050_GYRO_SCALE;
	data->gyro_y = raw_gyro_y / MPU6050_GYRO_SCALE;
	data->gyro_z = raw_gyro_z / MPU6050_GYRO_SCALE;

	return 0;
}

/*
 * 보드가 정지한 상태에서 자이로 값을 여러 번 읽고 평균 offset을 계산한다.
 *
 * MPU6050 자이로는 가만히 있어도 0.0 deg/s가 정확히 나오지 않는다.
 * 이 평균값을 offset으로 저장해두고, 나중에 실제 제어값에서 빼면
 * 정지 상태에서 천천히 각도가 밀리는 현상을 줄일 수 있다.
 */
int mpu6050_calibrate_gyro(struct mpu6050_calibration *calibration,
							int sample_count)
{
	struct mpu6050_data data;
	float gyro_x_sum = 0.0f;
	float gyro_y_sum = 0.0f;
	float gyro_z_sum = 0.0f;
	int i;

	if (calibration == NULL) {
		fprintf(stderr, "mpu6050: calibration data is NULL\n");
		return -1;
	}

	if (sample_count <= 0) {
		fprintf(stderr, "mpu6050: invalid calibration sample count\n");
		return -1;
	}

	for (i = 0; i < sample_count; i++) {
		if (mpu6050_read(&data) < 0)
			return -1;

		gyro_x_sum += data.gyro_x;
		gyro_y_sum += data.gyro_y;
		gyro_z_sum += data.gyro_z;

		usleep(10000);
	}

	calibration->gyro_x_offset = gyro_x_sum / sample_count;
	calibration->gyro_y_offset = gyro_y_sum / sample_count;
	calibration->gyro_z_offset = gyro_z_sum / sample_count;

	printf("mpu6050: gyro calibration x=%.2f y=%.2f z=%.2f deg/s\n",
			calibration->gyro_x_offset,
			calibration->gyro_y_offset,
			calibration->gyro_z_offset);

	return 0;
}

/*
 * 읽어온 자이로 값에서 보정 offset을 빼서 정지 상태의 오차를 줄인다.
 *
 * calibration 값은 mpu6050_calibrate_gyro()에서 계산한 평균 자이로 값이다.
 * 보드가 실제로 움직이지 않을 때 이 값만큼 출력되는 오차가 있으므로,
 * 이후 읽은 자이로 값에서 빼면 0 deg/s에 더 가까워진다.
 */
void mpu6050_apply_gyro_calibration(struct mpu6050_data *data,
			const struct mpu6050_calibration *calibration)
{
	if (data == NULL)
		return;

	if (calibration == NULL)
		return;

	data->gyro_x -= calibration->gyro_x_offset;
	data->gyro_y -= calibration->gyro_y_offset;
	data->gyro_z -= calibration->gyro_z_offset;
}

/*
 * 가속도 센서값만 이용해서 현재 보드의 roll/pitch 각도를 계산한다.
 *
 * roll  : X축 기준 좌우 기울기
 * pitch : Y축 기준 앞뒤 기울기
 *
 * MPU6050의 가속도 값은 중력 방향을 포함하므로,
 * 보드가 정지해 있거나 천천히 움직일 때는 기울기 계산에 사용할 수 있다.
 * 단, 빠르게 움직이거나 진동이 심할 때는 가속도 값에 외부 가속도가 섞여
 * 각도가 흔들릴 수 있다.
 */
int mpu6050_calculate_angle(const struct mpu6050_data *data,
			struct mpu6050_angle *angle)
{
	float accel_yz;
	float accel_xz;

	if (data == NULL) {
		fprintf(stderr, "mpu6050: angle input data is NULL\n");
		return -1;
	}

	if (angle == NULL) {
		fprintf(stderr, "mpu6050: angle output data is NULL\n");
		return -1;
	}

	/*
	 * roll 계산에서 X축을 기준으로 보려면,
	 * 나머지 Y/Z축 가속도의 합성 크기가 필요하다.
	 */
	accel_yz = sqrtf(data->accel_y * data->accel_y +
					data->accel_z * data->accel_z);

	/*
	 * pitch 계산에서 Y축을 기준으로 보려면,
	 * 나머지 X/Z축 가속도의 합성 크기가 필요하다.
	 */
	accel_xz = sqrtf(data->accel_x * data->accel_x +
					data->accel_z * data->accel_z);

	/*
	 * atan2f()는 두 값을 이용해서 각도를 안정적으로 계산한다.
	 * 결과는 라디안이므로 180 / pi 값을 곱해서 degree 단위로 바꾼다.
	 */
	angle->roll = atan2f(data->accel_x, accel_yz) * 57.2957795f;
	angle->pitch = atan2f(data->accel_y, accel_xz) * 57.2957795f;

	return 0;	
}

/*
 * MPU6050에서 사용하던 I2C 파일 디스크립터를 닫는다.
 */
void mpu6050_close(void)
{
	if (i2c_fd >= 0) {
		close(i2c_fd);
		i2c_fd = -1;
	}
}

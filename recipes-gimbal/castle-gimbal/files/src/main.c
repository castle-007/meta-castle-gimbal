#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <gpiod.h>

#include "camera.h"
#include "mpu6050.h"

#include <time.h>			// 파일생성시 현재날짜와 시간사용을 위해.
#include <sys/stat.h>		// 녹화 저장 폴더가 없을때 mkdir() 생성.
#include <sys/statvfs.h>	// statvfs()를 사용해 파일시스템의 남은 공간 확인

/* Test define value */
#define TEST_PRINT	0
/* end */
#define PWM_CHIP_PATH "/sys/class/pwm/pwmchip0"
#define PWM_PERIOD_NS 100000
#define BUTTON_GPIO_CHIP "/dev/gpiochip0"
#define BUTTON_GPIO_OFFSET 17
/* 녹화 디렉토리 지정. */
#define RECORDING_DIRECTORY	"/root/recordings"

#define MIN_RECORDING_FREE_BYTES (200ULL * 1024ULL * 1024ULL)

/* 보드가 수평이 되도록 제어하기 위해 정의 */
#define GIMBAL_TARGET_ROLL_DEG 0.0f
#define GIMBAL_TARGET_PITCH_DEG 0.0f

/* 모터가 갑자기 크게 움직이지 않게 */
#define GIMBAL_ROLL_KP 1.0f
#define GIMBAL_PITCH_KP 1.0f

/* 모터가 갑자기 세게 움직이는 것을 방지하기 위해 PWM duty 값을 30%로 제한 */
#define	GIMBAL_STOP_DUTY_PERCENT	0.0f
#define GIMBAL_MIN_RUN_DUTY_PERCENT 30.0f
#define GIMBAL_MAX_DUTY_PERCENT		60.0f

/* gimbal motor 떨림 방지. */
#define GIMBAL_DEADBAND_DEG 1.0f

/* 동작확인을 위한 테스트 값 */
#define GIMBAL_PWM_TEST_DUTY_PERCENT 5.0f

/* 짧은 제어 루프로 5초 동안 반복 
 * 장시간 자동 제어가 아닌 테스트용 
*/
#define GIMBAL_CONTROL_INTERVAL_US 20000
#define GIMBAL_CONTROL_TEST_COUNT 250

/* 출력 로그를 너무 많이 찍지 않도록 줄이는 것 */
#define GIMBAL_LOG_INTERVAL_COUNT 25

/* PWM 출력 GPIO */
#define GIMBAL_ROLL_DIR_GPIO 20
#define GIMBAL_PITCH_DIR_GPIO 21

#define GIMBAL_DIRECTION_TEST_DELAY_US 1000000

/* 모터 축별 방향 0: 현재 방향, 1: 방향 반전 */
#define	GIMBAL_ROLL_DIRECTION_INVERT	0
#define	GIMBAL_PITCH_DIRECTION_INVERT	0

/*
 * 종료 신호 기록.
 * signal 함수에서 변경되므로 sig_atomic_t 형식을 사용.
 */
static volatile sig_atomic_t stop_requested;

/*
 * 영상 녹화 버튼 제어 시 필요한 GPIO 객체 보관
 */
struct button_gpio {
	struct gpiod_chip *chip;
	struct gpiod_line_request *request;
};

/* PWM 출력 GPIO 구조체 */
struct output_gpio {
	struct gpiod_chip *chip;
	struct gpiod_line_request *request;
	unsigned int line_offset;
};

struct gimbal_error {
	float roll;
	float pitch;
};

/* control output = error * KP */
struct gimbal_control_output {
	float roll;
	float pitch;
};

/*
 * PWM duty 값으로 제어 출력값 변경하는 구조체
 */
struct gimbal_pwm_output {
	float roll_duty_percent;
	float pitch_duty_percent;
};

/*
 * 모터 방향 구조체.
 * 1: 정방향, -1: 역방향, 0:정지 또는 방향 없음.
 */
struct gimbal_motor_direction {
	int roll;
	int pitch;
};
/*
 * 녹화 파일을 저장할 디렉터리가 없으면 생성한다.
 */
static int prepare_recording_directory(void)
{
	if (mkdir(RECORDING_DIRECTORY, 0755) < 0 && errno != EEXIST) {
		fprintf(stderr, "castle-gimbal: cannot create %s: %s\n",
				RECORDING_DIRECTORY, strerror(errno));
		return -1;
	}

	return 0;
}

/*
 * 현재 날짜와 시간을 사용해 녹화 파일 경로를 만든다.
 * 
 * 예: /home/root/recordings/gimbal-20260625-143015.h264
 */
static int make_recording_path(char *path, size_t path_size)
{
	time_t current_time;
	struct tm local_time;

	current_time = time(NULL);
	if (current_time == (time_t)-1) {
		fprintf(stderr, "castle-gimbal: cannot get current time\n");
		return -1;
	}

	if (localtime_r(&current_time, &local_time) == NULL) {
		fprintf(stderr, "castle-gimbal: cannot convert current time\n");
		return -1;
	}

	if (strftime(path, path_size,
				RECORDING_DIRECTORY "/gimbal-%Y%m%d-%H%M%S.h264",
				&local_time) == 0) {
		fprintf(stderr, "castle-gimbal: recording path is too long\n");
		return -1;
	}

	return 0;
}

/*
 * 녹화 디렉터리가 있는 파일시스템의 남은 공간을 확인한다.
 * 반환값:
 *  1: 녹화 가능한 공간이 있음
 *  0: 남은 공간이 부족함
 * -1: 파일시스템 정보 확인 실패
 */
static int has_recording_space(void)
{
	struct statvfs filesystem;
	unsigned long long available_bytes;

	if (statvfs(RECORDING_DIRECTORY, &filesystem) < 0) {
		fprintf(stderr, "castle-gimbal: cannot check free space: %s\n",
				strerror(errno));
		return -1;
	}

	/*
	 * 일반 사용자가 실제로 사용할 수 있는 블록 수와
	 * 블록 크기를 곱해 남은 바이트 수를 계산한다.
	 */
	available_bytes = 
		(unsigned long long)filesystem.f_bavail *
		(unsigned long long)filesystem.f_frsize;

	if (available_bytes < MIN_RECORDING_FREE_BYTES) {
		fprintf(stderr, "castle-gimbal: not enough recording space: %llu MB free\n",
				available_bytes / (1024ULL * 1024ULL));
		return 0;
	}

	return 1;
}

/*
 * GPIO 17 을 내부 풀업이 적용된 Active-Low 버튼 입력으로 초기화.
 */
static int init_record_button(struct button_gpio *button)
{
	struct gpiod_line_settings *settings;
	struct gpiod_line_config *line_config;
	struct gpiod_request_config *request_config;
	unsigned int offset = BUTTON_GPIO_OFFSET;

	/* 오류 처리 시 안전하도록 포인터를 초기화한다. */
	button->chip = NULL;
	button->request = NULL;

	/* Raspberry Pi의 GPIO 칩 장치를 연다. */
	button->chip = gpiod_chip_open(BUTTON_GPIO_CHIP);
	if (button->chip == NULL) {
		fprintf(stderr, "castle-gimbal: cannot open %s: %s\n",
				BUTTON_GPIO_CHIP, strerror(errno));
		return -1;
	}

	settings = gpiod_line_settings_new();
	line_config = gpiod_line_config_new();
	request_config = gpiod_request_config_new();

	if (settings == NULL || line_config == NULL ||
		request_config == NULL) {
		fprintf(stderr, "castle-gimbal: cannot create GPIO configuration\n");
		goto error;
	}

	/* GPIO17을 입력으로 설정한다. */
	if (gpiod_line_settings_set_direction(
				settings, GPIOD_LINE_DIRECTION_INPUT) < 0)
		goto error;

	/* 버튼을 누르지 않았을 때 HIGH가 되도록 내부 풀업을 사용한다. */
	if (gpiod_line_settings_set_bias(
				settings, GPIOD_LINE_BIAS_PULL_UP) < 0)
		goto error;

	/* 물리적으로 LOW일 때 버튼이 눌린 상태로 해석한다. */
	gpiod_line_settings_set_active_low(settings, true);

	/* 위 설정을 GPIO17에 적용한다. */
	if (gpiod_line_config_add_line_settings(
				line_config, &offset, 1, settings) < 0)
		goto error;

	/* gpioinfo에서 사용 목적을 확인할 수 있도록 이름을 붙인다. */
	gpiod_request_config_set_consumer(
				request_config, "castle-gimbal-record-button");

	/* GPIO17의 사용 권한을 요청한다. */
	button->request = gpiod_chip_request_lines(
				button->chip, request_config, line_config);
	if (button->request == NULL) {
		fprintf(stderr, "castle-gimbal: cannot request GPIO17: %s\n",
				strerror(errno));
		goto error;
	}

	gpiod_request_config_free(request_config);
	gpiod_line_config_free(line_config);
	gpiod_line_settings_free(settings);

	printf("castle-gimbal: record button initialized on GPIO17\n");
	return 0;

error:
	gpiod_request_config_free(request_config);
	gpiod_line_config_free(line_config);
	gpiod_line_settings_free(settings);

	if (button->chip != NULL) {
		gpiod_chip_close(button->chip);
		button->chip = NULL;
	}
	
	return -1;
}

/*
 * 출력 GPIO를 초기화
 * direction GPIO는 BLDC 드라이버의 DIR 입력에 연결
 * 초기값은 0
 */
static int init_output_gpio(struct output_gpio *gpio,
							unsigned int line_offset,
							const char *consumer)
{
	struct gpiod_line_settings *settings;
	struct gpiod_line_config *line_config;
	struct gpiod_request_config *request_config;
	int ret = -1;

	if (gpio == NULL)
		return -1;

	gpio->chip = gpiod_chip_open("/dev/gpiochip0");
	if (gpio->chip == NULL) {
		fprintf(stderr, "gimbal: cannot open gpiochip0\n");
		return -1;
	}

	settings = gpiod_line_settings_new();
	if (settings == NULL)
		goto fail_close_chip;

	gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
	gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);

	line_config = gpiod_line_config_new();
	if (line_config == NULL)
		goto fail_free_settings;

	if (gpiod_line_config_add_line_settings(line_config, &line_offset,
											1, settings) < 0)
		goto fail_free_line_config;

	request_config = gpiod_request_config_new();
	if (request_config == NULL)
		goto fail_free_line_config;

	gpiod_request_config_set_consumer(request_config, consumer);

	gpio->request = gpiod_chip_request_lines(gpio->chip, request_config, line_config);
	if (gpio->request == NULL)
		goto fail_free_request_config;

	gpio->line_offset = line_offset;
	ret = 0;

fail_free_request_config:
	gpiod_request_config_free(request_config);
fail_free_line_config:
	gpiod_line_config_free(line_config);
fail_free_settings:
	gpiod_line_settings_free(settings);
fail_close_chip:
	if (ret < 0) {
		gpiod_chip_close(gpio->chip);
		gpio->chip = NULL;
	}

	return ret;
}

/*
 * 출력 GPIO 값을 설정한다.
 * value가 0이면 low, 0이 아니면 high로 출력
 * TXS0108E를 거친 뒤 BLDC 드라이버 DIR 핀에는 0V 또는 5V로 전달
 */
static int set_output_gpio(struct output_gpio *gpio, int value)
{
	enum gpiod_line_value line_value;

	if (gpio == NULL)	return -1;
	if (gpio->request == NULL)	return -1;

	if (value)
		line_value = GPIOD_LINE_VALUE_ACTIVE;
	else
		line_value = GPIOD_LINE_VALUE_INACTIVE;

	if (gpiod_line_request_set_value(gpio->request, gpio->line_offset, line_value) < 0) {
		fprintf(stderr, "gimbal: cannot set output gpio%u to %d: %s\n",
				gpio->line_offset,
				value ? 1:0,
				strerror(errno));
		return -1;
	}

	return 0;
}

/*
 * 출력 GPIO 요청과 chip을 해제한다.
 * 프로그램 종료 시 호출해서 GPIO 자원을 반환한다.
 */
static void release_output_gpio(struct output_gpio *gpio)
{
	if (gpio == NULL)	return;

	if (gpio->request != NULL) {
		set_output_gpio(gpio, 0);
		gpiod_line_request_release(gpio->request);
		gpio->request = NULL;
	}

	if (gpio->chip != NULL) {
		gpiod_chip_close(gpio->chip);
		gpio->chip = NULL;
	}
}

/*
 * 영상 녹화 버튼의 현재 상태 확인.
 * 반환값:
 * 1: 버튼 눌림,  0: 버튼 해제, -1: 읽기 오류
 */
static int read_record_button(struct button_gpio *button)
{
	enum gpiod_line_value value;

	value = gpiod_line_request_get_value(
			button->request, BUTTON_GPIO_OFFSET);

	if (value == GPIOD_LINE_VALUE_ERROR) {
		fprintf(stderr, "castle-gimbal: connot read record button: %s\n",
			strerror(errno));
		return -1;
	}

	if (value == GPIOD_LINE_VALUE_ACTIVE)
		return 1;

	return 0;
}

/*
 * 영상 녹화 버튼에 사용한 GPIO 자원을 해제한다.
 * 프로그램 종료 전에 호출해야 다른 프로그램이 GPIO17을 사용할 수 있다.
 */
static void close_record_button(struct button_gpio *button)
{
	if (button->request != NULL) {
		gpiod_line_request_release(button->request);
		button->request = NULL;
	}

	if (button->chip != NULL) {
		gpiod_chip_close(button->chip);
		button->chip = NULL;
	}
}

/*
 * Ctrl+C 또는 종료 요청을 받으면 종료 상태만 기록.
 * 실제 PWM 정지는 main 함수에서 처리.
 */
static void handle_stop_signal(int signal_number)
{
	(void)signal_number;
	stop_requested = 1;
}

/*
 * 지정한 sysfs 파일을 열어 문자열을 기록한다.
 * PWM의 export, period, duty_cycle, enable 값을 설정할 때 사용한다.
 */
static int write_text_file(const char *path, const char *text)
{
	int fd;
	ssize_t text_length;
	ssize_t written;

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "castle-gimbal: cannot open %s: %s\n",
			path, strerror(errno));
		return -1;
	}

	text_length = (ssize_t)strlen(text);
	written = write(fd, text, (size_t)text_length);
	if (written != text_length) {
		fprintf(stderr, "castle-gimbal: cannot write %s: %s\n",
			path, strerror(errno));
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}

/*
 * sysfs 파일에 저장된 정수값을 읽는다.
 * PWM이 현재 활성화되어 있는지 확인할 때 사용.
 */
static int read_int_file(const char *path, int *value)
{
	FILE *file;

    file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "castle-gimbal: cannot open %s:%s\n", path, strerror(errno));
        return -1;
    }

    if (fscanf(file, "%d", value) != 1) {
        fprintf(stderr, "castle-gimbal: cannot read %s\n", path);
        fclose(file);
        return -1;
    }

    fclose(file);
    return 0;	
}

/*
 * 커널이 PWM 채널의 sysfs 디렉터리를 생성할 때까지 기다린다.
 * 최대 1초 동안 10ms 간격으로 경로가 생성됐는지 확인한다.
 */
static int wait_for_path(const char *path)
{
	int retry;

	for (retry = 0; retry < 100; retry++) {
		if (access(path, F_OK) == 0)
			return 0;

		usleep(10000);
	}

	fprintf(stderr, "castle-gimbal: timeout waiting for %s\n", path);
	return -1;
}

/*
 * 지정한 PWM 채널을 사용자 공간에서 사용할 수 있도록 export한다.
 * 이미 export된 채널이면 다시 export하지 않는다.
 */
static int export_pwm_channel(unsigned int channel)
{
	char channel_path[128];
	char channel_text[16];

	snprintf(channel_path, sizeof(channel_path), "%s/pwm%u",
		 PWM_CHIP_PATH, channel);

	if (access(channel_path, F_OK) == 0)
		return 0;

	snprintf(channel_text, sizeof(channel_text), "%u", channel);
	if (write_text_file(PWM_CHIP_PATH "/export", channel_text) < 0)
		return -1;

	return wait_for_path(channel_path);
}

/*
 * PWM 채널을 안전한 초기 상태로 설정한다.
 * 출력을 끈 다음 듀티비를 0%로 만들고 주파수를 10kHz로 설정한다.
 * 이 함수에서는 PWM 출력을 활성화하지 않는다.
 */
static int configure_pwm_channel(unsigned int channel)
{
	char path[160];
	char period_text[32];

	if (export_pwm_channel(channel) < 0)
		return -1;

	int enabled;

	snprintf(path, sizeof(path), "%s/pwm%u/enable",
		 PWM_CHIP_PATH, channel);

	if (read_int_file(path, &enabled) < 0)
		return -1;

	if (enabled != 0) {
		if (write_text_file(path, "0") < 0)
			return -1;
	}

	/* PWM 주기를 먼저 100000ns(10kHz)로 설정한다. */
	snprintf(period_text, sizeof(period_text), "%d", PWM_PERIOD_NS);
	snprintf(path, sizeof(path), "%s/pwm%u/period",
		 PWM_CHIP_PATH, channel);
	if (write_text_file(path, period_text) < 0)
		return -1;

	/* 설정된 주기 안에서 듀티 시간을 0ns로 설정한다. */
	snprintf(path, sizeof(path), "%s/pwm%u/duty_cycle",
		 PWM_CHIP_PATH, channel);
	if (write_text_file(path, "0") < 0)
		return -1;

	printf("castle-gimbal: PWM%u initialized: 10000 Hz, duty 0%%, disabled\n",
	       channel);
	return 0;
}

/*
 * 지정한 PWM 채널을 원하는 듀티비로 활성화한다.
 * period 설정이 완료된 이후에 호출해야 한다.
 */
static int enable_pwm_channel(unsigned int channel,
			      unsigned int duty_percent)
{
	char path[160];
	char duty_text[32];
	unsigned int duty_ns;

	if (duty_percent > 100) {
		fprintf(stderr,
			"castle-gimbal: invalid duty percent: %u\n",
			duty_percent);
		return -1;
	}

	/* 100000ns 주기에서 HIGH로 유지할 시간을 계산한다. */
	duty_ns = (PWM_PERIOD_NS * duty_percent) / 100;

	snprintf(duty_text, sizeof(duty_text), "%u", duty_ns);
	snprintf(path, sizeof(path), "%s/pwm%u/duty_cycle",
		 PWM_CHIP_PATH, channel);

	if (write_text_file(path, duty_text) < 0)
		return -1;

	/* 듀티 시간 설정이 끝난 후 PWM 출력을 활성화한다. */
	snprintf(path, sizeof(path), "%s/pwm%u/enable",
		 PWM_CHIP_PATH, channel);

	if (write_text_file(path, "1") < 0)
		return -1;

	printf("castle-gimbal: PWM%u enabled: duty %u%%\n",
	       channel, duty_percent);

	return 0;
}

static int disable_pwm_channel(unsigned int channel)
{
	char path[160];
	int enabled;

	/* 해당 채널의 enable 파일 경로 생성 */
	snprintf(path, sizeof(path), "%s/pwm%u/enable",
			PWM_CHIP_PATH, channel);

	/* 현재 PWM 활성화 상태 확인 */
	if (read_int_file(path, &enabled) < 0)
		return -1;

	/* 이미 비활성화 되어있으면 종료 */
	if (enabled == 0)
		return 0;

	/* 활성화된 PWM 출력은 끈다. */
	if (write_text_file(path, "0") < 0)
		return -1;

	printf("castle-gimbal: PWM%u disabled\n", channel);
	return 0;
}

/*
 * 종료 처리에서 사용하는 강제 PWM disable 함수다.
 * 현재 enable 값을 읽지 않고 바로 0을 쓴다.
 * 종료 시점에는 PWM이 켜져 있든 꺼져 있든 무조건 안전하게 끄는 것이 목적이다.
 */
static int force_disable_pwm_channel(unsigned int channel)
{
	char path[160];

	snprintf(path, sizeof(path), "%s/pwm%u/enable", PWM_CHIP_PATH, channel);

	if (write_text_file(path, "0") < 0)
		return -1;

	printf("castle-gimbal: PWM%u force disabled\n", channel);
	return 0;
}

/*
 * 목표 각도와 현재 각도의 차이를 계산한다.
 *
 * error = target - current
 *
 * error가 양수이면 목표보다 현재 각도가 작다는 뜻이고,
 * error가 음수이면 목표보다 현재 각도가 크다는 뜻이다.
 * 이 값은 나중에 모터 방향과 PWM duty를 결정하는 기준이 된다.
 */
static void calculate_gimbal_error(const struct mpu6050_angle *current_angle, struct gimbal_error *error)
{
	if (current_angle == NULL)
		return;

	if (error == NULL)
		return;

	error->roll = GIMBAL_TARGET_ROLL_DEG - current_angle->roll;
	error->pitch = GIMBAL_TARGET_PITCH_DEG - current_angle->pitch;
}

/*
 * 각도 오차가 deadband 범위 안이면 0을 반환.
 * 센서 노이즈나 아주 작은 떨림 때문에 모터가 계속 반응하면 짐벌이 떨릴 수 있으므로, 작은 오차는 무시.
 */
static float apply_deadband(float value, float deadband)
{
	if (value > -deadband && value < deadband )
		return 0.0f;

	return value;
}

/*
 * 각도 오차에 비례 게인(Kp)을 곱해서 P 제어 출력값을 계산한다.
 * P 제어는 가장 단순한 제어 방식이다.
 * 현재는 모터에 바로 적용하지 않고 로그로만 확인한다.
 * output 값이 양수/음수인지에 따라 모터 방향을 정할 수 있고,
 * output 값의 크기에 따라 PWM duty를 정할 수 있다.
 * 제어 순서:
 * 센서 읽기 -> 현재 각도 계산 -> 목표와 오차 계산 -> 제어 출력 계산 -> 모터 출력
 */
static void calculate_gimbal_control_output( const struct gimbal_error *error, struct gimbal_control_output *output)
{
	if (error == NULL)
		return;

	if (output == NULL)
		return;

	output->roll = apply_deadband(error->roll, GIMBAL_DEADBAND_DEG) * GIMBAL_ROLL_KP;
	output->pitch = apply_deadband(error->pitch, GIMBAL_DEADBAND_DEG) * GIMBAL_PITCH_KP;
}

/*
 * value가 min_value보다 작으면 min_value를 반환하고,
 * max_value보다 크면 max_value를 반환한다.
 */
static float clamp_float(float value, float min_value, float max_value)
{
	if (value < min_value)
		return min_value;

	if (value > max_value)
		return max_value;

	return value;
}

/*
 * P 제어 출력값을 PWM duty 후보값으로 변환한다.
 * 현재 단계에서는 모터 방향 제어를 아직 하지 않는다.
 * 그래서 출력값의 부호는 방향 정보로 남겨두지 않고, 절댓값 크기만 duty percent로 사용한다.
 * example:
 * control pitch = -14.0 이면 pitch duty = 14.0%
 * control pitch =  45.0 이면 pitch duty = 30.0%로 제한
 * 양수/음수: 모터 방향, 크기: 모터 세기
 */
static void calculate_gimbal_pwm_output (
	const struct gimbal_control_output *control_output,
	struct gimbal_pwm_output *pwm_output)
{
	float roll_duty;
	float pitch_duty;

	if (control_output == NULL)
		return;
	if (pwm_output == NULL)
		return;

	roll_duty = control_output->roll;
	if (roll_duty < 0.0f)
		roll_duty = -roll_duty;

	pitch_duty = control_output->pitch;
	if (pitch_duty < 0.0f)
		pitch_duty = -pitch_duty;

	if (roll_duty <= 0.0f) {
		pwm_output->roll_duty_percent = GIMBAL_STOP_DUTY_PERCENT;
	} else {
		pwm_output->roll_duty_percent = clamp_float(roll_duty,
													GIMBAL_MIN_RUN_DUTY_PERCENT,
													GIMBAL_MAX_DUTY_PERCENT);
	}

	if (pitch_duty <= 0.0f) {
		pwm_output->pitch_duty_percent = GIMBAL_STOP_DUTY_PERCENT;
	} else {
		pwm_output->pitch_duty_percent = clamp_float(pitch_duty,
													 GIMBAL_MIN_RUN_DUTY_PERCENT,
													 GIMBAL_MAX_DUTY_PERCENT);
	}
}

/*
 * P 제어 출력값의 부호를 보고 모터 방향 후보값을 계산한다.
 *
 * +1은 정방향, -1은 역방향, 0은 정지를 의미한다. - 아직 적용하지 않고 로고만 확인
 */
static void calculate_gimbal_motor_direction(
	const struct gimbal_control_output *control_output,
	struct gimbal_motor_direction *direction)
{
	if (control_output == NULL)	return;
	if (direction == NULL)	return;

	if (control_output->roll > 0.0f)
		direction->roll = 1;
	else if (control_output->roll < 0.0f)
		direction->roll = -1;
	else
		direction->roll = 0;

	if (control_output->pitch > 0.0f)
		direction->pitch = 1;
	else if (control_output->pitch < 0.0f)
		direction->pitch = -1;
	else
		direction->pitch = 0;
}

/*
 * 축별 방향 반전 설정을 적용한다.
 * 실제 모터 연결 후 보정 방향이 반대로 움직이면
 * GIMBAL_ROLL_DIRECTION_INVERT 또는 GIMBAL_PITCH_DIRECTION_INVERT를 1로 바꾼다.
 */
static void apply_gimbal_direction_invert(
	struct gimbal_motor_direction *direction)
{
	if (direction == NULL)	return;

	if (GIMBAL_ROLL_DIRECTION_INVERT)
		direction->roll = -direction->roll;
	if (GIMBAL_PITCH_DIRECTION_INVERT)
		direction->pitch = -direction->pitch;
}

/*
 * 계산된 방향값을 실제 DIR GPIO에 적용한다.
 * direction 값이 1이면 GPIO high, -1 또는 0이면 GPIO low로 둔다.
 * BLDC 드라이버 기준: DIR high(5V) = CCW, DIR low(0V)  = CW
 */
static int apply_gimbal_motor_direction(
	struct output_gpio *roll_dir_gpio,
	struct output_gpio *pitch_dir_gpio,
	const struct gimbal_motor_direction *direction)
{
	if (direction == NULL)	 return -1;

	if (set_output_gpio(roll_dir_gpio, direction->roll > 0) < 0) {
		fprintf(stderr, "gimbal: cannot apply roll direction\n");
		return -1;
	}
	if (set_output_gpio(pitch_dir_gpio, direction->pitch > 0) < 0) {
		fprintf(stderr, "gimbal: cannot apply pitch direction\n");
		return -1;
	}

	return 0;
}

/*
 */
static int test_direction_gpio_once(struct output_gpio *roll_dir_gpio,
									struct output_gpio *pitch_dir_gpio)
{
	printf("gimbal: direction test low\n");

	if (set_output_gpio(roll_dir_gpio, 0) < 0)	return -1;
	if (set_output_gpio(pitch_dir_gpio, 0) < 0)	return -1;
	usleep(GIMBAL_DIRECTION_TEST_DELAY_US);

	printf("gimbal: direction test high\n");

	if (set_output_gpio(roll_dir_gpio, 1) < 0)	return -1;
	if (set_output_gpio(pitch_dir_gpio, 1) < 0)	return -1;
	usleep(GIMBAL_DIRECTION_TEST_DELAY_US);
	
	printf("gimbal: direction test low\n");

	if (set_output_gpio(roll_dir_gpio, 0) < 0)	return -1;
	if (set_output_gpio(pitch_dir_gpio, 0) < 0)	return -1;

	return 0;
}

/*
 * percent 단위의 PWM duty를 sysfs PWM duty_cycle 값(ns)으로 변환한다.
 *
 * PWM sysfs의 duty_cycle은 percent가 아니라 ns 단위이다.
 * 예를 들어 period가 100000ns이고 duty가 30%이면:
 * duty_cycle = 100000 * 30 / 100 = 30000ns
 */
static int duty_percent_to_ns(float duty_percent)
{
	return (int)((PWM_PERIOD_NS * duty_percent) / 100.0f);
}

/*
 * 지정한 PWM 채널에 duty percent를 적용한다.
 *
 * channel 0은 pwm0, channel 1은 pwm1을 의미한다.
 * duty_percent는 사람이 보기 쉬운 percent 값이고,
 * 실제 sysfs에는 ns 단위 duty_cycle 값으로 변환해서 쓴다.
 */
static int set_pwm_duty_percent(unsigned int channel, float duty_percent)
{
	char path[160];
	char duty_text[32];
	int duty_ns;

	duty_percent = clamp_float(duty_percent,
								GIMBAL_STOP_DUTY_PERCENT,
								GIMBAL_MAX_DUTY_PERCENT);

	duty_ns = duty_percent_to_ns(duty_percent);

	snprintf(duty_text, sizeof(duty_text), "%d", duty_ns);
	snprintf(path, sizeof(path), "%s/pwm%u/duty_cycle", PWM_CHIP_PATH, channel);

	if (write_text_file(path, duty_text) < 0)
		return -1;

	return 0;
}

/*
 * duty percent 값에 따라 PWM 채널을 켜거나 끈다.
 * duty가 0% 이하이면 PWM을 disable한다.
 * duty가 0%보다 크면 duty_cycle을 먼저 설정하고 enable한다.
 *
 * duty_cycle을 먼저 설정하는 이유는 enable한 뒤 갑자기 이전 duty 값이 출력되는 상황을 피하기 위해서.
 */
static int apply_pwm_output(unsigned int channel, float duty_percent)
{
	char path[160];

	if (duty_percent <= 0.0f) {
		snprintf(path, sizeof(path), "%s/pwm%u/enable", PWM_CHIP_PATH, channel);

		if (write_text_file(path, "0") < 0)
			return -1;

		return 0;
	}

	if (set_pwm_duty_percent(channel, duty_percent) < 0)
		return -1;

	snprintf(path, sizeof(path), "%s/pwm%u/enable", PWM_CHIP_PATH, channel);

	if (write_text_file(path, "1") < 0)
		return -1;

	return 0;
}

/*
 * 계산된 짐벌 PWM duty를 실제 PWM 채널에 적용한다.
 * 현재 매핑:
 * roll  -> PWM0 , pitch -> PWM1
 * 지금은 duty 크기만 PWM에 반영하는 단계다.
 */
static int apply_gimbal_pwm_output(const struct gimbal_pwm_output *pwm_output)
{
	if (pwm_output == NULL)
		return -1;
//	printf("gimbal: applying pwm roll=%.2f%% pitch=%.2f%%\n", pwm_output->roll_duty_percent, pwm_output->pitch_duty_percent);

	if (apply_pwm_output(0, pwm_output->roll_duty_percent) < 0)	{
		fprintf(stderr, "gimbal: failed to apply roll PWM0\n");
		return -1;
	}

	if (apply_pwm_output(1, pwm_output->pitch_duty_percent) < 0) {
		fprintf(stderr, "gimbal: failed to apply pitch PWM0\n");
		return -1;
	}

	return 0;
}

/*
 * PWM 출력 경로가 정상인지 확인하기 위한 낮은 duty 테스트 함수다.
 * 자동 제어 루프에 들어가기 전에 pwm0/pwm1에 아주 낮은 duty를 한 번 써서
 * sysfs PWM 제어가 정상 동작하는지 확인한다.
 * 자동 호출은 하지 않는다.
 */
static int test_pwm_low_duty_once(void)
{
	if (set_pwm_duty_percent(0, GIMBAL_PWM_TEST_DUTY_PERCENT) < 0)
		return -1;

	if (set_pwm_duty_percent(1, GIMBAL_PWM_TEST_DUTY_PERCENT) <0)
		return -1;

	printf("castle-gimbal: PWM test duty %.1f%% written to PWM0/PWM1\n", GIMBAL_PWM_TEST_DUTY_PERCENT);
	return 0;
}

/*
 * MPU6050 센서값을 한 번 읽고 roll/pitch 각도를 출력한다.
 *
 * 현재 단계에서는 제어 계산 전에 센서값이 정상적으로 변하는지 확인하기 위한
 * 테스트 함수다. 나중에 모터 제어 루프에서 이 값을 사용하게 된다.
 */
static int print_mpu6050_once(const struct mpu6050_calibration *calibration)
{
    struct mpu6050_data sensor_data;
    struct mpu6050_angle sensor_angle;
	struct gimbal_error error;
	struct gimbal_control_output control_output;
	struct gimbal_pwm_output pwm_output;
	struct gimbal_motor_direction direction;

    if (mpu6050_read(&sensor_data) < 0)
        return -1;
	/* mpu6050_read()는 원본 자이로 값을 읽은 후 아래 함수에서 offset을 빼서 보정된 자이로 값으로 변경.*/
	mpu6050_apply_gyro_calibration(&sensor_data, calibration);

    printf("mpu6050: accel x=%.2f y=%.2f z=%.2f g\n",
           sensor_data.accel_x,
           sensor_data.accel_y,
           sensor_data.accel_z);

    printf("mpu6050: gyro x=%.2f y=%.2f z=%.2f deg/s\n",
           sensor_data.gyro_x,
           sensor_data.gyro_y,
           sensor_data.gyro_z);

    printf("mpu6050: temperature %.2f C\n",
           sensor_data.temperature);

    if (mpu6050_calculate_angle(&sensor_data, &sensor_angle) < 0)
        return -1;

    printf("mpu6050: angle roll=%.2f pitch=%.2f deg\n",
           sensor_angle.roll,
           sensor_angle.pitch);

	/* gimbal error 값 control 값 pwm 값을 비교할 수 있다.
	 * motor 회전 방향 확인.
	 * KP 값이 1.0f 면 error 과 control 값은 같게 나와야 함.
	 * KP 값이 0.5f 면 control 값은 error의 절반이 된다.
	 * 최대 duty 값을 30%로 제한했기 때문에, 각도 오차가 커도 30.00%를 넘지 않아야 한다.
	 */
	calculate_gimbal_error(&sensor_angle, &error);
	printf("gimbal: error roll=%.2f pitch=%.2f deg\n",
			error.roll, error.pitch);

	calculate_gimbal_control_output(&error, &control_output);
	printf("gimbal: control roll=%.2f pitch=%.2f\n", control_output.roll, control_output.pitch);

	calculate_gimbal_pwm_output(&control_output, &pwm_output);
	printf("gimbal: pwm roll=%.2f%% pitch=%.2f%%\n", pwm_output.roll_duty_percent, pwm_output.pitch_duty_percent);

#if 0
	printf("gimbal: before apply pwm\n");
	if (apply_gimbal_pwm_output(&pwm_output) < 0)
		return -1;
	printf("gimbal: after apply pwm\n");
	sleep(10);
#endif

	calculate_gimbal_motor_direction(&control_output, &direction);
	printf("gimbal: motor direction roll=%d pitch=%d\n", direction.roll, direction.pitch);

    return 0;
}

/*
 * 짐벌 제어 계산을 한 번 수행하고 계산된 PWM duty를 실제 PWM에 적용한다.

 * 이 함수는 실제 제어용 함수다.
 * 센서를 읽고, 자이로 보정을 적용하고, roll/pitch 각도를 계산한 뒤,
 * 목표 각도와의 오차를 이용해 PWM duty를 계산한다.

 * 아직 방향 제어는 적용하지 않는다.
 */
static int run_gimbal_control_once(
	const struct mpu6050_calibration *calibration,
	struct output_gpio *roll_dir_gpio,
	struct output_gpio *pitch_dir_gpio,
	int print_log)
{
	struct mpu6050_data sensor_data;
	struct mpu6050_angle sensor_angle;
	struct gimbal_error error;
	struct gimbal_control_output control_output;
	struct gimbal_pwm_output pwm_output;
	struct gimbal_motor_direction direction;

	if (mpu6050_read(&sensor_data) < 0)
		return -1;

	mpu6050_apply_gyro_calibration(&sensor_data, calibration);

	if (mpu6050_calculate_angle(&sensor_data, &sensor_angle) < 0)
		return -1;

	calculate_gimbal_error(&sensor_angle, &error);
	calculate_gimbal_control_output(&error, &control_output);
	calculate_gimbal_pwm_output(&control_output, &pwm_output);
	calculate_gimbal_motor_direction(&control_output, &direction);

	/* 모터 방향 결정. */
	apply_gimbal_direction_invert(&direction);

	if (apply_gimbal_motor_direction(roll_dir_gpio, pitch_dir_gpio, &direction) < 0)
		return -1;

	if (print_log)
		printf("gimbal: angle roll=%.2f pitch=%.2f deg, "
				"pwm roll=%.2f%% pitch=%.2f%%, "
				"direction roll=%d pitch=%d\n",
				sensor_angle.roll,
				sensor_angle.pitch,
				pwm_output.roll_duty_percent,
				pwm_output.pitch_duty_percent,
				direction.roll,
				direction.pitch);

	if (apply_gimbal_pwm_output(&pwm_output) < 0)
		return -1;

	return 0;
}

/*
 무한 루프가 아니라 250번만 반복해서 약 5초 동안만 확인
 * 짧은 시간 동안 짐벌 제어를 반복 실행한다.
 */
static int run_gimbal_control_test_loop(
	const struct mpu6050_calibration *calibration,
	struct output_gpio *roll_dir_gpio,
	struct output_gpio *pitch_dir_gpio)
{
	int i;

	for (i=0; i<GIMBAL_CONTROL_TEST_COUNT && !stop_requested; i++) {
		int print_log;

		print_log = ( i % GIMBAL_LOG_INTERVAL_COUNT) == 0;
		if (run_gimbal_control_once(calibration,
									roll_dir_gpio,
									pitch_dir_gpio,
									print_log) < 0)
			return -1;

		usleep(GIMBAL_CONTROL_INTERVAL_US);
	}

	return 0;
}

/*
 * 서비스 실행 중 계속 동작할 짐벌 제어 루프다.
 * stop_requested가 설정되면 루프를 빠져나온다.
 * systemd stop 또는 Ctrl+C가 들어왔을 때 PWM 정리 코드로 넘어가기 위해서다.
 */
static int run_gimbal_control_loop(
	const struct mpu6050_calibration *calibration,
	struct output_gpio *roll_dir_gpio,
	struct output_gpio *pitch_dir_gpio)
{
	int loop_count = 0;

	while(!stop_requested) {
		int print_log;
		print_log = (loop_count % GIMBAL_LOG_INTERVAL_COUNT) == 0;

		if (run_gimbal_control_once(calibration,
									roll_dir_gpio,
									pitch_dir_gpio,
									print_log) < 0)
			return -1;

		loop_count++;
		usleep(GIMBAL_CONTROL_INTERVAL_US);
	}

	return 0;
}

int main(void)
{
	struct button_gpio record_button = {0};
	struct output_gpio roll_dir_gpio = {0};
	struct output_gpio pitch_dir_gpio = {0};
	char recording_path[256];

	printf("castle-gimbal: start\n");

	/*
	 * GPIO17 영상 녹화버튼으로 설정.(INPUT)
	 * 초기화 실패시 PWM 종료.
	 */
	if (init_record_button(&record_button) < 0)
		return 1;

	/* PWM0, PWM1 초기화 */
	if (configure_pwm_channel(0) < 0)
		return 1;
	if (configure_pwm_channel(1) < 0)
		return 1;

	printf("castle-gimbal: PWM initialization complete\n");

	if (apply_pwm_output(0, 0.0f) < 0)
		return 1;
	if (apply_pwm_output(1, 0.0f) < 0)
		return 1;

	/* PWM0/PWM1 GPIO 설정 */
	if (init_output_gpio(&roll_dir_gpio, GIMBAL_ROLL_DIR_GPIO, "castle-roll-dir") < 0)
		return 1;
	if (init_output_gpio(&pitch_dir_gpio, GIMBAL_PITCH_DIR_GPIO, "castle-pitch-dir") < 0)
		return 1;
#if TEST_PRINT
	/* PWM GPIO channel test */
	if (test_direction_gpio_once(&roll_dir_gpio, &pitch_dir_gpio) < 0)	return 1;
#endif
	if (mpu6050_init() == 0) {
		struct mpu6050_calibration calibration;

		if (mpu6050_calibrate_gyro(&calibration, 100) == 0) {
			/* MPU6050 초기화 후 한 번 출력한다 */
			print_mpu6050_once(&calibration);
#if 1
			if (run_gimbal_control_loop(&calibration,
										&roll_dir_gpio,
										&pitch_dir_gpio) < 0)
				fprintf(stderr, "gimbal: control loop failed\n");
#else
			if (run_gimbal_control_test_loop(&calibration,
											roll_dir_gpio,
											pitch_dir_gpio,
											print_log) < 0)
				fprintf(stderr, "gimbal: control test leep failed\n");
#endif
		}
#if TEST_PRINT
		struct mpu6050_data sensor_data;
		struct mpu6050_angle sensor_angle;
		if (mpu6050_read(&sensor_data) == 0) {
			printf("mpu6050: accel x=%.2f y=%.2f z=%2.f g\n",
					sensor_data.accel_x,
					sensor_data.accel_y,
					sensor_data.accel_z);

			printf("mpu6050: gyro x=%.2f y=%.2f z=%.2f deg/s\n",
					sensor_data.gyro_x,
					sensor_data.gyro_y,
					sensor_data.gyro_z);

			printf("mpu6050: temperature %.2f C\n", sensor_data.temperature);

			if (mpu6050_calculate_angle(&sensor_data, &sensor_angle) == 0) {
				printf("mpu6050: angle roll=%.2f pitch=%.2f deg\n",
						sensor_angle.roll,
						sensor_angle.pitch);
			}
		}
#endif
	}
	/*
	 * Ctrl+C(SIGINT)와 종료 요청(SIGTERM)을
	 * handle_stop_signal 함수가 처리하도록 등록.
	 */
	signal(SIGINT, handle_stop_signal);
	signal(SIGTERM, handle_stop_signal);

	/*
	 * 카메라와 버튼을 사용하기 전에 녹화 저장 폴더를 준비한다.
	 */
	if (prepare_recording_directory() < 0)
		return -1;

#if TEST_PRINT
	/*
	 * 첫번째 모터 드라이버용 PWM 채널 시험.
	 */
	if (enable_pwm_channel(0, 30) < 0)
		return -1;
	if (enable_pwm_channel(1, 30) < 0)
		return -1;
#endif
	int previous_button_pressed = 0;
	/*
	 * 최대 3초 동안 PWM을 출력한다.
	 * 100ms 마다 종료 요청이 들어왔는지 확인한다.
	 */
	while (!stop_requested) {
		int button_pressed;

		if (stop_requested)
			break;

		button_pressed = read_record_button(&record_button);
		if (button_pressed < 0) {
			close_record_button(&record_button);
			return 1;
		}

		/*
		 * 이전에는 해제 상태였고 현재 눌림 상태가 된 순간만 처리한다.
		 */
		if (button_pressed && !previous_button_pressed) {
			if (camera_is_recording()) {
				if (camera_stop_recording() < 0)
					fprintf(stderr, "castle-gimbal: cannot stop recording\n");
			} else {
				int free_space;

				/*
				 * 녹화를 시작할 때마다 현재 날짜와 시간으로 새 파일명을 만든다.
				 */
				free_space = has_recording_space();
				if (free_space < 0) {
					fprintf(stderr, "castle-gimbal: recording space check failed\n");
				} else if (free_space == 0) {
					fprintf(stderr, "castle-gimbal: recording rejected due to low space\n");
				} else if (make_recording_path(recording_path, sizeof(recording_path)) < 0) {
					fprintf(stderr, "castle-gimbal: cannot create recording path\n");
				} else if (camera_start_recording(recording_path) < 0) {
					fprintf(stderr, "castle-gimbal: cannot start recording\n");
				}
			}
		}
		/* 다음 반복에서 비교할 수 있도록 현재 상태를 저장한다. */
		previous_button_pressed = button_pressed;
		usleep(100000);
	}

	/*
	 * 프로그램 종료 요청 시 녹화 상태를 먼저 정리한다.
	 */
	if (camera_is_recording()) {
		if (camera_stop_recording() < 0)
			fprintf(stderr, "castle-gimbal: cannot stop recording during shutdown\n");
	}
	/* 정상 종료와 Ctrl+C 종료 모두 PWM 채널을 끈다. */
	int shutdown_failed = 0;
	if ((force_disable_pwm_channel(0) < 0) || (force_disable_pwm_channel(1) < 0))
		shutdown_failed = 1;

	if (shutdown_failed)
		return 1;

	release_output_gpio(&roll_dir_gpio);
	release_output_gpio(&pitch_dir_gpio);

	/*
	 * 프로그램 종료 전에 GPIO17 사용 권한과 GPIO 칩을 해제한다.
	 */
	close_record_button(&record_button);
	mpu6050_close();

	return 0;
}

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <gpiod.h>

#include "camera.h"

#include <time.h>			// 파일생성시 현재날짜와 시간사용을 위해.
#include <sys/stat.h>		// 녹화 저장 폴더가 없을때 mkdir() 생성.
#include <sys/statvfs.h>	// statvfs()를 사용해 파일시스템의 남은 공간 확인

#define PWM_CHIP_PATH "/sys/class/pwm/pwmchip0"
#define PWM_PERIOD_NS 100000
#define BUTTON_GPIO_CHIP "/dev/gpiochip0"
#define BUTTON_GPIO_OFFSET 17
/* 녹화 디렉토리 지정. */
#define RECORDING_DIRECTORY	"/root/recordings"

#define MIN_RECORDING_FREE_BYTES (200ULL * 1024ULL * 1024ULL)
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
 * 영상 녹화 버튼의 현재 상태 확인.
 * 반환값:
 * 1: 버튼 눌림
 * 0: 버튼 해제
 * -1: 읽기 오류
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

int main(void)
{
	struct button_gpio record_button;
	char recording_path[256];

	printf("castle-gimbal: start\n");

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

	/*
	 * 첫번째 모터 드라이버용 PWM 채널 시험.
	 */
	if (enable_pwm_channel(0, 30) < 0)
		return -1;
	if (enable_pwm_channel(1, 30) < 0)
		return -1;

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
	if (disable_pwm_channel(0) < 0)
		return 1;

	if (disable_pwm_channel(1) < 0)
		return 1;

	/*
	 * 프로그램 종료 전에 GPIO17 사용 권한과 GPIO 칩을 해제한다.
	 */
	close_record_button(&record_button);

	return 0;
}

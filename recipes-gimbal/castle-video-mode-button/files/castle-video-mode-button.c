#include <errno.h>
#include <gpiod.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	GPIO_CHIP_PATH	"/dev/gpiochip0"
#define	MODE_BUTTON_GPIO	27
#define	POLL_INTERVAL_US	50000
#define	MODE_FILE_PATH	"/run/castle-video-mode"

/* 비디오 모드 */
enum video_mode {
	VIDEO_MODE_OFF,
	VIDEO_MODE_RECORD,
	VIDEO_MODE_RTSP,
};

/* 버튼 상태 관리 */
struct mode_button {
	struct gpiod_chip *chip;
	struct gpiod_line_request *request;
	int previous_pressed;
	int waiting_for_release;
};

static volatile sig_atomic_t stop_requested;

/*
 * 종료 신호 처리
 */
static void handle_stop_signal(int signal_number)
{
	(void)signal_number;
	stop_requested = 1;
}

/*
 * 비디오 모드 설정
 * "off", "record", "rtsp" 문자열 사용.
 */
static const char *video_mode_to_string(enum video_mode mode)
{
	switch (mode) {
		case VIDEO_MODE_OFF:
			return "off";
		case VIDEO_MODE_RECORD:
			return "record";
		case VIDEO_MODE_RTSP:
			return "rtsp";
		default:
			return "off";
	}
}

/*
 * 현재 비디오 모드 저장.
 */
static int write_mode_file(enum video_mode mode)
{
	FILE *file;

	file = fopen(MODE_FILE_PATH, "w");
	if (file == NULL) {
		fprintf(stderr, "castle-video-mode-button: cannot open %s: %s\n",
				MODE_FILE_PATH, strerror(errno));
		return -1;
	}

	if (fprintf(file, "%s\n", video_mode_to_string(mode)) < 0) {
		fprintf(stderr, "castle-video-mode-button: cannot write %s: %s\n",
				MODE_FILE_PATH, strerror(errno));
		fclose(file);
		return -1;
	}

	fclose(file);
	return 0;
}

/*
 * 다음 video 모드 확인.
*/
static enum video_mode get_next_mode(enum video_mode current_mode)
{
	switch(current_mode) {
		case VIDEO_MODE_OFF:
			return VIDEO_MODE_RECORD;
		case VIDEO_MODE_RECORD:
			return VIDEO_MODE_RTSP;
		case VIDEO_MODE_RTSP:
		default:
			return VIDEO_MODE_OFF;
	}
}

/* 선택 모드를 실제 서비스 전환 */
static int apply_video_mode(enum video_mode mode)
{
	int result;

	switch(mode) {
		case VIDEO_MODE_OFF:
			result = system("systemctl --no-block stop castle-record.service");
			if (result != 0) {
				fprintf(stderr, "castle-video-mode-button: failed to stop castle-record.service\n");
				return -1;
			}

			result = system("systemctl --no-block stop castle-rtsp-server.service");
			if (result != 0) {
				fprintf(stderr, "castle-video-mode-button: failed to stop castle-rtsp-server.service\n");
				return -1;
			}
			break;
		case VIDEO_MODE_RECORD:
			result = system("systemctl --no-block stop castle-rtsp-server.service");
			if (result != 0) {
				fprintf(stderr, "castle-video-mode-button: failed to stop castle-rtsp-server.service\n");
				return -1;
			}

			result = system("sh -c 'systemctl start castle-record.service >/dev/null 2>&1 &'");
			if (result != 0) {
				fprintf(stderr, "castle-video-mode-button: failed to start castle-record.service\n");
				return -1;
			}
			break;
		case VIDEO_MODE_RTSP:
			result = system("systemctl --no-block stop castle-record.service");
			if (result != 0) {
				fprintf(stderr, "castle-video-mode-button: failed to stop castle-record.service\n");
				return -1;
			}

			result = system("sh -c 'systemctl start castle-rtsp-server.service >/dev/null 2>&1 &'");
			if (result != 0) {
				fprintf(stderr, "castle-video-mode-button: failed to start castle-rtsp-server.service\n");
				return -1;
			}
			break;
		default:
			fprintf(stderr, "castle-video-mode-button: invaild mode\n");
			return -1;
	}

	return 0;
}

/* 선택버튼 클릭 시 모드 변경, 서비스 적용, 상태 저장 */
static int switch_to_next_mode(enum video_mode *current_mode)
{
	enum video_mode next_mode;

	if (current_mode == NULL)	return -1;

	next_mode = get_next_mode(*current_mode);

	if (apply_video_mode(next_mode) < 0)	return -1;
	if (write_mode_file(next_mode) < 0)		return -1;

	*current_mode = next_mode;

	printf("castle-video-mode-button: mode changed to %s\n", video_mode_to_string(next_mode));
	return 0;
}

/* GIPO 27 button 설정 */
static int init_mode_button(struct mode_button *button)
{
	struct gpiod_line_settings	*line_settings;
	struct gpiod_line_config	*line_config;
	struct gpiod_request_config	*request_config;
	unsigned int line_offset	= MODE_BUTTON_GPIO;

	if (button == NULL)	return -1;

	memset(button, 0, sizeof(*button));
	button->chip = gpiod_chip_open(GPIO_CHIP_PATH);
	if (button->chip == NULL) {
		fprintf(stderr, "castle-video-mode-button: cannot open %s: %s\n",
				GPIO_CHIP_PATH, strerror(errno));
		return -1;
	}

	line_settings = gpiod_line_settings_new();
	if (line_settings == NULL) {
		fprintf(stderr, "castle-video-mode-button: cannot create line settings\n");
		goto error;
	}

	gpiod_line_settings_set_direction(line_settings, GPIOD_LINE_DIRECTION_INPUT);
	gpiod_line_settings_set_bias(line_settings, GPIOD_LINE_BIAS_PULL_UP);

	line_config = gpiod_line_config_new();
	if (line_config == NULL) {
		fprintf(stderr, "castle-video-mode-button: cannot create line config\n");
		gpiod_line_settings_free(line_settings);
		goto error;
	}

	if (gpiod_line_config_add_line_settings(line_config, &line_offset, 1, line_settings) < 0) {
		fprintf(stderr, "castle-video-mode-button: cannot add line settings: %s\n", strerror(errno));
		gpiod_line_config_free(line_config);
		gpiod_line_settings_free(line_settings);
		goto error;
	}

	gpiod_line_settings_free(line_settings);

	request_config = gpiod_request_config_new();
	if (request_config == NULL) {
		fprintf(stderr, "castle-video-mode-button: cannot create request config\n");
		gpiod_line_config_free(line_config);
		goto error;
	}

	gpiod_request_config_set_consumer(request_config, "castle-video-mode-button");
	button->request = gpiod_chip_request_lines(button->chip, request_config, line_config);

	gpiod_request_config_free(request_config);
	gpiod_line_config_free(line_config);

	if (button->request == NULL) {
		fprintf(stderr, "castle-video-mode-button: cannot request GPIO%u: %s\n",
				line_offset, strerror(errno));
		goto error;
	}

	button->previous_pressed = 0;
	return 0;

error:
	if (button->chip != NULL) {
		gpiod_chip_close(button->chip);
		button->chip = NULL;
	}
	return -1;
}

/* GPIO 버튼 상태 확인: 1 = press, 0 = none press, -1 = read fail */
static int read_mode_button_pressed(struct mode_button *button)
{
	int value;

	if (button == NULL || button->request == NULL)	return -1;

	value = gpiod_line_request_get_value(button->request, MODE_BUTTON_GPIO);
	if (value < 0) {
		fprintf(stderr, "castle-video-mode-button: cannot read GPIO%u: %s\n",
				MODE_BUTTON_GPIO, strerror(errno));
		return -1;
	}

	return value == 0 ? 1 : 0;
}

/* GPIO 버튼이 눌리는 순간 감지: 1-detect press, 0-unchanged, -1-read fail */
static int detect_new_button_press(struct mode_button *button)
{
	int pressed;

	pressed = read_mode_button_pressed(button);
	if (pressed < 0)	return -1;

	if (!pressed) {
		button->previous_pressed = 0;
		button->waiting_for_release = 0;
		return 0;
	}

	if (button->waiting_for_release)
		return 0;

	if (!button->previous_pressed) {
		button->previous_pressed = 1;
		button->waiting_for_release = 1;
		return 1;
	}
#if 0
	printf("castle-video-mode-button: pressed=%d previous=%d\n",
			pressed, button->previous_pressed);

	if (pressed && !button->previous_pressed) {
		button->previous_pressed = 1;
		button->waiting_for_release

		while (!stop_requested) {
			pressed = read_mode_button_pressed(button);
			if (pressed < 0)	return -1;

			if (!pressed) {
				button->previous_pressed = 0;
				break;
			}
			usleep(200000);
		}
		return 1;
	}
#endif

	return 0;
}

/* 프로그램 종료시 GPIO 정리 */
static void cleanup_mode_button(struct mode_button *button)
{
	if (button == NULL)	return;

	if (button->request != NULL) {
		gpiod_line_request_release(button->request);
		button->request = NULL;
	}
	if (button->chip != NULL) {
		gpiod_chip_close(button->chip);
		button->chip = NULL;
	}
}

int main(void)
{
	struct mode_button button;
	enum video_mode current_mode = VIDEO_MODE_OFF;

	signal(SIGINT, handle_stop_signal);
	signal(SIGTERM, handle_stop_signal);

	if (init_mode_button(&button) < 0)	return 1;

	/* 버튼 초기화 후 기준 상태로 설정 */	
	int pressed;
	pressed = read_mode_button_pressed(&button);
	if (pressed < 0) {
		cleanup_mode_button(&button);
		return 1;
	}
	button.previous_pressed = pressed;
	button.waiting_for_release = pressed ? 1 : 0;

	if (apply_video_mode(current_mode) < 0) {
		cleanup_mode_button(&button);
		return 1;
	}

	if (write_mode_file(current_mode) < 0) {
		cleanup_mode_button(&button);
		return 1;
	}

	printf("castle-video-mode-button: started, current mode is %s\n",
			video_mode_to_string(current_mode));

	while (!stop_requested) {
		int pressed;
		pressed = detect_new_button_press(&button);
		if (pressed < 0) {
			cleanup_mode_button(&button);
			return 1;
		}

		if (pressed > 0) {
			if (switch_to_next_mode(&current_mode) < 0) {
				cleanup_mode_button(&button);
				return 1;
			}
		}
		usleep(POLL_INTERVAL_US);
	}

	cleanup_mode_button(&button);
	return 0;
}

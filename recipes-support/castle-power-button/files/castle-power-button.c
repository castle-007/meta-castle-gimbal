#include <stdio.h>
#include <gpiod.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define	GPIO_CHIP_PATH		"/dev/gpiochip0"
#define	POWER_BUTTON_GPIO	25
#define	POLL_INTERVAL_US	50000
#define	POWER_BUTTON_HOLD	2000

/* 종료 플래그 */
static volatile sig_atomic_t	stop_requested;

/* 버튼 구조체 */
struct power_button {
	struct gpiod_chip *chip;
	struct gpiod_line_request *request;
	int previous_pressed;
	struct timespec pressed_since;
	int poweroff_triggered;
};

/* Signal Handler */
static void handle_stop_signal(int signal_number)
{
	(void)signal_number;
	stop_requested = 1;
}

static int init_power_button(struct power_button *button)
{
	struct gpiod_line_settings *line_settings;
	struct gpiod_line_config *line_config;
	struct gpiod_request_config *request_config;
	unsigned int line_offset = POWER_BUTTON_GPIO;

	if (button == NULL)	return -1;

	memset(button, 0, sizeof(*button));

	button->chip = gpiod_chip_open(GPIO_CHIP_PATH);
	if (button->chip == NULL) {
		fprintf(stderr, "castle-power-button: cannot open %s: %s\n",
				GPIO_CHIP_PATH, strerror(errno));
		return -1;
	}

	line_settings = gpiod_line_settings_new();
	if (line_settings == NULL) {
		fprintf(stderr, "castle-power-button: cannot create line settings\n");
		goto error;
	}

	gpiod_line_settings_set_direction(line_settings, GPIOD_LINE_DIRECTION_INPUT);
	gpiod_line_settings_set_bias(line_settings, GPIOD_LINE_BIAS_PULL_UP);

	line_config = gpiod_line_config_new();
	if (line_config == NULL) {
		fprintf(stderr, "castle-power-button: cannot create line config\n");
		gpiod_line_settings_free(line_settings);
		goto error;
	}

	if (gpiod_line_config_add_line_settings(line_config, &line_offset, 1,
											line_settings) < 0) {
		fprintf(stderr, "castle-power-button: cannot add line settings: %s\n", strerror(errno));
		gpiod_line_config_free(line_config);
		gpiod_line_settings_free(line_settings);
		goto error;
	}

	gpiod_line_settings_free(line_settings);

	request_config = gpiod_request_config_new();
	if (request_config == NULL) {
		fprintf(stderr, "castle-power-button: cannot create request config\n");
		gpiod_line_config_free(line_config);
		goto error;
	}

	gpiod_request_config_set_consumer(request_config, "castle-power-button");

	button->request = gpiod_chip_request_lines(button->chip, request_config,
						   line_config);
	gpiod_request_config_free(request_config);
	gpiod_line_config_free(line_config);

	if (button->request == NULL) {
		fprintf(stderr, "castle-power-button: cannot request GPIO%u: %s\n",
			line_offset, strerror(errno));
		goto error;
	}

	button->previous_pressed = 0;
	button->pressed_since.tv_sec = 0;
	button->pressed_since.tv_nsec = 0;
	button->poweroff_triggered = 0;
	return 0;

error:
	if (button->chip != NULL) {
		gpiod_chip_close(button->chip);
		button->chip = NULL;
	}

	return -1;
}

/* GPIO 입력값 확인
 * 1 = press, 0 = none press, -1 = read fail
 */
static int read_power_button_pressed(struct power_button *button)
{
	int value;

	if (button == NULL || button->request == NULL)	return -1;

	value = gpiod_line_request_get_value(button->request, POWER_BUTTON_GPIO);
	if (value < 0) {
		fprintf(stderr, "castle-power-button: cannot read GPIO%u: %s\n", POWER_BUTTON_GPIO, strerror(errno));
		return -1;
	}
	return value == 0 ? 1 : 0;
}

/*
 * Button 상태 확인
 * 2초 이상 누를때 power off 
 */
static int process_power_button(struct power_button *button)
{
	struct timespec now;
	long held_ms;
	int pressed;

	if (button == NULL)	return -1;

	pressed = read_power_button_pressed(button);

	if (pressed < 0)	return -1;

	if (!pressed) {
		button->previous_pressed = 0;
		button->pressed_since.tv_sec = 0;
		button->pressed_since.tv_nsec = 0;
		button->poweroff_triggered = 0;
		return 0;
	}

	if (!button->previous_pressed) {
		button->previous_pressed = 1;
		if (clock_gettime(CLOCK_MONOTONIC, &button->pressed_since) < 0) {
			fprintf(stderr, "castle-power-button: cannot read pressed_since time\n");
			return -1;
		}
		return 0;
	}

	if (button->poweroff_triggered)	return 0;

	if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
		fprintf(stderr, "castle-power-button: cannot read current time\n");
		return -1;
	}

	time_t sec_diff;
	long nsec_diff;

	sec_diff = now.tv_sec - button->pressed_since.tv_sec;
	nsec_diff = now.tv_nsec - button->pressed_since.tv_nsec;

	if (nsec_diff < 0) {
		sec_diff--;
		nsec_diff += 1000000000L;
	}
	held_ms = sec_diff * 1000L + nsec_diff / 1000000L;

	if (held_ms >= POWER_BUTTON_HOLD) {
		button->poweroff_triggered = 1;
		printf("castle-power-button: power button held for %ld ms, power off\n", held_ms);
		if (system("systemctl poweroff") != 0) {
			fprintf(stderr, "castle-power-button: failed to execute poweroff\n");
			return -1;
		}
	}

	return 0;
}

static void cleanup_power_button(struct power_button *button)
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
	struct power_button button;

	signal(SIGINT, handle_stop_signal);
	signal(SIGTERM, handle_stop_signal);

	if (init_power_button(&button) < 0)	return 1;
	printf("castle-power-button: started on GPIO%u, hold %d ms for poweroff\n",
			POWER_BUTTON_GPIO, POWER_BUTTON_HOLD);

	while (!stop_requested) {
		if (process_power_button(&button) < 0) {
			cleanup_power_button(&button);
			return 1;
		}
		usleep(POLL_INTERVAL_US);
	}

	cleanup_power_button(&button);
	return 0;
}

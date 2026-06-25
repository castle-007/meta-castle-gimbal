#include "camera.h"
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * 실행 중인 실제 카메라 녹화 프로세스의 PID를 저장한다.
 * -1: 녹화 프로세스 없음.
 *  1 이상: 싱행 중인 녹화 프로세스 PID.
 */
static pid_t recording_pid = -1;

#define CAMERA_DEVICE	"/dev/video0"
#define CAMERA_WIDTH	1280
#define CAMERA_HEIGHT	720
#define CAMERA_COMMAND	"/usr/bin/v4l2-ctl"

/*
 * 카메라 녹화를 시작한다.
 * 현재 단계에서는 실제 카메라를 실행하지 않고 상태만 변경한다.
 */
int camera_start_recording(const char *output_path)
{
	pid_t pid;
	char format_value[80];
	char output_option[256];

	if (recording_pid > 0) {
		fprintf(stderr, "camera: recording is already active\n");
		return -1;
	}

	if (output_path == NULL || output_path[0] == '\0') {
		fprintf(stderr, "camera: invalid output path\n");
		return -1;
	}

	snprintf(format_value, sizeof(format_value),
			"width=%d,height=%d,pixelformat=H264",
			CAMERA_WIDTH, CAMERA_HEIGHT);

	snprintf(output_option, sizeof(output_option),
			"--stream-to=%s", output_path);

	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "camera: fork failed: %s\n",
				strerror(errno));
	}

	if (pid == 0) {
		execl(CAMERA_COMMAND,
			"v4l2-ctl",
			"--device=" CAMERA_DEVICE,
			"--set-fmt-video",
			format_value,
			"--stream-mmap=3",
			output_option,
			(char*)NULL);

		fprintf(stderr, "camera: cannot execute v4l2-ctl: %s\n", strerror(errno));
		_exit(127);
	}

	recording_pid = pid;
	printf("camera: recording started: %s, pid=%ld\n",
			output_path, (long)recording_pid);

	return 0;
}

/*
 * 진행 중인 카메라 녹화를 정지한다.
 * v4l2-ctl 프로세스에 SIGINT를 보내 녹화 종료.
 */
int camera_stop_recording(void)
{
	int status;

	if (recording_pid <= 0)
		return 0;

	/*
	 * Ctrl+C와 같은 SIGINT를 보내 파일 저장을 정상적으로 마치게 한다.
	 */
	if (kill(recording_pid, SIGINT) < 0) {
		fprintf(stderr, "camera: cannot stop process %ld: %s\n",
				(long)recording_pid, strerror(errno));
		return -1;
	}

	/*
	 * 자식 프로세스가 완전히 종료될 때까지 기다린다.
	 */
	if (waitpid(recording_pid, &status, 0) < 0) {
		fprintf(stderr, "camera: waitpid failed: %s\n", strerror(errno));
		return -1;
	}

	printf("camera: recording stopped\n");
	recording_pid = -1;

	return 0;
}

/*
 * 현재 녹화 상태를 반환한다.
 * 저장된 PID 확인
 */
int camera_is_recording(void)
{
	return recording_pid > 0;
}

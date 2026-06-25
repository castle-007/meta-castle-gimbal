#ifndef CASTLE_CAMERA_H
#define CASTLE_CAMERA_H

/*
 * 카메라 녹화를 시작한다.
 *
 * 반환값:
 *  0: 성공
 * -1: 실패
 */
int camera_start_recording(const char *output_path);

/*
 * 현재 진행 중인 카메라 녹화를 정지한다.
 *
 * 반환값:
 *  0: 성공
 * -1: 실패
 */
int camera_stop_recording(void);

/*
 * 현재 녹화 중인지 확인한다.
 *
 * 반환값:
 *  1: 녹화 중
 *  0: 녹화 정지
 */
int camera_is_recording(void);
#endif

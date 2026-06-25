#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <stdio.h>

#define RTSP_PORT			"8554"
#define RTSP_MOUNT_POINT	"/gimbal"
#define CAMERA_DEVICE		"/dev/video0"

int main(int argc, char *argv[])
{
	GMainLoop *main_loop;
	GstRTSPServer *server;
	GstRTSPMountPoints *mounts;
	GstRTSPMediaFactory *factory;

	/*
	 * 명령행 옵션을 처리하고 GStreamer 내부 기능을 초기화한다.
	 */
	gst_init(&argc, &argv);

	/*
	 * RTSP 서버가 종료될 때까지 이벤트를 처리할 메인 루프를 만든다.
	 */
	main_loop = g_main_loop_new(NULL, FALSE);
	if (main_loop == NULL) {
		fprintf(stderr, "castle-gimbal: cannot create RTSP server\n");
		return 1;
	}

	/*
	 * RTSP 서버 객체를 만든다.
	 */
	server = gst_rtsp_server_new();
	if (server == NULL) {
		fprintf(stderr, "castle-rtsp: cannot create RTSP server\n");
		g_main_loop_unref(main_loop);
		return 1;
	}

	/*
	 * RTSP 서버가 8554 포트에서 접속을 받도록 설정한다.
	 */
	g_object_set(server, "service", RTSP_PORT, NULL);

	/*
	 * 클라이언트가 접속할 경로를 관리하는 객체를 가져온다.
	 */
	mounts = gst_rtsp_server_get_mount_points(server);
	if (mounts == NULL) {
		fprintf(stderr, "castle-rtsp: cannot get mount points\n");
		g_object_unref(server);
		g_main_loop_unref(main_loop);
		return 1;
	}

	/*
	 * /dev/video0의 H.264 영상을 RTP 패킷으로 만드는 미디어 공장을 생성한다.
	 */
	factory = gst_rtsp_media_factory_new();
	if (factory == NULL) {
		fprintf(stderr, "castle-rtsp: cannot create media factory\n");
		g_object_unref(mounts);
		g_object_unref(server);
		g_main_loop_unref(main_loop);
		return 1;
	}

	gst_rtsp_media_factory_set_launch( factory,
			"( v4l2src device=" CAMERA_DEVICE " ! "
			"video/x-h264,width=1280,height=72-,framerate=30/1 ! "
			"h264parse ! rtph264pay name=pay0 pt=96 config-interval=1 )");

	/*
	 * 여러 클라이언트가 하나의 카메라 파이프라인을 공유한다.
	 */
	gst_rtsp_media_factory_set_shared(factory, TRUE);

	/*
	 * 생성한 영상 파이프라인을 /gimbal 경로에 등록한다.
	 * 이 함수가 factory의 소유권을 mounts로 넘긴다.
	 */
	gst_rtsp_mount_points_add_factory(
		mounts, RTSP_MOUNT_POINT, factory);
	g_object_unref(mounts);

	/*
	 * RTSP 서버를 기본 메인 루프에 연결한다.
	 * 실패하면 포트 사용 중이거나 서버 설정에 문제가 있는 것이다.
	 */
	if (gst_rtsp_server_attach(server, NULL) == 0) {
		fprintf(stderr, "castle-rtsp: cannot attach RTSP server\n");
		g_object_unref(server);
		g_main_loop_unref(main_loop);
		return 1;
	}

	printf("castle-rtsp: server started\n");
	printf("castle-rtsp: rtsp://<board-ip>:%s%s\n", RTSP_PORT, RTSP_MOUNT_POINT);

	/*
	 * 종료될 때까지 RTSP 접속과 영상 처리를 계속한다.
	 */
	g_main_loop_run(main_loop);

	g_object_unref(server);
	g_main_loop_unref(main_loop);

	return 0;
}

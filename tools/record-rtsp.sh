#!/bin/sh

set -eu

RTSP_URL="${1:-rtsp://192.168.121.129:8554/gimbal}"
OUTPUT_DIR="${2:-.}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
OUTPUT_FILE="${OUTPUT_DIR}/gimbal-rtsp-${TIMESTAMP}.mp4"

mkdir -p "${OUTPUT_DIR}"

echo "Recording RTSP stream"
echo "  URL    : ${RTSP_URL}"
echo "  Output : ${OUTPUT_FILE}"
echo "Press q in ffmpeg to stop recording cleanly."

ffmpeg \
	-rtsp_transport tcp \
	-i "${RTSP_URL}" \
	-c copy \
	-movflags +faststart \
	"${OUTPUT_FILE}"

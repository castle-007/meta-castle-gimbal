#!/bin/sh

set -eu

if [ "$#" -lt 1 ]; then
	echo "Usage: $0 input.h264 [output.mp4]" >&2
	exit 1
fi

INPUT_FILE="$1"

if [ "$#" -ge 2 ]; then
	OUTPUT_FILE="$2"
else
	OUTPUT_FILE="${INPUT_FILE%.*}.mp4"
fi

echo "Converting H264 to MP4"
echo "  Input  : ${INPUT_FILE}"
echo "  Output : ${OUTPUT_FILE}"

ffmpeg \
	-i "${INPUT_FILE}" \
	-c copy \
	-movflags +faststart \
	"${OUTPUT_FILE}"

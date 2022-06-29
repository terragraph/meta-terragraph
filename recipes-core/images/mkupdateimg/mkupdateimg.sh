#!/bin/sh

MYDIR=$(dirname "$0")
BLOCK_SIZE=1024
HDRSIZE=$((32 * BLOCK_SIZE))
PREAMBLE="${MYDIR}/preamble.template"
IMAGE=""
IMAGE_META=""

usage () {
	echo "Usage: $0 [-h] [-p preamble] -t type -i image -o output"
	exit 64
}

while getopts ":i:o:p:t:" opt; do
	case $opt in
	i)
		IMAGE="${OPTARG}"
		;;
	o)
		OIMAGE="${OPTARG}"
		;;
	p)
		PREAMBLE="${OPTARG}"
		;;
	t)
		TYPE="${OPTARG}"
		;;
	\?)
		usage
		;;
	:)
		echo "-$OPTARG requires an argument!" >&2
		usage
		;;
	esac
done

if [ x"${IMAGE}" = "x" ]; then
	usage
fi

if [ ! -e "${IMAGE}" ]; then
	echo "Input image not found: '${IMAGE}'"
	exit 1
fi

IMAGE_META="${IMAGE}.meta"
if [ ! -e "${IMAGE_META}" ]; then
	echo "Meta file of input image not found: '${IMAGE_META}'"
	exit 1
fi

imgsize=$(stat -Lc %s "${IMAGE}")
if [ $? -ne 0 ]; then
	echo "Unable to determine the image size"
	exit 1
fi
metasize=$(stat -Lc %s "${IMAGE_META}")
if [ $? -ne 0 ]; then
	echo "Unable to determine the image meta file size"
	exit 1
fi
META_BLOCK_SIZE=$(((metasize + BLOCK_SIZE - 1) / BLOCK_SIZE * BLOCK_SIZE))
if [ ${META_BLOCK_SIZE} -ge "${HDRSIZE}" ]; then
	echo "Meta file is too large"
	exit 1
fi
md5=$(md5sum "${IMAGE}" | cut -d " " -f1)
if [ $? -ne 0 ]; then
	echo "Checksum computation of image failed"
	exit 1
fi
metamd5=$(md5sum "${IMAGE_META}" | cut -d " " -f1)
if [ $? -ne 0 ]; then
	echo "Checksum computation if image meta file failed"
	exit 1
fi
preamblesize=$(stat -Lc %s "${PREAMBLE}")
if [ $? -ne 0 ]; then
	echo "Unable to determine the preamble size"
	exit 1
fi
PREAMBLE_BLOCK_SIZE="$((HDRSIZE - META_BLOCK_SIZE))"
if [ "${preamblesize}" -gt "${PREAMBLE_BLOCK_SIZE}" ]; then
	echo "Preamble is too large"
	exit 1
fi

tmphdr=$(mktemp /tmp/preamble.XXXX)
trap "rm -f ${tmphdr}; exit" HUP INT TERM QUIT

sed -E -e " s|%%IMGTYPE%%|${TYPE}| " \
		-e "s|%%MD5%%|${md5}|" \
		-e "s|%%METAMD5%%|${metamd5}|" \
		-e "s|%%HDRSIZE%%|${HDRSIZE}|" \
		-e "s|%%PREAMBLE_BLOCK_SIZE%%|${PREAMBLE_BLOCK_SIZE}|" \
		-e "s|%%METASIZE%%|${metasize}|" \
    -e "s|%%IMGSIZE%%|${imgsize}|" "${PREAMBLE}" > "${tmphdr}"
if [ $? -ne 0 ]; then
	exit 1
fi

dd if="${IMAGE_META}" ibs="${META_BLOCK_SIZE}" of="${tmphdr}" oflag=seek_bytes \
  seek="${PREAMBLE_BLOCK_SIZE}" count=1 conv=notrunc,sync || exit 1
cat "${tmphdr}" "${IMAGE}" > "${OIMAGE}" || exit 1
chmod a+x "${OIMAGE}"
rm -f "${tmphdr}"

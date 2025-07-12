set -e

source test/literals.sh

setup() {
	make
	insmod $MODULE_OBJ
	modprobe brd rd_nr=1 rd_size=$DISK_SIZE_IN_KB max_part=0
	echo -n $UNDERLYING_DEVICE > $DEVICE_MAPPER
}

make_requests() {
	dd if=$DEVICE_RANDOM of=$TEST_DEVICE bs=4k count=9 oflag=direct
	dd if=$TEST_DEVICE of=$DEVICE_ZERO bs=512 count=14 iflag=direct
	dd if=$DEVICE_RANDOM of=$TEST_DEVICE bs=512 count=20 oflag=direct
	dd if=$TEST_DEVICE of=$DEVICE_ZERO bs=4k count=8 iflag=direct
}

reset_stats() {
	echo -n reset > $REQUEST_STATS
}

get_stats() {
	cat $REQUEST_STATS
}

cleanup() {
	exit_code=$?
	rmmod $MODULE_NAME
	rmmod brd
	exit $exit_code
}

trap cleanup EXIT

setup
make_requests
get_stats
reset_stats
get_stats

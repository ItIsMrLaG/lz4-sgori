source test/literals.sh

set -euxo pipefail

setup() {
	make
	insmod $MODULE_OBJ
	modprobe brd rd_nr=1 rd_size=$DISK_SIZE_IN_KB max_part=0
	echo -n $UNDERLYING_DEVICE > $DEVICE_MAPPER
}

get_disk_info() {
	cat $DEVICE_MAPPER
	cat $DEVICE_UNMAPPER
}

cleanup() {
	exit_code=$?
	rmmod $MODULE_NAME
	rmmod brd
	exit $exit_code
}

trap cleanup EXIT

setup
get_disk_info

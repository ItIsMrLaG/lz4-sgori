set -e

source test/literals.sh

setup() {
	make
	insmod $MODULE_OBJ
	modprobe brd rd_nr=1 rd_size=$DISK_SIZE_IN_KB max_part=0
	echo -n $UNDERLYING_DEVICE > $DEVICE_MAPPER
	mkdir $TEMP_DIR
	touch $PROXY_OUTPUT_FILE
}

make_requests_and_compare() {
	dd if=$PROXY_TEST_FILE of=$TEST_DEVICE bs=4k count=2 oflag=direct
	dd if=$TEST_DEVICE of=$PROXY_OUTPUT_FILE bs=4k count=2 iflag=direct
	cmp --verbose --bytes=$PROXY_TEST_FILE_LEN $PROXY_TEST_FILE $PROXY_OUTPUT_FILE
}

cleanup() {
	exit_code=$?
	rm -rf $TEMP_DIR
	rmmod $MODULE_NAME
	rmmod brd
	exit $exit_code
}

trap cleanup EXIT

setup
make_requests_and_compare

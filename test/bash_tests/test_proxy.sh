source test/literals.sh

set -euxo pipefail

setup() {
	make reinsert
	modprobe brd rd_nr=1 rd_size=$DISK_SIZE_IN_KB max_part=0
	echo -n $UNDERLYING_DEVICE > $DEVICE_MAPPER
	mkdir $TEMP_DIR
}

compare_files() {
	file1=$1
	file2=$2
	bytes=$3

	cmp --verbose --bytes=$bytes $file1 $file2
}

test1() {
	dd if=$PROXY_TEST_FILE1 of=$TEST_DEVICE bs=1k count=5 oflag=direct
	dd if=$TEST_DEVICE of=$PROXY_OUTPUT_FILE1 bs=1k count=5 iflag=direct
	compare_files $PROXY_TEST_FILE1 $PROXY_OUTPUT_FILE1 $PROXY_TEST_FILE_LEN1
}

test2() {
	dd if=$PROXY_TEST_FILE2 of=$TEST_DEVICE bs=4k count=5 oflag=direct
	dd if=$TEST_DEVICE of=$PROXY_OUTPUT_FILE2 bs=4k count=5 iflag=direct
	compare_files $PROXY_TEST_FILE2 $PROXY_OUTPUT_FILE2 $PROXY_TEST_FILE_LEN2
}

test3() {
	dd if=$PROXY_TEST_FILE3 of=$TEST_DEVICE bs=36k count=8 oflag=direct
	dd if=$TEST_DEVICE of=$PROXY_OUTPUT_FILE3 bs=36k count=8 iflag=direct
	compare_files $PROXY_TEST_FILE3 $PROXY_OUTPUT_FILE3 $PROXY_TEST_FILE_LEN3
}

cleanup() {
	exit_code=$?
	rm -rf $TEMP_DIR
	make remove
	rmmod brd
	exit $exit_code
}

trap cleanup EXIT

setup
test1
test2
test3

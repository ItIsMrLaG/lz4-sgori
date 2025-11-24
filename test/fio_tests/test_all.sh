source test/literals.sh

set -euxo pipefail

setup() {
	make reinsert
	modprobe brd rd_nr=1 rd_size=$DISK_SIZE_IN_KB max_part=0
	echo -n $UNDERLYING_DEVICE > $DEVICE_MAPPER
}

run_test() {
	file=$1
	fio $file > /dev/null
}

run_all_tests() {
	for test_file in test/fio_tests/test_*.fio; do
		run_test $test_file
	done
}

cleanup() {
	exit_code=$?
	make remove
	rmmod brd
	exit $exit_code
}

trap cleanup EXIT

setup
run_all_tests

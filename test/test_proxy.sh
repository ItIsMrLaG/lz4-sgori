set -e

MAPPER=/sys/module/blk_comp/parameters/mapper
UNMAPPER=/sys/module/blk_comp/parameters/unmapper

TEST_DISK=/dev/blk-comp-0
TEST_FILE=test/test_files/01.txt
TEST_FILE_LEN=$(stat --print="%s" $TEST_FILE)
TEMP_DIR=test/tmp
OUTPUT_FILE=test/tmp/01.txt

make
insmod build/blk_comp.ko

modprobe brd rd_nr=1 rd_size=307200 max_part=0
echo -n /dev/ram0 > $MAPPER

mkdir $TEMP_DIR
touch $OUTPUT_FILE

dd if=$TEST_FILE of=$TEST_DISK bs=4k count=2 oflag=direct
dd if=$TEST_DISK of=$OUTPUT_FILE bs=4k count=2 iflag=direct

cmp --verbose --bytes=$TEST_FILE_LEN $TEST_FILE $OUTPUT_FILE

rm -rf $TEMP_DIR

rmmod blk_comp

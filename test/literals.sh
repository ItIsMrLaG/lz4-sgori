export BDEV_NAME=lz4e_bdev
export BDEV_PARAMETERS=/sys/module/$BDEV_NAME/parameters

export DEVICE_MAPPER=$BDEV_PARAMETERS/mapper
export DEVICE_UNMAPPER=$BDEV_PARAMETERS/unmapper
export REQUEST_STATS=$BDEV_PARAMETERS/stats

export UNDERLYING_DEVICE=/dev/ram0
export TEST_DEVICE=/dev/lz4e0
export DISK_SIZE_IN_KB=307200

export TEST_FILES_DIR=test/test_files
export TEMP_DIR=test/tmp

export PROXY_TEST_FILE1=$TEST_FILES_DIR/01.txt
export PROXY_TEST_FILE2=$TEST_FILES_DIR/02.txt
export PROXY_TEST_FILE3=$TEST_FILES_DIR/03.jpg

export PROXY_TEST_FILE_LEN1=$(stat --print="%s" $PROXY_TEST_FILE1)
export PROXY_TEST_FILE_LEN2=$(stat --print="%s" $PROXY_TEST_FILE2)
export PROXY_TEST_FILE_LEN3=$(stat --print="%s" $PROXY_TEST_FILE3)

export PROXY_OUTPUT_FILE1=$TEMP_DIR/01.txt
export PROXY_OUTPUT_FILE2=$TEMP_DIR/02.txt
export PROXY_OUTPUT_FILE3=$TEMP_DIR/03.jpg

export DEVICE_ZERO=/dev/zero
export DEVICE_RANDOM=/dev/random

set -e

make
insmod build/blk_comp.ko

modprobe brd rd_nr=1 rd_size=307200 max_part=0

echo -n /dev/ram0 > /sys/module/blk_comp/parameters/mapper
echo -n unmap > /sys/module/blk_comp/parameters/unmapper

rmmod blk_comp

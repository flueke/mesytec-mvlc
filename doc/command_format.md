vme_read amod data_width address ['late']

vme_block_read amod transfers address [esst_rate]
vme_block_read_swapped amod transfers address [esst_rate]

vme_block_read_mem amod transfers address
vme_block_read_mem_swapped amod transfers address

vme_write amod data_width address value
write_marker value

wait cycles:24

signal_accu
mask_shift_accu mask shift
set_accu value
read_to_accu amod data_width address ['late']
compare_loop_accu cmp value

software_delay delay_ms

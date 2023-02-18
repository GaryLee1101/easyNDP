
#ifndef IO_CMD_H_
#define IO_CMD_H_


// int handle_dram_flash_write(int logicaladdress);
unsigned int handle_dram_flash_read(unsigned int lpn);
int handle_DMA_read_host(unsigned int devAddr,unsigned int cmdSlotTag);

#endif

#include "io_cmd.h"
#include "lru_buffer.h"
#include "low_level_scheduler.h"
#include "memory_map.h"



unsigned int handle_dram_flash_read(unsigned int lpn){
	unsigned int bufferEntry, dataPageAddr;

	bufferEntry = CheckBufHit(lpn);
	if (bufferEntry != 0x7fff)  // hit
		return BUFFER_ADDR + bufferEntry * BUF_ENTRY_SIZE;
	else{  // miss, need to read from flash
		bufferEntry = AllocateBufEntry(lpn);
		bufMap->bufEntry[bufferEntry].dirty = 0;

		//link
		unsigned int dieNo = lpn % DIE_NUM;
		unsigned int dieLpn = lpn / DIE_NUM;
		if(bufLruList->bufLruEntry[dieNo].head != 0x7fff)
		{
			bufMap->bufEntry[bufferEntry].prevEntry = 0x7fff;
			bufMap->bufEntry[bufferEntry].nextEntry = bufLruList->bufLruEntry[dieNo].head;
			bufMap->bufEntry[bufLruList->bufLruEntry[dieNo].head].prevEntry = bufferEntry;
			bufLruList->bufLruEntry[dieNo].head = bufferEntry;
		}
		else
		{
			bufMap->bufEntry[bufferEntry].prevEntry = 0x7fff;
			bufMap->bufEntry[bufferEntry].nextEntry = 0x7fff;
			bufLruList->bufLruEntry[dieNo].head = bufferEntry;
			bufLruList->bufLruEntry[dieNo].tail = bufferEntry;
		}
		bufMap->bufEntry[bufferEntry].lpn = lpn;

		LOW_LEVEL_REQ_INFO lowLevelCmd;
		if (pageMap->pmEntry[dieNo][dieLpn].ppn != 0xffffffff)
		{
			lowLevelCmd.rowAddr = pageMap->pmEntry[dieNo][dieLpn].ppn;
			lowLevelCmd.spareDataBuf = SPARE_ADDR;
			lowLevelCmd.devAddr = BUFFER_ADDR + bufferEntry * BUF_ENTRY_SIZE;
			lowLevelCmd.chNo = dieNo % CHANNEL_NUM;
			lowLevelCmd.wayNo = dieNo / CHANNEL_NUM;
			lowLevelCmd.bufferEntry = bufferEntry;
			lowLevelCmd.request = V2FCommand_ReadPageTrigger;
			lowLevelCmd.search = 0;
			lowLevelCmd.task_index = 0;
			PushToReqQueue(&lowLevelCmd);
			reservedReq = 1;

			while (ExeLowLevelReqPerCh(lowLevelCmd.chNo, REQ_QUEUE)) ;

			return lowLevelCmd.devAddr;
		}
		else{
			xil_printf("[Warning] lpn %d not has ppn!\r\n", lpn);
			return 0xffffffff;
		}
	}
}

int handle_DMA_read_host(unsigned int devAddr,unsigned int cmdSlotTag){
	
}


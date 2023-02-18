//////////////////////////////////////////////////////////////////////////////////
// nvme_io_cmd.c for Cosmos+ OpenSSD
// Copyright (c) 2016 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Youngjin Jo <yjjo@enc.hanyang.ac.kr>
//				  Sangjin Lee <sjlee@enc.hanyang.ac.kr>
//				  Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//
// This file is part of Cosmos+ OpenSSD.
//
// Cosmos+ OpenSSD is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// Cosmos+ OpenSSD is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Cosmos+ OpenSSD; see the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Company: ENC Lab. <http://enc.hanyang.ac.kr>
// Engineer: Sangjin Lee <sjlee@enc.hanyang.ac.kr>
//			 Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: NVMe IO Command Handler
// File Name: nvme_io_cmd.c
//
// Version: v1.0.1
//
// Description:
//   - handles NVMe IO command
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.1
//   - header file for buffer is changed from "ia_lru_buffer.h" to "lru_buffer.h"
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////


#include "xil_printf.h"
#include "debug.h"
#include "io_access.h"
#include "xtime_l.h"
#include "string.h"

#include "nvme.h"
#include "host_lld.h"
#include "nvme_io_cmd.h"
#include "../memory_map.h"
#include "../lru_buffer.h"
#include "../fiemap.h"
#include "../low_level_scheduler.h"

#include "../command_list.h"

void handle_nvme_io_read(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd, unsigned int* printIOinfo)
{
	IO_READ_COMMAND_DW12 readInfo12;
	//IO_READ_COMMAND_DW13 readInfo13;
	//IO_READ_COMMAND_DW15 readInfo15;
	unsigned int startLba[2];
	unsigned int nlb;


	readInfo12.dword = nvmeIOCmd->dword[12];
	//readInfo13.dword = nvmeIOCmd->dword[13];
	//readInfo15.dword = nvmeIOCmd->dword[15];

	startLba[0] = nvmeIOCmd->dword[10];
	startLba[1] = nvmeIOCmd->dword[11];
	nlb = readInfo12.NLB;

	if(*printIOinfo)
		xil_printf("IO read: %d  nlb:%d\r\n",startLba[0],nlb);

	ASSERT(startLba[0] < storageCapacity_L && (startLba[1] < STORAGE_CAPACITY_H || startLba[1] == 0));
	//ASSERT(nlb < MAX_NUM_OF_NLB);
	ASSERT((nvmeIOCmd->PRP1[0] & 0xF) == 0 && (nvmeIOCmd->PRP2[0] & 0xF) == 0); //error
	ASSERT(nvmeIOCmd->PRP1[1] < 0x10 && nvmeIOCmd->PRP2[1] < 0x10);

	HOST_REQ_INFO hostCmd;
	hostCmd.curSect = startLba[0];
	hostCmd.reqSect = nlb + 1;
	hostCmd.cmdSlotTag = cmdSlotTag;
	hostCmd.task_index = 0;

	LRUBufRead(&hostCmd);
}


void handle_nvme_io_write(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd, unsigned int* printIOinfo)
{
	IO_READ_COMMAND_DW12 writeInfo12;
	//IO_READ_COMMAND_DW13 writeInfo13;
	//IO_READ_COMMAND_DW15 writeInfo15;
	unsigned int startLba[2];
	unsigned int nlb;

	writeInfo12.dword = nvmeIOCmd->dword[12];
	//writeInfo13.dword = nvmeIOCmd->dword[13];
	//writeInfo15.dword = nvmeIOCmd->dword[15];

//	if(writeInfo12.FUA == 1)
//		xil_printf("write FUA\r\n");


	if(writeInfo12.reserved0 == TEST_C){ //for test task list
		if(DEBUGMODE)
			xil_printf("Interupted! rsv:%X\r\n",writeInfo12.reserved0);

		set_auto_rx_dma(cmdSlotTag, 0, INTEGRATE_BLOCK_ADDR);
		while(!check_auto_rx_dma_partial_done(g_hostDmaStatus.fifoTail.autoDmaRx, g_hostDmaAssistStatus.autoDmaRxOverFlowCnt));

		unsigned int* sector_num = (unsigned int*)INTEGRATE_BLOCK_ADDR;
		unsigned int* sector_count = sector_num + 1;
		unsigned int* data_size = sector_count + 1;
		if(DEBUGMODE)
			xil_printf("block address:%u block count:%u data size:%u\r\n",*sector_num,*sector_count,*data_size);

		if((*data_size + SECTOR_SIZE_FTL -1) / SECTOR_SIZE_FTL != *sector_count)
			xil_printf("invalid request, size_in_byte and sector_count unmatching\r\n");

		//insert to task list
		struct task_entry task;
		memset(&task, 0, sizeof(struct task_entry));

		task.task_type = TEST;
		task.task_status = INIT;
		task.start_sector = *sector_num;
		task.sector_count = *sector_count;
		task.data_size = *data_size;
		task.cmdSlotTag = cmdSlotTag;

		if(insert_tasklist(&task) == -1){
			if(DEBUGMODE)
				xil_printf("task insert failed!\r\n");
			set_auto_nvme_cpl(cmdSlotTag, 0, 0);
		}
		return;
	}

	if(writeInfo12.reserved0 == CLEAR_C){ //return all request
		if(DEBUGMODE)
			xil_printf("Interupted! rsv:%X\r\n",writeInfo12.reserved0);

		//we need clear command only if we stocked by bug, so we scan the whole task list.
		for(int i=0; i<TASK_LIST_SIZE; i++){
			if(task_list[i].task_type == NOTASK)
				continue;

			if(task_list[i].cmdSlotTag != 0)
				set_auto_nvme_cpl(task_list[i].cmdSlotTag, 0, 0); //avoid host stocked
			reset_task(i);
		}
		//reset all variable
		memset(task_list, 0, sizeof(struct task_entry)*TASK_LIST_SIZE);
		memset(task_buffer_list, 0, sizeof(struct task_buffer_entry)*TASK_LIST_SIZE);
		current_task = 0;
		allocate_start = 0;

		return;
	}

	if(writeInfo12.reserved0 == DEBUGON_C){ //for turn on DEBUGMODE
		xil_printf("DEBUGMODE On!\r\n");
		DEBUGMODE = 1;
		set_auto_nvme_cpl(cmdSlotTag, 0, 0);
		return;
	}

	if(writeInfo12.reserved0 == DEBUGOFF_C){ //for turn off DEBUGMODE
		xil_printf("DEBUGMODE Off!\r\n");
		DEBUGMODE = 0;
		set_auto_nvme_cpl(cmdSlotTag, 0, 0);
		return;
	}

	if(writeInfo12.reserved0 == DIRSEARCH_C){ //for DIR
		if(DEBUGMODE)
			xil_printf("Interupted! rsv:%X\r\n",writeInfo12.reserved0);

		set_auto_rx_dma(cmdSlotTag, 0, INTEGRATE_BLOCK_ADDR);
		while(!check_auto_rx_dma_partial_done(g_hostDmaStatus.fifoTail.autoDmaRx, g_hostDmaAssistStatus.autoDmaRxOverFlowCnt));

		unsigned int* sector_num = (unsigned int*)INTEGRATE_BLOCK_ADDR;
		unsigned int* sector_count = sector_num + 1;
		unsigned int* size_in_byte = sector_count + 1;
		if(DEBUGMODE)
			xil_printf("block address:%u block count:%u size:%u B\r\n",*sector_num,*sector_count,*size_in_byte);

		//check if size_in_byte could matching sector_count
		if((*size_in_byte + SECTOR_SIZE_FTL -1) / SECTOR_SIZE_FTL != *sector_count){
			xil_printf("invalid request, size_in_byte and sector_count unmatching\r\n");
			set_auto_nvme_cpl(cmdSlotTag, 0, 0);
			return;
		}

		//insert to task list
		struct task_entry task;
		memset(&task, 0, sizeof(struct task_entry));

		task.task_type = DIR;
		task.task_status = INIT;
		task.start_sector = *sector_num;
		task.sector_count = *sector_count;
		task.data_size = *size_in_byte;
		task.cmdSlotTag = cmdSlotTag;

		if(insert_tasklist(&task) == -1){
			if(DEBUGMODE)
				xil_printf("task insert failed!\r\n");
			set_auto_nvme_cpl(cmdSlotTag, 0, 0);
		}
		return;
	}

	if(writeInfo12.reserved0 == FIEMAPSEARCH_C){ //for single fiemap
		if(DEBUGMODE)
			xil_printf("Interupted! rsv:%X\r\n",writeInfo12.reserved0);

		//receive 1st block of dma data
		unsigned int devAddr = INTEGRATE_BLOCK_ADDR;
		memset(devAddr, 0, 4096);
		set_auto_rx_dma(cmdSlotTag, 0, devAddr); // we only receive one block here, 4096B could support 72 extent in maximum, enough for our test
		while(!check_auto_rx_dma_partial_done(g_hostDmaStatus.fifoTail.autoDmaRx, g_hostDmaAssistStatus.autoDmaRxOverFlowCnt));

		//we assume that it only need one dma
		if(DEBUGMODE)
			xil_printf("Received fiemap\r\n");

		struct fiemap* map = (struct fiemap*)devAddr;
		if(map->fm_reserved != 0xAE86){
			if(DEBUGMODE)
				xil_printf("Magic number:%X error!\r\n",map->fm_reserved);
			set_auto_nvme_cpl(cmdSlotTag, 0, 0);
		}
		else{
			if(DEBUGMODE)
				xil_printf("Magic number:%X correct!\r\n",map->fm_reserved);

			//insert a matching task as the parent matching task
			struct task_entry task;
			memset(&task, 0, sizeof(struct task_entry));

			task.task_type = MATCHING;
			task.task_status = READING; //don't need distribute
			task.cmdSlotTag = cmdSlotTag;

			unsigned int parent_index = insert_tasklist(&task);
			if(parent_index == -1){
				if(DEBUGMODE)
					xil_printf("task insert failed!\r\n");
				set_auto_nvme_cpl(cmdSlotTag, 0, 0);
				return;
			}

			//get all fiemap
			unsigned int extent_num = map->fm_mapped_extents;

			for(int i=0; i<extent_num; i++){
				struct fiemap_extent* temp_extent = map->fm_extents+i;
				if(DEBUGMODE){
					xil_printf("extent: %d\r\n",i);
					printf("logical start:%llu\r\n",temp_extent->fe_logical / SECTOR_SIZE_FTL);
					printf("physical start:%llu\r\n",temp_extent->fe_physical / SECTOR_SIZE_FTL);
					printf("length:%llu\r\n",(temp_extent->fe_length + SECTOR_SIZE_FTL - 1) / SECTOR_SIZE_FTL);
				}

				//insert matching
				memset(&task, 0, sizeof(struct task_entry));

				task.task_type = MATCHING;
				task.task_status = INIT; //don't need distribute
				task.start_sector = temp_extent->fe_physical / SECTOR_SIZE_FTL;
				task.sector_count = (temp_extent->fe_length + SECTOR_SIZE_FTL - 1) / SECTOR_SIZE_FTL;
				task.parent_task_addone = parent_index+1;
				task_list[parent_index].child_task_count++;

				if(insert_tasklist(&task) == -1){
					if(DEBUGMODE)
						xil_printf("task insert failed!\r\n");
					task_list[parent_index].child_task_dispatched++;
					task_list[parent_index].child_task_finished++;
					break; //we can't set nvme complete here, because previous dispatched task maybe orphan, we still need to handle these tasks.
				}
				else{
					task_list[parent_index].child_task_dispatched++;
				}
			}
		}

		return;
	}

	startLba[0] = nvmeIOCmd->dword[10];
	startLba[1] = nvmeIOCmd->dword[11];
	nlb = writeInfo12.NLB;

	if(*printIOinfo)
		xil_printf("IO write: %d  nlb:%d\r\n",startLba[0],nlb);

	ASSERT(startLba[0] < storageCapacity_L && (startLba[1] < STORAGE_CAPACITY_H || startLba[1] == 0));
	//ASSERT(nlb < MAX_NUM_OF_NLB);
	ASSERT((nvmeIOCmd->PRP1[0] & 0xF) == 0 && (nvmeIOCmd->PRP2[0] & 0xF) == 0);
	ASSERT(nvmeIOCmd->PRP1[1] < 0x10 && nvmeIOCmd->PRP2[1] < 0x10);

	HOST_REQ_INFO hostCmd;
	hostCmd.curSect = startLba[0];
	hostCmd.reqSect = nlb + 1;
	hostCmd.cmdSlotTag = cmdSlotTag;
	hostCmd.task_index = 0;

	LRUBufWrite(&hostCmd);
}

void handle_nvme_io_cmd(NVME_COMMAND *nvmeCmd, unsigned int* printIOinfo)
{
	NVME_IO_COMMAND *nvmeIOCmd;
	NVME_COMPLETION nvmeCPL;
	unsigned int opc;

	nvmeIOCmd = (NVME_IO_COMMAND*)nvmeCmd->cmdDword;
	opc = (unsigned int)nvmeIOCmd->OPC;

	switch(opc)
	{
		case 0xA:  // fopen task
		{
			xil_printf("fopen Command\r\n");
			// set_auto_rx_dma(nvmeCmd->cmdSlotTag, 0, FS_FOPEN_PATH_ADDR);
			// unsigned int ino = search_keyword();

			// unsigned int result_addr = FS_FOPEN_PATH_ADDR + 4096;
			// *((unsigned int *)result_addr) = ino;
			// set_auto_tx_dma(nvmeCmd->cmdSlotTag, 0, result_addr, 1);

			set_auto_nvme_cpl(nvmeCmd->cmdSlotTag, 0, 0);
			break;
		}
		case IO_NVM_FLUSH:
		{
			xil_printf("IO Flush Command\r\n");
			nvmeCPL.dword[0] = 0;
			nvmeCPL.specific = 0x0;
			set_auto_nvme_cpl(nvmeCmd->cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
			break;
		}
		case IO_NVM_WRITE:
		{
			//xil_printf("IO Write Command\r\n");
			handle_nvme_io_write(nvmeCmd->cmdSlotTag, nvmeIOCmd, printIOinfo);
			break;
		}
		case IO_NVM_READ:
		{
			//xil_printf("IO Read Command\r\n");
			handle_nvme_io_read(nvmeCmd->cmdSlotTag, nvmeIOCmd, printIOinfo);
			break;
		}
		default:
		{
			xil_printf("Not Support IO Command OPC: %X\r\n", opc);
			//ASSERT(0);
			break;
		}
	}
}


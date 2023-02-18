//////////////////////////////////////////////////////////////////////////////////
// internal_req.h for Cosmos+ OpenSSD
// Copyright (c) 2016 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
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
// Engineer: Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: Flash Translation Layer
// File Name: internal_req.h
//
// Version: v1.3.0
//
// Description:
//   - define the request format of FTL
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.3.0
//   - Index for operating sequence of NVMe DMA and NAND operation is deleted
//
// * v1.2.0
//   - DMA check option is deleted
//
// * v1.1.0
//   - data structure for requests are re-defined
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////



#ifndef	INTERNAL_REQ_H_
#define	INTERNAL_REQ_H_

typedef struct _HOST_REQ_INFO
{
	unsigned int curSect;
	unsigned int reqSect;
	unsigned int cmdSlotTag;
	unsigned int task_index; //task index in task list, note that the value is added by one to distinguish it from invalid
}HOST_REQ_INFO, *P_HOST_REQ_INFO;

typedef struct _BUFFER_REQ_INFO
{
	unsigned int lpn;
	unsigned int devAddr;
	unsigned int cmdSlotTag : 16;
	unsigned int startDmaIndex : 16;
	unsigned int subReqSect : 8;
	unsigned int bufferEntry : 16;
	unsigned int reserved	: 8;
}BUFFER_REQ_INFO, *P_BUFFER_REQ_INFO;

typedef struct _LOW_LEVEL_REQ_INFO
{
	unsigned int rowAddr;  //Physical Page Number (PPN)
	unsigned int devAddr;
	unsigned int spareDataBuf;
	unsigned int cmdSlotTag : 16;
	unsigned int startDmaIndex : 16;
	unsigned int chNo : 4;
	unsigned int wayNo : 4;
	unsigned int subReqSect : 8;
	unsigned int bufferEntry : 16;
	unsigned int request : 16;

	unsigned int search : 1;  // to judge whether this entry is a regular or a search entry
	unsigned int searchBufferEntry : 15;  // identifies the buffer entry to which this entry belongs
	unsigned int searchPageIndex : 16;
	unsigned int task_index : 16; //index of task list + 1

	unsigned int offset_in_task : 16; //start from 0
	unsigned int sector_start : 2; //the offset of useful sector in this page
	unsigned int sector_count : 3; //the number of useful sector in this page(because count would be 4,so it need 3 bits)
	unsigned int reserved : 11;
}LOW_LEVEL_REQ_INFO, *P_LOW_LEVEL_REQ_INFO;

#endif	/* INTERNAL_REQ_H_ */

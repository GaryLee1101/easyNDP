#include "task_handler.h"
#include "memory_map.h"
#include "search.h"
#include "nvme/debug.h"
#include "nvme/host_lld.h"
#include "low_level_scheduler.h"
#include "nvme/debug.h"

void init_tasklist(){
	task_list = (struct task_entry*)TASK_ARRAY;
	task_buffer_list = (struct task_buffer_entry*)TASK_BUFFER;
	memset(task_list, 0, sizeof(struct task_entry)*TASK_LIST_SIZE);
	memset(task_buffer_list, 0, sizeof(struct task_buffer_entry)*TASK_LIST_SIZE);
	current_task = 0;
	allocate_start = 0;
	DEBUGMODE = 1;

	memcpy(task_argument,"hello\0",6);
}

void reset_task(unsigned int index){
	current_task--;
	memset(task_list + index, 0, sizeof(struct task_entry));
	if(index < allocate_start){
		allocate_start = index;
	}
}

//find an empty slot to insert task
int insert_tasklist(struct task_entry* input){
	if(current_task >= TASK_LIST_SIZE){
		xil_printf("insert failed!too many tasks\r\n");
		return -1;
	}

	for(int i=allocate_start; i<TASK_LIST_SIZE; i++){
		if(task_list[i].task_type == NOTASK){
			memcpy(task_list+i, input, sizeof(struct task_entry));
			if(DEBUGMODE)
				xil_printf("insert to task list:%u\r\n",i);
			allocate_start = i+1;
			current_task++;
			return i;
		}
	}

	return -1;
}


//distribute flash command for a whole request
void distribute_flash_read(unsigned int index){
    LOW_LEVEL_REQ_INFO lowLevelCmd;
    unsigned int tempLpn;
    unsigned int start_sector;
    unsigned int offset_in_task = 0;
    unsigned int offset_in_page, useful_sector;
    unsigned int left_sector = task_list[index].sector_count;
    while (left_sector > 0){
    	start_sector = task_list[index].start_sector + offset_in_task;
    	offset_in_page = start_sector % 4;
    	if(offset_in_page + left_sector > 4){ //need next read
    		useful_sector = 4-offset_in_page;
    	}
    	else{
    		useful_sector = left_sector;
    	}
    	left_sector -= useful_sector;

    	tempLpn = start_sector / 4;
        unsigned int dieNo = tempLpn % DIE_NUM;
        unsigned int dieLpn = tempLpn / DIE_NUM;
        if(pageMap->pmEntry[dieNo][dieLpn].ppn != 0xffffffff){
            lowLevelCmd.rowAddr = pageMap->pmEntry[dieNo][dieLpn].ppn;
            lowLevelCmd.spareDataBuf = SPARE_ADDR;
            lowLevelCmd.chNo = dieNo % CHANNEL_NUM;
            lowLevelCmd.wayNo = dieNo / CHANNEL_NUM;
            lowLevelCmd.request = V2FCommand_ReadPageTrigger;

            lowLevelCmd.search = 0;
            lowLevelCmd.task_index = index+1;
            lowLevelCmd.offset_in_task = offset_in_task;
            lowLevelCmd.sector_start = offset_in_page;
            lowLevelCmd.sector_count = useful_sector;
            PushToReqQueue(&lowLevelCmd);

            offset_in_task += useful_sector;
        }
        else{
            xil_printf("lpn %d not has ppn!\r\n", tempLpn);
        }
    }

    reservedReq = 1;
}

//for FLASHREAD task distribute, which has only one page
int distribute_flash_read_simple(unsigned int index){
	LOW_LEVEL_REQ_INFO lowLevelCmd;
	unsigned int tempLpn = task_list[index].start_sector / 4;
	unsigned int dieNo = tempLpn % DIE_NUM;
	unsigned int dieLpn = tempLpn / DIE_NUM;
	if(pageMap->pmEntry[dieNo][dieLpn].ppn != 0xffffffff){
		lowLevelCmd.rowAddr = pageMap->pmEntry[dieNo][dieLpn].ppn;
		lowLevelCmd.spareDataBuf = SPARE_ADDR;
		lowLevelCmd.chNo = dieNo % CHANNEL_NUM;
		lowLevelCmd.wayNo = dieNo / CHANNEL_NUM;
		lowLevelCmd.request = V2FCommand_ReadPageTrigger;

		lowLevelCmd.search = 0;
		lowLevelCmd.task_index = index+1;
		lowLevelCmd.offset_in_task = task_list[index].offset_in_parent;
		lowLevelCmd.sector_start = task_list[index].start_sector % 4;
		lowLevelCmd.sector_count = task_list[index].sector_count;
		PushToReqQueue(&lowLevelCmd);

		reservedReq = 1;

		return 0;
	}
	else{
		xil_printf("lpn %d not has ppn!\r\n", tempLpn);
		return 1;
	}
}

//generate child task for non-flash task
void generate_child_task(unsigned int index){
	if(task_list[index].sector_count == 0){
		xil_printf("sector count can't be zero!\r\n");
		task_list[index].task_status = READING;
		return;
	}

	unsigned int first_page = task_list[index].start_sector / 4;
	unsigned int end_page = (task_list[index].start_sector + task_list[index].sector_count - 1) / 4;
	if(task_list[index].child_task_count == 0){ //if it is the first time generate child task
		task_list[index].child_task_count = end_page - first_page + 1;
	}

	unsigned int remain_data_size = task_list[index].data_size;
	if(remain_data_size==0) //if task did't indicate data size, then we set data size as the whole sector size.
		remain_data_size = task_list[index].sector_count*SECTOR_SIZE_FTL;

	for(int i=task_list[index].child_task_dispatched; i<task_list[index].child_task_count; i++){
		//calculate the start and sector count inside a page
		unsigned int sector_start;
		unsigned int sector_count;
		if(i == 0){ //if this is the first page
			sector_start = task_list[index].start_sector % 4;
			sector_count = 4 - sector_start;
			if(sector_count > task_list[index].sector_count) //if it have only one page, then we may don't need the whole page
				sector_count = task_list[index].sector_count;
		}
		else if(i == task_list[index].child_task_count-1){ //if this is the last page
			sector_start = 0;
			sector_count = ((task_list[index].start_sector + task_list[index].sector_count - 1) % 4) + 1;
		}
		else{ //if this is the middle page
			sector_start = 0;
			sector_count = 4;
		}

		//modify data_size
		unsigned int data_size = sector_count*SECTOR_SIZE_FTL;
		if(data_size > remain_data_size)
			data_size = remain_data_size;
		remain_data_size -= data_size;

		//generate new task
		struct task_entry task;
		memset(&task, 0, sizeof(struct task_entry));

		task.task_type = FLASHREAD;
		task.task_status = INIT;
		task.start_sector = (first_page+i)*4 + sector_start;
		task.sector_count = sector_count;
		task.data_size = data_size;

		task.parent_task_addone = index + 1;
		task.offset_in_parent = i;

		//insert new task, if success then add dispatched child task
		if(insert_tasklist(&task)){
			task_list[index].child_task_dispatched++;
			//if dispatched all task, then change the status of this task into READING
			if(task_list[index].child_task_dispatched >= task_list[index].child_task_count){
				task_list[index].task_status = READING;
				return;
			}
		}
		else{ //if failed then exit to waiting for next task list check
			return;
		}
	}
}

//check if there are task need to handle
void check_task_list(){
	unsigned int index = 0;
	unsigned int temp_current_task = current_task; 	//check task list may change the current task list.
													//for example we may add child task in front of i, the current_task will add 1 but we can't reach it in this iteration so the while loop can't stop.
													//so we set a variable to save the origin task count to ensure we have temp_current_task to handle at least.
													//but it insert a new task before final valid task, the valid task will not handle in this iteration which may cause hungry if insert frequently.
	for(int i=0; i<temp_current_task; i++){
		while(task_list[index].task_type == NOTASK){
			index++;
		} //find next valid task

		struct task_entry temp = task_list[index]; //just for debug
		//handle new insert task, mainly for send flash request
		if(task_list[index].task_type == TEST){ //It's an NDP application develop example
			if(task_list[index].task_status == INIT){ //1.First when we init we need to distribute flash command, this function will change status into READING if all distribute, otherwise it will continue to distribute in next iteration.
				generate_child_task(index); //generate child task, it will change status automatically if generate all child task
			}
			if(task_list[index].task_status == READING){ //2. After all child task have been dispatched, we check whether all child task have been finished
				if(task_list[index].child_task_count <= task_list[index].child_task_finished){
					if(DEBUGMODE)
						xil_printf("task %u finished\r\n",index);
					set_auto_nvme_cpl(task_list[index].cmdSlotTag, 0, 0); //if finished we return to host
					reset_task(index);
				}
			}
		}

		if(task_list[index].task_type == DIR){
			if(task_list[index].task_status == INIT){
				generate_child_task(index); //we did not consider task list full status, which may cause bug.
			}
			if(task_list[index].task_status == READING){
				if(task_list[index].child_task_count <= task_list[index].child_task_finished){
					if(DEBUGMODE)
						xil_printf("task %u finished\r\n",index);
					set_auto_nvme_cpl(task_list[index].cmdSlotTag, 0, 0); //if finished we return to host
					reset_task(index);
				}
			}
		}

		if(task_list[index].task_type == FLASHREAD){ //Check if there are FLASHREAD not dispatched
			if(task_list[index].task_status == INIT){
				if(!distribute_flash_read_simple(index)){ //if success
					task_list[index].task_status = READING;
				}
				else{ //if distribute failed
					if(task_list[index].parent_task_addone) //if have parent task
						task_list[task_list[index].parent_task_addone-1].child_task_finished++;
					reset_task(index);
				}
			}
		}

		if(task_list[index].task_type == MATCHING){
			if(task_list[index].task_status == INIT){
				generate_child_task(index);
			}
			if(task_list[index].task_status == READING){
				if(task_list[index].child_task_count <= task_list[index].child_task_finished){
					if(DEBUGMODE)
						xil_printf("task %u finished\r\n",index);
					if(task_list[index].parent_task_addone){ //if this is a sub matching task
						task_list[task_list[index].parent_task_addone-1].child_task_finished++;
					}
					else{
						set_auto_nvme_cpl(task_list[index].cmdSlotTag, 0, 0);
					}
					reset_task(index);
				}
			}
		}

		//set all task done and return, to avoid host blocked
		if(task_list[index].task_type == CLEARTASK){
			unsigned int need_clear_task_count = current_task; //save the task count to avoid current_task modify.
			unsigned int clear_index = 0;
			for(int i=0; i<need_clear_task_count; i++){
				while(task_list[clear_index].task_type == NOTASK){
					clear_index++;
				} //find next valid task

				set_auto_nvme_cpl(task_list[clear_index].cmdSlotTag, 0, 0);
				reset_task(clear_index);
			}
			break;
		}

		index++;
	}
}

//called when data read
int handle_task(unsigned int index, unsigned int offset_in_task, unsigned int sector_start, unsigned int sector_count, unsigned int bufAddr){
	if(index >= TASK_LIST_SIZE){
		xil_printf("index overflow:%u\r\n",index);
		return -1;
	}
	if(task_list[index].task_type == PRINTALL){
		xil_printf("%s\r\n",(char*)bufAddr);
		reset_task(index);
	}
	else if(task_list[index].task_type == MATCHING){
		//for return
		set_auto_nvme_cpl(task_list[index].cmdSlotTag, 0, 0);
		reset_task(index);
	}
	else if(task_list[index].task_type == DIR){
		if(task_list[index].task_status == READINGADDR){ //get all block address

		}

		reset_task(index);
	}
	else if(task_list[index].task_type == TEST){
		if(task_list[index].task_status == READING){ //we won't get into these code
			//handle data
			if(DEBUGMODE){
				xil_printf("handling data of task %u, offset:%u start:%u count:%u\r\n",index,offset_in_task,sector_start,sector_count);
				xil_printf("data content in string:%s\r\n",task_buffer_list[index].buffer+4096*sector_start);
			}

			if(task_list[index].finished_sector >= task_list[index].sector_count){
				if(DEBUGMODE)
					xil_printf("task %u finished\r\n",index);
				set_auto_nvme_cpl(task_list[index].cmdSlotTag, 0, 0);
				reset_task(index);
			}
		}
	}
	else if(task_list[index].task_type == FLASHREAD){
		if(task_list[index].task_status == READING){
			//don't need to check if sector all read, each FLASHREAD task only responsible to one page read
			//handle data
			if(DEBUGMODE){
				xil_printf("handling data of task %u, offset:%u start:%u count:%u size:%u parent:%u\r\n",index,offset_in_task,sector_start,sector_count,task_list[index].data_size,task_list[index].parent_task_addone-1);
				//xil_printf("data content in string:%s\r\n",task_buffer_list[index].buffer+4096*sector_start);
			}

			//handle data according to parent task
			if(task_list[index].parent_task_addone){
				if(task_list[task_list[index].parent_task_addone-1].task_type == MATCHING){ //if parent task is a string matching
//					unsigned int result = Sunday(task_buffer_list[index].buffer+SECTOR_SIZE_FTL*sector_start, "hello", task_list[index].sector_count*SECTOR_SIZE_FTL, 5);
//					if(DEBUGMODE)
//						xil_printf("matching result:%u\r\n",result);
				}

				if(task_list[task_list[index].parent_task_addone-1].task_type == DIR){ //if parent task is a string matching
					//separate start address and block count
					unsigned int remain_data = task_list[index].data_size;
					unsigned int data_index = 0;
					unsigned int* data_list = (unsigned int*)(task_buffer_list[index].buffer+SECTOR_SIZE_FTL*sector_start);
					struct task_entry task;
					while(remain_data){
						unsigned int start_sector = data_list[data_index++];
						unsigned int count = data_list[data_index++];
						if(DEBUGMODE)
							xil_printf("push a matching:sector %u, count %u\r\n",start_sector,count);
						//push a matching task into task list
						memset(&task, 0, sizeof(struct task_entry));
						task.task_type = MATCHING;
						task.task_status = INIT;
						task.start_sector = start_sector;
						task.sector_count = count;
						task.parent_task_addone = task_list[index].parent_task_addone;

						if(insert_tasklist(&task) == -1){
							if(DEBUGMODE)
								xil_printf("task insert failed!\r\n");
							task_list[task_list[index].parent_task_addone-1].child_task_count++;
							task_list[task_list[index].parent_task_addone-1].child_task_finished++;
							break; //we can't set nvme complete here, because previous dispatched task maybe orphan, we still need to handle these tasks.
						}
						else{
							task_list[task_list[index].parent_task_addone-1].child_task_count++;
						}

						remain_data -= 2*sizeof(unsigned int);
					}
				}

				if(task_list[task_list[index].parent_task_addone-1].task_type == TEST){ //if parent task is a test
					char* printbuffer = (char*)PRINTBUFFER;
					memset(printbuffer,0,SECTOR_SIZE_FTL*5);
					memcpy(printbuffer, task_buffer_list[index].buffer+SECTOR_SIZE_FTL*sector_start, task_list[index].data_size);
					xil_printf("data content:%s \r\n",printbuffer);
				}

				task_list[task_list[index].parent_task_addone-1].child_task_finished++;
			}

			reset_task(index);
		}
	}
	else{
		xil_printf("Not valid task type:%u\r\n",task_list[index].task_type);
	}
	return 0;
}

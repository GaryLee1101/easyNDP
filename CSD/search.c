
#include "xtime_l.h"
#include "search.h"
#include "low_level_scheduler.h"
#include "internal_req.h"
#include "page_map.h"
#include "memory_map.h"
#include "nvme/host_lld.h"

struct searchTask* searchTask;

void delay_ms(unsigned int mseconds)
{
    XTime tEnd, tCur;
    XTime_GetTime(&tCur);
    tEnd = tCur + (((XTime) mseconds) * (COUNTS_PER_SECOND / 1000));
    do
    {
        XTime_GetTime(&tCur);
    } while (tCur < tEnd);
}
void delay_us(unsigned int useconds)
{
    XTime tEnd, tCur;
    XTime_GetTime(&tCur);
    tEnd = tCur + (((XTime) useconds) * (COUNTS_PER_SECOND / 1000000));
    do
    {
        XTime_GetTime(&tCur);
    } while (tCur < tEnd);
}

void initSearchTask(){
    searchTask->searchPageNum = 0;
    searchTask->pageCompleteCount = 0;
    searchTask->taskValid = 0;
    searchTask->need_path_walk = 0;
    searchTask->totalHitCounts = 0;
}

void analysisTask(unsigned int startSec, unsigned int nlb){
    LOW_LEVEL_REQ_INFO lowLevelCmd;
    unsigned int tempLpn = startSec / 4;

    do
    {
        unsigned int dieNo = tempLpn % DIE_NUM;
        unsigned int dieLpn = tempLpn / DIE_NUM;
        if(pageMap->pmEntry[dieNo][dieLpn].ppn != 0xffffffff){
            lowLevelCmd.rowAddr = pageMap->pmEntry[dieNo][dieLpn].ppn;
            lowLevelCmd.spareDataBuf = SPARE_ADDR;
            lowLevelCmd.chNo = dieNo % CHANNEL_NUM;
            lowLevelCmd.wayNo = dieNo / CHANNEL_NUM;
            lowLevelCmd.request = V2FCommand_ReadPageTrigger;
            lowLevelCmd.search = 1;
            lowLevelCmd.searchPageIndex = searchTask->searchPageNum;
            lowLevelCmd.task_index = 0;
            PushToReqQueue(&lowLevelCmd);
        }
        else{
            xil_printf("lpn %d not has ppn!\r\n", tempLpn);
            searchTask->pageCompleteCount++;
        }

        searchTask->searchPageNum++;
    } while (4 * (++tempLpn) < startSec + nlb);

    reservedReq = 1;
}

void CheckTaskDone(){
    // for( ; searchTask->startIndex < searchTask->searchPageNum; searchTask->startIndex++){
    //     if(!searchTask->pageTable[searchTask->startIndex].done)
    //         return;
    // }
    if(searchTask->pageCompleteCount < searchTask->searchPageNum)
        return;

    XTime_GetTime(&time_end_search);

    // all the pages are done, return response to host
    NVME_COMPLETION nvmeCPL;
    nvmeCPL.dword[0] = 0x0;
    set_auto_nvme_cpl(searchTask->cmdSlotTag, 0x0, nvmeCPL.statusFieldWord);
    
    searchTask->taskValid = 0;
    xil_printf("[ search task done, total hit counts: %d ]\r\n", searchTask->totalHitCounts);

    if (searchTask->need_path_walk){
		unsigned int t_total, tUsed;
		t_total = ((time_end_search - time_start_search) * 1000000) / (COUNTS_PER_SECOND);
		tUsed = ((time_end_retrieve - time_start_retrieve) * 1000000) / (COUNTS_PER_SECOND);
		xil_printf("Total search time: %d us. Time of retrieve:  %d us.\r\n", t_total, tUsed);
    }
}

//?????????temp??????arget???????????????
int Sunday_FindIndex(char *target,char temp){
    for(int i = strlen(target) -1;i>=0;i--){
        if(target[i] == temp)
            return i;
    }
    return -1;  //??????????????????????????????????????????
}

//sunday?????????
unsigned int Sunday(char *source, char *target, unsigned int srclen, unsigned int tarlen){
    int i= 0,j = 0;
    int temp  = 0,index = -1;
	int count = 0;

    while(i<srclen){  //??????????????????
        if(source[i] == target[j]){
            if(j==tarlen-1){
                i++; j=0; count++;  // match successfully
            }
			else{
                i++;j++;
            }	
        }else{  //?????????????????????????????????
            temp = tarlen - j + i;  // source????????????????????????????????????????????????????????????
            index = Sunday_FindIndex(target,source[temp]);
            if(index==-1){ //???????????????????????????????????
                i = temp+1;
                j = 0;
            }else{  //??????????????????
                i = temp-index;
                j = 0;
            }
        }
    }
    return count;
}

void searchInPage(unsigned int pageDataBufAddr, unsigned int searchPageIndex){
    unsigned int hitCount = 0;
    // XTime tEnd, tCur;
    // XTime_GetTime(&tCur);
//    hitCount = Sunday((char*)pageDataBufAddr, searchTask->targetString);
    // XTime_GetTime(&tEnd);
	// unsigned int tUsed = ((tEnd-tCur)*1000000)/(COUNTS_PER_SECOND);
    // // xil_printf("%d us used for searching.\r\n", tUsed);
//
    delay_us(50);

    searchTask->totalHitCounts += hitCount;
    searchTask->pageCompleteCount++;
}

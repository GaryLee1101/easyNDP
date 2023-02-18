//将多个文件的extent依次发送，而不是读一个发一个

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>

#include "filefrag.h"
#include "dma.h"
#include "command_list.h"

int total_file = 0;
unsigned long total_extent = 0; 
struct fiemap* result = NULL;
unsigned long long filesize = 0;
unsigned long long total_filesize = 0;

struct timeval tmp_begin, tmp_end;
unsigned long total_get_time = 0; //统计获取extents的总耗时
unsigned long total_send_time = 0; //统计发送extents的总耗时

//统计最大extent和最大时间
int max_extent = 0;
unsigned long gettimeofmaxextent = 0;
unsigned long sendtimeofmaxextent = 0;

//统计所有请求中最长获取时间和最长发送时间
unsigned long maxgettime = 0;
unsigned long maxsendtime = 0;

int is_send = 0; //为1表示要发送，默认状态下是不发送，靠参数"-send"控制

unsigned long elapsed(struct timeval* begin, struct timeval* end){
    long seconds = end->tv_sec - begin->tv_sec;
    long microseconds = end->tv_usec - begin->tv_usec;
    return seconds*1e6 + microseconds;
}

void fetchdir(char* path,void (*fcn)(char*))
{
    char name[256];
    struct dirent* stp;
    DIR* dp;

    // 打开目录
    if((dp = opendir(path))== NULL)
    {
        fprintf(stderr,"fetchdir error%s\n",path);
        return;
    }
    while((stp = readdir(dp)) != NULL)
    {
        if(strcmp(stp->d_name,".")==0 || strcmp(stp->d_name,"..")==0)
            continue;
        if(strlen(path)+strlen(stp->d_name)+2>sizeof(name))
        {
            fprintf(stderr,"fetchdir name %s %s too long\n",path,stp->d_name);
        }
        else
        {
            sprintf(name,"%s/%s",path,stp->d_name);
            fcn(name); //递归调用
        }
    }
    closedir(dp);

}
    

void isFile(char* path)
{
    struct stat sbuf;
    if(lstat(path,&sbuf)==-1)
    {
        fprintf(stderr,"lstat error%s\n",path);
    }
    
    // 判断是否是文件
    if((sbuf.st_mode & S_IFMT) == S_IFDIR)
    { 
        fetchdir(path,isFile);        
    }
    else{
        total_file++;

        gettimeofday(&tmp_begin, 0);

        result = NULL;
        int num_extent = get_fiemap(path,&result);
        gettimeofday(&tmp_end, 0);

        unsigned long get_time = elapsed(&tmp_begin, &tmp_end);

        total_get_time += get_time;

        //更新最大extent
        int max_flag = 0;
        if(num_extent > max_extent){
            max_extent = num_extent;
            gettimeofmaxextent = get_time;
            max_flag = 1;
        }

        //更新最大获取时间
        if(get_time > maxgettime){
            maxgettime = get_time;
        }

        total_extent += num_extent;
        total_filesize += filesize; //暂时没有实现统计文件大小的功能

        // for(int i=0; i<num_extent; i++){
        //     printf("extent: %d\r\n",i);
        //     printf("logical start:%lu\r\n",(result->fm_extents+i)->fe_logical);
        //     printf("physical start:%lu\r\n",(result->fm_extents+i)->fe_physical);
        //     printf("length:%lu\r\n",(result->fm_extents+i)->fe_length);
        // }

        //printf("size:%ld extent:%d %s\n",sbuf.st_size,num_extent,path);   // 不是目录则是普通文件，直接打印文件名
    
        //send to cosmos
        if(is_send){
            //给fiemap的保留位中添加magic number标识是否是一个正确的指令
            result->fm_reserved = 0xAE86;

            gettimeofday(&tmp_begin, 0);
            int buf_size = sizeof(struct fiemap)+sizeof(struct fiemap_extent)*num_extent;
            char* buf_start = (char*)malloc(buf_size);
            memset(buf_start,0,buf_size);

            //copy extents
            memcpy(buf_start,(char*)result,buf_size);

            //printf("Start send to cosmos\n");
            //printf("send:%d, bufsize:%d\n",num_extent,buf_size);
           if(is_send){
                open_unvme();
                DMA_Send("/dev/nvme0n1",buf_start,buf_size,FIEMAPSEARCH_C);
                close_unvme();
            }
            //printf("send finish\n");
            gettimeofday(&tmp_end, 0);

            unsigned long send_time = elapsed(&tmp_begin, &tmp_end);

            total_send_time += send_time;

            if(max_flag){
                sendtimeofmaxextent = send_time;
            }

            if(send_time > maxsendtime){
                maxsendtime = send_time;
            }

            free(buf_start);
        }
        free(result);
    }

}

int main(int argc, char *argv[]){
    if(argc <= 1){
        printf("argment error, folder or file name not provided!\n");
        return 0;
    }

    if(argc == 3){
        if(strcmp(argv[2],"-send")==0){
            is_send = 1;
        }
    }

    struct timeval begin, end, tmp_begin, tmp_end;

    gettimeofday(&begin, 0);

    //遍历整个文件夹
    isFile(argv[1]);

    gettimeofday(&end, 0);

    //输出统计信息
    printf("total elapsed %lu us\n",elapsed(&begin, &end));
    printf("total get time %lu us\n",total_get_time);
    printf("total send time %lu us\n",total_send_time);
    printf("total get+send:%lu us\n",total_get_time+total_send_time);
    printf("total other time:%lu us\n",elapsed(&begin, &end) - total_get_time-total_send_time);

    printf("average elapsed %lu us\n",elapsed(&begin, &end)/total_file);
    printf("average get time %lu us\n",total_get_time/total_file);
    printf("average send time %lu us\n",total_send_time/total_file);
    printf("average get+send:%lu us\n",(total_get_time+total_send_time)/total_file);
    printf("average other time:%lu us\n",(elapsed(&begin, &end) - total_get_time-total_send_time)/total_file);

    printf("max extent %d\n",max_extent);
    printf("get time of max extent %lu us\n",gettimeofmaxextent);
    printf("send time of max extent %lu us\n",sendtimeofmaxextent);

    printf("max get time %lu us\n",maxgettime);
    printf("max send time %lu us\n",maxsendtime);

    float average_fragment = total_extent/(float)total_file;
    unsigned long average_filesize = total_filesize/total_file;
    printf("total_file:%d\ntotal_extent:%lu\naverage fragment:%f\naverage filesize:%lu\n",total_file,total_extent,average_fragment,average_filesize);
    return 0;
}
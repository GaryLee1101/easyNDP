//将多个文件的extent依次发送，而不是读一个发一个

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>

#include "filefrag.h"

FILE* fp = NULL;

int total_file = 0;
unsigned long total_extent = 0; 
struct fiemap* result = NULL;
unsigned long long filesize = 0;
unsigned long long total_filesize = 0;

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

        result = NULL;
        int num_extent = get_fiemap(path,&result);

        total_extent += num_extent;
        total_filesize += filesize; //暂时没有实现统计文件大小的功能

        for(int i=0; i<num_extent; i++){
            printf("extent: %d\r\n",i);
            printf("logical start:%lu\r\n",(result->fm_extents+i)->fe_logical);
            printf("physical start:%lu\r\n",(result->fm_extents+i)->fe_physical);
            printf("length:%lu\r\n",(result->fm_extents+i)->fe_length);

            // address_list[index++] = (result->fm_extents+i)->fe_physical / 4096;
            // address_list[index++] = ((result->fm_extents+i)->fe_length + 4095) / 4096;

            unsigned int sector_start = (result->fm_extents+i)->fe_physical / 4096;
            unsigned int sector_count = ((result->fm_extents+i)->fe_length + 4095) / 4096;
            fwrite(&sector_start,sizeof(unsigned int),1,fp);
            fwrite(&sector_count,sizeof(unsigned int),1,fp);
        }

        free(result);
    }
}

int main(int argc, char *argv[]){
    if(argc <= 1){
        printf("argment error, folder or file name not provided!\n");
        return 0;
    }

    char filename[20] = "dirfile.bin";
    //strcat(filename,argv[1]);

    fp = fopen(filename, "wb+");

    //遍历整个文件夹
    isFile(argv[1]);
    printf("fclose start\n");
    fclose(fp);

    printf("write done\n");
    return 0;
}
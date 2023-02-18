#ifndef FILEFRAG
#define FILEFRAG
#define FILEDEBUG 0

#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include "fiemap.h"

int get_fiemap(char* filename, struct fiemap** result){
    if(filename == NULL){
        printf ("File name is NULL\n");
        return -1;
    }

    /* Create input file descriptor */
    int input_fd = open(filename, O_RDWR);
    if (input_fd < 0) {
        perror ("open");
        return -1;
    }

    //为第一次ioctl创建fiemap结构，主要是用于获取有多少个extent
    struct fiemap *fiemap;
    if ((fiemap = (struct fiemap*)malloc(sizeof(struct fiemap))) == NULL) {
        printf("Out of memory allocating fiemap\n");
        return -1;
    }
    memset(fiemap, 0, sizeof(struct fiemap));

    fiemap->fm_start = 0;
    fiemap->fm_flags = 0;
    fiemap->fm_extent_count = 0;
    fiemap->fm_length = ~0;
    fiemap->fm_mapped_extents = 0;

    if (ioctl(input_fd, FS_IOC_FIEMAP, fiemap) < 0) {
        printf("fiemap ioctl() failed\n");
        return -1;
    }

    if(FILEDEBUG){
        printf("fiemap->fm_mapped_extents:%d\n",fiemap->fm_mapped_extents);
    }

    //重新根据第一次结果分配正确大小的内存空间用于接收fiemap
    int extents_size = sizeof(struct fiemap_extent)*(fiemap->fm_mapped_extents);

    if ((fiemap = (struct fiemap*)realloc(fiemap,sizeof(struct fiemap)+extents_size)) == NULL) { 
        printf("Out of memory allocating fiemap\n");
        return -1;
    }

    memset(fiemap->fm_extents, 0, extents_size);
    fiemap->fm_extent_count = fiemap->fm_mapped_extents;
    fiemap->fm_mapped_extents = 0;

    if (ioctl(input_fd, FS_IOC_FIEMAP, fiemap) < 0) {
        printf("fiemap ioctl() failed\n");
        return -1;
    }

    if(FILEDEBUG){
        printf("get fiemap success\n");
    }

    //执行成功后将fiemap的地址赋值给参数
    *result = fiemap;

    close(input_fd);

    return fiemap->fm_mapped_extents;
}

#endif
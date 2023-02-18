#ifndef DMA_H
#define DMA_H
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <err.h>
#include <sys/mman.h>
#include <linux/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef _U_TYPE
#define _U_TYPE       ///< bit size data types
typedef int8_t s8;    ///< 8-bit signed
typedef int16_t s16;  ///< 16-bit signed
typedef int32_t s32;  ///< 32-bit signed
typedef int64_t s64;  ///< 64-bit signed
typedef uint8_t u8;   ///< 8-bit unsigned
typedef uint16_t u16; ///< 16-bit unsigned
typedef uint32_t u32; ///< 32-bit unsigned
typedef uint64_t u64; ///< 64-bit unsigned
#endif                // _U_TYPE

// Global variables
static char *devname = "/dev/nvme0n1";
static int fd;

// For ioctl index send in circle
#define MAXINDEX 10000
#define MININDEX 1000
static unsigned int dma_index = MININDEX;

// static void *fromPageAlloc;
#define MAXBPIO 32
#define BLOCKSIZE 4096
#define NVME_IOCTL_ADMIN_CMD _IOWR('N', 0x41, struct nvme_admin_cmd)
#define NVME_IOCTL_IO_CMD _IOWR('N', 0x43, struct nvme_passthru_cmd)
#define NVME_IOCTL_SUBMIT_IO _IOW('N', 0x42, struct nvme_user_io)
#define DEBUG
enum nvme_io_opcode
{
    nvme_cmd_flush = 0x00,
    nvme_cmd_write = 0x01,
    nvme_cmd_read = 0x02,
    nvme_cmd_write_uncor = 0x04,
    nvme_cmd_compare = 0x05,
    nvme_cmd_write_zeroes = 0x08,
    nvme_cmd_dsm = 0x09,
    nvme_cmd_verify = 0x0c,
    nvme_cmd_resv_register = 0x0d,
    nvme_cmd_resv_report = 0x0e,
    nvme_cmd_resv_acquire = 0x11,
    nvme_cmd_resv_release = 0x15,
    nvme_cmd_copy = 0x19,
    nvme_zns_cmd_mgmt_send = 0x79,
    nvme_zns_cmd_mgmt_recv = 0x7a,
    nvme_zns_cmd_append = 0x7d,
};
struct nvme_user_io
{
    __u8 opcode;
    __u8 flags;
    __u16 control;
    __u16 nblocks;
    __u16 rsvd;
    __u64 metadata;
    __u64 addr;
    __u64 slba;
    __u32 dsmgmt;
    __u32 reftag;
    __u16 apptag;
    __u16 appmask;
};
int nvme_io(int fd, __u8 opcode, __u64 slba, __u16 nblocks, __u16 control,
            __u32 dsmgmt, __u32 reftag, __u16 apptag, __u16 appmask, void *data,
            void *metadata)
{
    struct nvme_user_io io = {
        .opcode = opcode,
        .flags = 0,
        .control = control,
        .nblocks = nblocks,
        .rsvd = 0,
        .metadata = (__u64)(uintptr_t)metadata,
        .addr = (__u64)(uintptr_t)data,
        .slba = slba,
        .dsmgmt = dsmgmt,
        .reftag = reftag,
        .appmask = apptag,
        .apptag = appmask,
    };
    // printf("opcode : %d , flags: %d, control: %d, rsvd:%d, metadata:%x, addr:%x, slba:%d, dsmgmt:%d, reftag%d, appmask:%d, apptag:%d\n",io.opcode,io.flags,io.control,io.rsvd,io.metadata,io.addr,io.slba,io.dsmgmt,io.reftag,io.appmask,io.apptag);
    return ioctl(fd, NVME_IOCTL_SUBMIT_IO, &io);
}
void open_unvme()
{
    fd = open(devname, O_RDONLY);
    if (fd < 0)
        errx(1, "open device fail");
    // fromPageAlloc = malloc(4096);
    // if (fromPageAlloc == NULL)
    //     errx(1, "can not allocate fromPageAlloc\n");
}
void close_unvme()
{
    // free(fromPageAlloc);
    close(fd);
}
// int nvme_translate_region(void *buf, u64 slba, u32 nlb, u32 config_nlb)
// {
//     unsigned int nrequests = (nlb / MAXBPIO) + !!(nlb % MAXBPIO);
//     unsigned int readOffset = 0;
//     int i;
//     int temnlb;
// #ifdef DEBUG
//     printf("WRITE nlb : %d , slba: %d, buf: %x\n", config_nlb, slba, buf);
// #endif
//     if (nvme_io(fd, nvme_cmd_write, slba, config_nlb, 0x00AE, 0, 0, 0, 0, buf, 0) < 0)
//         errx(1, "ndp write error\n");
//     for (i = 0; i < nrequests; i++)
//     {
//         temnlb = (i == nrequests - 1 && nlb % MAXBPIO) ? nlb % MAXBPIO : MAXBPIO;
// #ifdef DEBUG
//         printf("READ nlb : %d , slba: %d, buf: %x\n", temnlb, slba, buf + readOffset);
// #endif
//         if (nvme_io(fd, nvme_cmd_read, slba, temnlb, 0x00AE, 0, 0, 0, 0, (void *)buf + readOffset, 0) < 0)
//             errx(1, "ndp read error\n");
//         readOffset += MAXBPIO * BLOCKSIZE;
//     }
//     return 0;
// }

//只负责发送或者接收最多32个block的数据，分段发送由调用者负责。
void *DMA_Send(char *dev_nvme, char *buf, unsigned int buf_len, unsigned int rsrv_field)
{
    srand((unsigned)time(NULL));
    static int allocsize = 0;
    static void *result_ptr = NULL;
    int buffersize = buf_len;
    // if(result_ptr != NULL)
    //   printf("result_ptr: %x\n",*((int*)result_ptr));
    if (allocsize < buffersize) // realloc
    {
        buffersize = (buffersize / getpagesize() + 2) * getpagesize();
        //printf("realloc %d to %d\n", allocsize, buffersize);
        if (result_ptr != NULL)
            free(result_ptr);
        if (posix_memalign(&result_ptr, getpagesize(), buffersize))
            errx(1, "can not allocate memory\n");
        memset(result_ptr, 0, buffersize);
        // result_ptr=realloc(result_ptr, buffersize);
        allocsize = buffersize;
    }
    // copy from buf which user inputted to aligined buffer
    memcpy((void *)result_ptr, (void *)buf, buf_len);
    // send
    dma_index++;
    if(dma_index > MAXINDEX)
        dma_index = MININDEX;
    if (nvme_io(fd, nvme_cmd_write, dma_index, buf_len / getpagesize() + 1, rsrv_field, 0, 0, 0, 0, result_ptr, 0) < 0)
        errx(1, "ndp write error\n");

    //nvme_translate_region(result_ptr, times, 1, buf_len / getpagesize() + 1);

    return result_ptr;
}

#endif

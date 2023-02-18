#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>

//For DMA_send Test****************
#include "dma.h"
#include "command_list.h"
#define page_size 4096
#define buf_len page_size*254

unsigned long elapsed(struct timeval* begin, struct timeval* end){
    long seconds = end->tv_sec - begin->tv_sec;
    long microseconds = end->tv_usec - begin->tv_usec;
    return seconds*1e6 + microseconds;
}

int main(){
    struct timeval begin, end;
    gettimeofday(&begin, 0);

    char string_send[4096] = {0};
    unsigned int sector_num =  33793;
    unsigned int sector_count = 1;
    unsigned int data_size = 4000;
    memcpy(string_send, &sector_num, 4);
    memcpy(string_send+4, &sector_count, 4);
    memcpy(string_send+8, &data_size, 4);
    
    open_unvme();
    DMA_Send(NULL,string_send,4096,TEST_C);
    close_unvme();

    gettimeofday(&end, 0);

    printf("total elapsed %lu us\n",elapsed(&begin, &end));

    return 0;
}

// int main(){
//     char string_send[buf_len] = {0};

//     for(int i=0; i<buf_len/page_size; i++){
//         int write_count = 0;
//         while(write_count<4096){
//             sprintf(string_send+(i*page_size)+write_count,"%d",i);
//             if(i<10)
//                 write_count++;
//             else if(i<100)        
//                 write_count+=2;
//             else if(i<1000)        
//                 write_count+=3;

//             if(write_count%80 == 0)
//                 string_send[(i*page_size)+write_count-1] = '\n';
//         }
//     }

//     string_send[buf_len] = 0;

//     printf("%s\n",string_send);
//     open_unvme();
//     DMA_Send(NULL,string_send,buf_len);
//     close_unvme();

//     return 0;
// }

//For folder walk test*************
// int main(int argc, char *argv[]){
//     if(argc <= 1){
//         printf("argment error, file name not provided!\n");
//         return 0;
//     }

//     FILE *fp = NULL; /* 需要注意 */
//     fp = fopen(argv[1], "r+");
//     if (NULL == fp)
//     {
//         printf("Please open an exist file\n");
//         return -1; /* 要返回错误代码 */
//     }

//     char tmp[100] = "1111111111";
//     fwrite(tmp,1,10,fp);

//     printf("fd is %d\n",fileno(fp));

//     struct timeval begin, end;
//     gettimeofday(&begin, 0);

//     fsync(fileno(fp));

//     gettimeofday(&end, 0);

//     printf("fsync %ld us\n",end.tv_usec - begin.tv_usec);

//     fclose(fp);
//     fp = NULL; /* 需要指向空，否则会指向原打开文件地址 */
//     return 0;
// }
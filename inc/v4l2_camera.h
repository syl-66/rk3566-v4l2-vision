#ifndef V4L2_CAMERA_H
#define V4L2_CAMERA_H

#include <stddef.h>
/* 定义结构体，记录缓冲区的映射信息*/
struct buffer{
    void *start;    //虚拟地址指针
    size_t length;  //缓冲区的大小
};

/* 函数声明：初始化摄像头，申请并映射缓冲区 */
int camera_init(const char *dev_name,int *fd_out,struct buffer **buffers,int *buffer_count);

/* 函数声明：开启视频流 */
int camera_start(int fd,int buffer_count);

/* 函数声明：抓取一帧画面 */
int camera_get_frame(int fd,struct buffer *buffers,void **data,size_t *size);

/* 函数声明：停止并清理资源 */
void camera_stop_and_cleanup(int fd,struct buffer *buffers,int buffer_count);
#endif
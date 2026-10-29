#include <stdio.h>      // 标准输入输出，为了用 printf 打印信息
#include <stdlib.h>     // 标准库，为了用 exit 退出
#include <fcntl.h>      // 文件控制，为了用 open 函数和 O_RDWR 宏
#include <unistd.h>     // Unix 标准库，为了用 close 函数
#include <string.h>     // 字符串操作，为了用 memset 清空结构体
#include <sys/ioctl.h>  // 底层设备控制，为了用 ioctl 发送指令
#include <linux/videodev2.h> // V4L2 的核心大字典，包含了所有摄像头的结构体和宏
#include <sys/mman.h>   //使用mmap函数

int main(){
    /* 指定 V4L2 视频设备节点路径（主通道） */
    const char *dev_name = "/dev/video10";

    /* ================= 第一阶段 打开视频设备 ================= */
    
    /* 文件描述符初始化，-1 表示无效状态 */
    int fd = -1;
    
    /*以阻塞，可读可写模式打开视频设备*/
    fd = open(dev_name,O_RDWR);

    /*错误处理检查open是否调用成功*/
    if(fd == -1){
        perror("Failed to open video device");
        return EXIT_FAILURE;
    }else{
        /* 输出设备打开成功信息及分配的文件描述符 */
        printf("1.Device opened successfully: %s,fd = %d\n",dev_name,fd);
    }
    /* ================= 第二阶段 设置视频格式 ================= */

    /* 实例化 V4L2 格式结构体，并初始化为 0 清除脏数据 */
    struct v4l2_format fmt;
    memset(&fmt,0,sizeof(fmt));

    /* 指定数据流类型为视频捕获(Capture) */
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    /*设置目标分辨率*/
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;

    /*设置像素格式:YUYV*/
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;

    /*设置场模式:V4L2_FIFELD_NONE*/
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    /*通过ioctl发送VIDIOC_S_FMT指令,将配置发送给驱动程序*/
    if(ioctl(fd,VIDIOC_S_FMT,&fmt) == -1){
        /* 若配置失败，打印底层错误并释放资源退出 */
        perror("Failed to set video format\n");
        close(fd);
        return EXIT_FAILURE;
    }

    /* 打印驱动实际接受的格式（硬件可能不支持你所求，会自动降级调整） */
    printf("2.Format set successfully: %d x %d, pixelformat: %c%c%c%c\n",
        fmt.fmt.pix.width, fmt.fmt.pix.height,
        (fmt.fmt.pix.pixelformat) & 0xFF,
        (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
        (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
        (fmt.fmt.pix.pixelformat >> 24) & 0xFF);

    /* ================= 第三阶段：申请缓冲区 ================= */
    
    /*实例化申请缓冲区的结构体*/
    struct v4l2_requestbuffers req;
    memset(&req,0,sizeof(req));
    
    /*申请四个缓冲区*/
    req.count = 4;

    /* 指定数据流类型为视频捕获 */
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    /*指定内存类型为MMAP*/
    req.memory = V4L2_MEMORY_MMAP;

    /*发送reqbufs 指令向内核要内存*/
    if(ioctl(fd,VIDIOC_REQBUFS,&req) == -1){
        perror("Failed to request buffers");
        close(fd);
        return EXIT_FAILURE;
    }else{
        /* 打印内核实际分配的缓冲区数量 */
        printf("3. Buffers requested successfully: %d buffers allocated\n", req.count);
    }

    /*================= 阶段四 mmap地址映射 ================= */
    /*定义结构体：记录映射后的起始地址和内存大小*/
    struct buffer{
        void *start;    /*映射到用户空间的虚拟地址起始指针*/
        size_t length;  /*缓冲区的字节大小*/
    };

    /*动态分配内存用来装4个结构体，calloc会自动把内存初始化为0*/
    struct buffer *buffer = calloc(req.count,sizeof(*buffer));

    /*遍历这4个缓冲区，向内核查询具体的信息并映射*/
    for (int i = 0; i < req.count; i++)
    {
        /*实例化v4l2缓冲区结构体，用于向内核查询信息*/
        struct v4l2_buffer buf;
        memset(&buf,0,sizeof(buf));

        /*告诉内核，我们要查询第i个缓冲区*/
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;;
        buf.memory =  V4L2_MEMORY_MMAP;
        buf.index = i;

        /* 发送 QUERYBUF 指令，获取缓冲区的长度和在物理内存中的偏移量 */
        if(ioctl(fd,VIDIOC_QUERYBUF,&buf) == -1){
            perror("Failed to query buffer");
            return EXIT_FAILURE;
        }else{
            /* 记录缓冲区的长度 */
            buffer[i].length = buf.length;

            /* 核心操作：使用 mmap 将内核物理内存映射到用户空间 */
            /* 
            * 使用 mmap 将内核物理内存映射到用户空间
            * 参数 1 (NULL)：映射的起始地址。传 NULL 表示让内核自动在用户空间找一块空闲地址。
            * 参数 2 (buf.length)：要映射的内存长度（字节数），由前面的 QUERYBUF 查询得到。
            * 参数 3 (PROT_READ | PROT_WRITE)：内存保护权限。表示映射后的内存既可读也可写。
            * 参数 4 (MAP_SHARED)：映射标志。表示与内核共享这块映射，对该内存的修改会反映给内核（也就是硬件）。
            * 参数 5 (fd)：文件描述符。表示要映射的是哪个设备节点。
            * 参数 6 (buf.m.offset)：被映射内存的偏移量。由 QUERYBUF 得到，告诉内核从物理内存的哪里开始映射。
            */
            buffer[i].start = mmap(NULL,buf.length,PROT_READ | PROT_WRITE,
                                MAP_SHARED,fd,buf.m.offset
            );
        }
    }
    
    /* 打印映射成功信息 */
    printf("4. mmap successfully mapped %d buffers\n", req.count);

    /* ================= 第五阶段：入队、开启流、出队 ================= */
    /*将所有空缓冲区放入内核的待填充队列*/
    for (int i = 0; i < req.count; i++)
    {
        struct v4l2_buffer buf;
        memset(&buf,0,sizeof(buf));

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        /*发送QBUF指令，把缓冲区交还给内核*/
        if(ioctl(fd,VIDIOC_QBUF,&buf) == -1){
            perror("Failed to queue buffer");
            return EXIT_FAILURE;
        }
    }
    printf("5.1 All %d buffers queued successfully\n", req.count);

    /*2.开启视频流*/
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    /*发送STREAMON指令，启动硬件采集*/
    if(ioctl(fd,VIDIOC_STREAMON,&type) == -1){
        perror("Failed to start stream");
        return EXIT_FAILURE;
    }
    printf("5.2 Video stream started successfully\n");

    /*出队获取一帧画面*/
    struct v4l2_buffer buf;
    memset(&buf,0,sizeof(buf));

    /* 极其关键：出队前必须指定类型和内存模式 */
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    /*
    //给硬件一点点时间准备第一帧画面 
    printf("Waiting for the first frame...\n");
    sleep(0.5); 
    */
   
    if(ioctl(fd,VIDIOC_DQBUF,&buf) == -1){
        perror("Failed to dequeue stream");
        return EXIT_FAILURE;
    }
    /* 打印取到的信息 */
    printf("5.3 Frame captured! Buffer index: %d, bytes used: %d\n",
         buf.index, buf.bytesused);
    
    /* ================= 第六阶段：保存图像数据到文件 ================= */
    /*以二进制写模式("wb")打开文件*/
    FILE *fp = fopen("frame.yuv","wb");
    if(fp == NULL){
        perror("Failed to open file for writing");
        return EXIT_FAILURE;
    }
    /* 将映射到用户空间的内存数据写入文件 */
    /* buffers[buf.index].start 就是刚才取出的那一帧数据的首地址 */
    /* buf.bytesused 是这一帧实际占用的字节数 */
    size_t written = fwrite(buffer[buf.index].start,1,buf.bytesused,fp);
    
    if(written != buf.bytesused){
        perror("Failed to write complete frame");
        fclose(fp);
        return EXIT_FAILURE;
    }
    
    /* 关闭文件 */
    fclose(fp);
    printf("6. Frame saved to frame.yuv successfully (Size: %d bytes)\n", 
                                                      buf.bytesused);

    /* ================= 第七阶段：清理与释放资源 ================= */                                                
        /* 1. 停止视频流（告诉硬件：别拍了） */
    if (ioctl(fd, VIDIOC_STREAMOFF, &type) == -1) {
        perror("Failed to stop stream");
        return EXIT_FAILURE;
    }else{

    /* 2. 解除内存映射（把 mmap 开的玻璃窗封上） */
    for (int i = 0; i < req.count; ++i) {
        munmap(buffer[i].start, buffer[i].length);
    }

    /* 3. 释放你 calloc 分配的记账本 */
    free(buffer);

    /* 4. 最后，关闭设备大门 */
    close(fd);

    printf("7. Cleanup finished. All resources released.\n");
    return EXIT_SUCCESS;
    }
    /* 释放文件描述符，关闭设备 */
    close(fd);
    return EXIT_SUCCESS;
}
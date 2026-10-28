#include <stdio.h>      // 标准输入输出，为了用 printf 打印信息
#include <stdlib.h>     // 标准库，为了用 exit 退出
#include <fcntl.h>      // 文件控制，为了用 open 函数和 O_RDWR 宏
#include <unistd.h>     // Unix 标准库，为了用 close 函数
#include <string.h>     // 字符串操作，为了用 memset 清空结构体
#include <sys/ioctl.h>  // 底层设备控制，为了用 ioctl 发送指令
#include <linux/videodev2.h> // V4L2 的核心大字典，包含了所有摄像头的结构体和宏

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
        printf("Failed to open video device");
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
        printf("Failed to set video format\n");
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

    /* 释放文件描述符，关闭设备 */
    close(fd);
    return EXIT_SUCCESS;
}
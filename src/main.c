#include <stdio.h>      // 标准输入输出，为了用 printf 打印信息
#include <stdlib.h>     // 标准库，为了用 exit 退出
#include <fcntl.h>      // 文件控制，为了用 open 函数和 O_RDWR 宏
#include <unistd.h>     // Unix 标准库，为了用 close 函数
#include <string.h>     // 字符串操作，为了用 memset 清空结构体
#include <sys/ioctl.h>  // 底层设备控制，为了用 ioctl 发送指令
#include <linux/videodev2.h> // V4L2 的核心大字典，包含了所有摄像头的结构体和宏

int main(){
    /* 指定 V4L2 视频设备节点路径（主通道） */
    const char *dev_name = "/dev/video0";

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
        printf("Device opened successfully: %s,fd = %d\n",dev_name,fd);
    }

    /* 释放文件描述符，关闭设备 */
    close(fd);
    return EXIT_SUCCESS;
}
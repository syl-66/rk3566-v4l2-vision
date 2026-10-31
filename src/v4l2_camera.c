#include <stdio.h>      // 标准输入输出，为了用 printf 打印信息
#include <stdlib.h>     // 标准库，为了用 exit 退出
#include <fcntl.h>      // 文件控制，为了用 open 函数和 O_RDWR 宏
#include <unistd.h>     // Unix 标准库，为了用 close 函数
#include <string.h>     // 字符串操作，为了用 memset 清空结构体
#include <sys/ioctl.h>  // 底层设备控制，为了用 ioctl 发送指令
#include <linux/videodev2.h> // V4L2 的核心大字典，包含了所有摄像头的结构体和宏
#include <sys/mman.h>   //使用mmap函数

#include "v4l2_camera.h"

/*自己的的理解
camera_init
第一步
open -> 为了是获取到fd，通过fd来标识自己要控制的硬件是哪一个

第二步
设置视频的格式 fmt

第三步
通过ioctl(fd, VIDIOC_S_FMT, &fmt);来告诉硬件输出什么格式的视频数据

第四步
申请四个缓冲区，用来存储camera的数据，这四个缓冲区都是创建在内核里面的，用户直接操作不了

第五步
创建一个local_buffer，里面存了 mmap映射后的虚拟地址，和缓冲区大小，缓冲区大小是解除映射的必要参数

第六步
创建一个buf,因为用户不能直接操作访问内核的缓冲区，所以通过buf来告诉内核用户要操作哪一个缓冲区,用index来标识

第七步
把每一个内核缓冲区的地址通过mmap映射的虚拟地址存到local_buffer.start里，缓冲区大小存到loacl_buffer.length里

camera_start
第一步
创建一个buf来告诉内核，用户要操作哪一个缓冲区

第二步
然后通过ioctl(fd, VIDIOC_QBUF, &buf)，把buf.index个缓冲区使用权还给内核

第三步
通过enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;设置camera的工作模式

第四步
通过ioctl(fd, VIDIOC_STREAMON, &type)；告诉camera硬件用type的工作模式开始工作采集数据

camera_get_frame
第一步
创建一个buf来告诉内核，camera类型和内存模式，因为获取缓冲区的数据是阻塞式的，只要有缓冲区满了就拿走

第二步
通过ioctl(fd, VIDIOC_DQBUF, &buf)，去缓冲区里拿一帧数据

第三步
把一帧数据存到*data里，data是函数参数，main函数可以创建参数，来传递到函数里
*data = buffers[buf.index].start;
*size = buf.bytesused;

第四步
通过ioctl(fd, VIDIOC_QBUF, &buf)把用完的缓存区再给内核，

camera_stop_and_cleanup
第一步
通过ioctl(fd, VIDIOC_STREAMOFF, &type)，停止camera采集数据

第二步
munmap解除映射，因为是用户解除映射，操作在用户态，不需要通过index来操作内核缓冲区，直接操作虚拟地址就行

第三步
free(buffers)；
close(fd)；
*/

/**
 * @brief 初始化 V4L2 摄像头设备
 * 
 * 该函数负责打开视频设备节点，配置视频捕获格式（分辨率、像素格式等），
 * 并向内核申请用于存放视频帧的 MMAP 缓冲区。
 * 
 * @param[in]  dev_name     视频设备节点的路径（例如 "/dev/video10"）
 * @param[out] fd_out       输出参数，保存成功打开的文件描述符
 * @param[out] buffers      输出参数，保存映射到用户空间的缓冲区信息数组的指针
 * @param[out] buffer_count 输出参数，保存内核实际分配的缓冲区数量
 * 
 * @return int 成功返回 0，失败返回 -1
 */
int camera_init(const char *dev_name,int *fd_out,struct buffer **buffers,int *buffer_count){
    /*1.打开ia设备节点。获取文件描述符*/
    int fd = -1;
    struct buffer *local_buffers = NULL;
    int mapped_count = 0; /* 记录成功 mmap 的缓冲区数量 */

    fd = open(dev_name,O_RDWR);

    if(fd == -1){
        perror("Failed to open this video device");
        return -1;
    }
        printf("1. Device opened successfully: %s, fd = %d\n", dev_name, fd);
        *fd_out = fd;
    

    /*2.设置视频格式*/
    struct v4l2_format fmt;
    memset(&fmt,0,sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    /* 发送 S_FMT 指令 */
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
        perror("Failed to set video format");
        close(fd);
        goto error_close_fd;
    }
    printf("2. Format set successfully: %d x %d, pixelformat: %c%c%c%c\n",
           fmt.fmt.pix.width, fmt.fmt.pix.height,
           (fmt.fmt.pix.pixelformat) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 24) & 0xFF);
    

    /*3.申请缓冲区*/
    struct v4l2_requestbuffers req;
    memset(&req,0,sizeof(req));

    req.count = 4;
    req.type =  V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if(ioctl(fd,VIDIOC_REQBUFS,&req) == -1){
        perror("Failed to request buffers");
        close(fd);
        goto error_close_fd;
    }
    printf("3. Buffers requested successfully: %d buffers allocated\n", req.count);
    
    *buffer_count = req.count;

    /* 4.动态分配用于管理缓冲区信息的数组（记账本）。
     * 使用局部指针 local_buffers 接收，防止操作过程中污染外部传入的 buffers 指针。
     */
    local_buffers = calloc(req.count, sizeof(*local_buffers));
    if (local_buffers == NULL) {
        perror("Failed to allocate memory for buffers");
        close(fd);
        goto error_close_fd;
    }

    /* 遍历内核分配的所有缓冲区，依次查询信息并建立内存映射 */
    for (int i = 0; i < req.count; ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));

        /* 指定要查询的缓冲区索引和类型 */
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        /* 发送 VIDIOC_QUERYBUF 指令，获取第 i 个缓冲区的长度及物理偏移量 */
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
            perror("Failed to query buffer");
            goto error_unmap;
        }

        /* 记录缓冲区长度 */
        local_buffers[i].length = buf.length;
        
        /* 
         * 使用 mmap 将内核物理内存映射至用户空间。
         * 参数 1 (NULL)：由内核自动分配虚拟地址。
         * 参数 2 (buf.length)：映射的内存大小。
         * 参数 3 (PROT_READ | PROT_WRITE)：内存可读可写。
         * 参数 4 (MAP_SHARED)：与内核共享该映射区域。
         * 参数 5 (fd)：设备文件描述符。
         * 参数 6 (buf.m.offset)：物理内存的偏移量。
         */
        local_buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, 
                                      MAP_SHARED, fd, buf.m.offset);

        /* 检查 mmap 返回值是否为 MAP_FAILED (即 (void *)-1) */
        if (local_buffers[i].start == MAP_FAILED) {
            perror("Failed to mmap buffer");
            goto error_unmap;
        }

    }
    printf("4. mmap successfully mapped %d buffers\n", req.count);

    /* 
     * 输出参数赋值。
     * 将内部动态分配的数组首地址，赋值给调用者传入的二级指针所指向的变量。
     */
    *fd_out = fd;
    *buffers = local_buffers;

    return 0; /* 初始化成功，返回 0 */

    /* ================= 错误处理区（阶梯式打扫战场） ================= */
error_unmap:
    /* 如果中途失败，先把它前面已经成功映射的内存解除 */
    for (int i = 0; i < mapped_count; ++i) {
        munmap(local_buffers[i].start, local_buffers[i].length);
    }

error_close_fd:
    /* 如果申请了记账本，释放它 */
    if (local_buffers != NULL) {
        free(local_buffers);
    }
    /* 最后一定要关闭文件描述符 */
    close(fd);
    
    return -1;
}

/**
 * @brief 启动视频流
 * 
 * 将初始化阶段分配的所有缓冲区放入内核待填充队列（入队），
 * 随后发送命令开启视频流，让摄像头硬件开始采集画面。
 * 
 * @param[in] fd 成功打开的视频设备的文件描述符
 * 
 * @return int 成功返回 0，失败返回 -1
 */
int camera_start(int fd,int buffer_count){
    /* 1. 将所有空缓冲区放入内核的待填充队列 (QBUF) */
    for (int i = 0; i < buffer_count; i++){
        struct v4l2_buffer buf;
        memset(&buf,0,sizeof(buf));

        /* 指定队列类型、内存模式以及要入队的仓库索引 */
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        /* 发送 QBUF 指令，把空缓冲区交还给内核 */
        if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
            perror("Failed to queue buffer");
            return -1;
        }
    }
    printf("5.1 All %d buffers queued successfully\n", buffer_count);
    
    /* 2. 开启视频流 (STREAMON) */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    /* 发送 STREAMON 指令，启动硬件采集 */
    if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
        perror("Failed to start stream");
        return -1;
    }
    printf("5.2 Video stream started successfully\n");

    return 0;
}

/**
 * @brief 从视频流中获取一帧画面
 * 
 * 该函数是阻塞的，如果队列中没有准备好的帧，会一直等待直到有数据。
 * 
 * @param[in]  fd      视频设备的文件描述符
 * @param[in]  buffers 保存了映射信息的缓冲区数组（记账本）
 * @param[out] data    输出参数，返回这一帧画面在用户空间的内存首地址
 * @param[out] size    输出参数，返回这一帧画面的实际字节大小
 * 
 * @return int 成功返回 0，失败返回 -1
 */
int camera_get_frame(int fd, struct buffer *buffers, void **data, size_t *size){
     
    /* 实例化 V4L2 缓冲区结构体作为临时工单 */
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));

    /* 指定工单类型和内存模式 */
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    /* 1. 发送 DQBUF 指令，向内核讨要一帧已装满数据的缓冲区 */
    /* 注意：这是一个阻塞操作。如果当前没有准备好的帧，程序会停在这里等待。 */
    if (ioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
        perror("Failed to dequeue buffer");
        return -1;
    }

    /* 2. 根据内核返回的索引号（buf.index），从记账本中提取真实数据 */
    /* *data 指向画面数据的真实内存地址，*size 返回实际数据大小 */
    *data = buffers[buf.index].start;
    *size = buf.bytesused;

    /* 3. 发送 QBUF 指令，将刚用过的空缓冲区重新交还给内核，进入待填充队列 */
    if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
        perror("Failed to requeue buffer");
        return -1;
    }

    return 0; /* 成功获取一帧画面 */
}

/**
 * @brief 停止视频流并清理所有申请的资源
 * 
 * @param[in] fd           视频设备的文件描述符
 * @param[in] buffers      保存了映射信息的缓冲区数组（记账本）
 * @param[in] buffer_count 缓冲区数量
 */
void camera_stop_and_cleanup(int fd, struct buffer *buffers, int buffer_count) {
    /* 1. 停止视频流 (STREAMOFF) */
    /* 告诉硬件不要再往内存里写数据了 */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    if (ioctl(fd, VIDIOC_STREAMOFF, &type) == -1) {
        perror("Failed to stop stream");
    }

    /* 2. 解除内存映射并释放账本 (munmap & free) */
    if (buffers != NULL) {
        for (int i = 0; i < buffer_count; ++i) {
            /* 增加防御性判断，防止解除未映射的非法地址 */
            if (buffers[i].start != NULL && buffers[i].start != MAP_FAILED) {
                /* 拔掉那根插在内核物理内存上的“数据线” */
                munmap(buffers[i].start, buffers[i].length);
            }
        }
        /* 烧毁记账本 */
        free(buffers);
    }

    /* 3. 关闭设备节点 (close) */
    if (fd != -1) {
        close(fd);
    }
    
    printf("7. Cleanup finished. All resources released.\n");
}


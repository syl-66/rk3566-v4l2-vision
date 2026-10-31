#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "v4l2_camera.h"
#include "rknn_engine.h"

int main() {
    const char *dev_name = "/dev/video10";
    const char *model_path = "/root/rk3566_vision/yolov5s-640-640.rknn";
    int fd = -1;
    struct buffer *buffers = NULL;
    int buffer_count = 0;

    /* 1. 初始化 V4L2 采集端 */
    if (camera_init(dev_name, &fd, &buffers, &buffer_count) != 0) return -1;
    if (camera_start(fd, buffer_count) != 0) return -1;

    /* 2. 初始化 RKNN NPU 推理端 */
    if (rknn_engine_init(model_path) != 0) return -1;

    void *yuyv_data = NULL;
    size_t frame_size = 0;

    /* 3. 采集一帧原始图像 */
    if (camera_get_frame(fd, buffers, &yuyv_data, &frame_size) == 0) {
        
        /* 分配用户态内存空间用于存放预处理后的 RGB 图像 */
        unsigned char *rgb_data = (unsigned char *)malloc(640 * 640 * 3);
        
        if (rgb_data != NULL) {
            /* 执行图像预处理 (YUV -> RGB, 缩放) */
            convert_yuyv_to_rgb((unsigned char *)yuyv_data, 640, 480, rgb_data);
            
            /* 将预处理数据送入 NPU 进行推理 */
            rknn_engine_infer(rgb_data);
            
            /* 释放临时图像缓冲区 */
            free(rgb_data);
        }
    }

    /* 4. 销毁资源与清理 */
    rknn_engine_destroy();
    camera_stop_and_cleanup(fd, buffers, buffer_count);

    return 0;
}
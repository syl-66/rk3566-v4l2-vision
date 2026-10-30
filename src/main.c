#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "v4l2_camera.h"
#include "rknn_engine.h"

int main() {
    const char *dev_name = "/dev/video10";
    int fd = -1;
    struct buffer *buffers = NULL;
    int buffer_count = 0;

    /* 1. 初始化摄像头 */
    if (camera_init(dev_name, &fd, &buffers, &buffer_count) != 0) {
        return -1;
    }

    /* 2. 启动摄像头 */
    if (camera_start(fd, buffer_count) != 0) {
        return -1;
    }

    printf("=== 开始测试图像预处理转换 ===\n");
    
    void *yuyv_data = NULL;
    size_t frame_size = 0;

    /* 3. 抓取一帧画面 */
    if (camera_get_frame(fd, buffers, &yuyv_data, &frame_size) == 0) {
        printf("成功抓到 YUYV 帧，大小: %zu 字节\n", frame_size);

        /* 4. 申请一块 640x640x3 的 RGB 内存 */
        unsigned char *rgb_data = (unsigned char *)malloc(640 * 640 * 3);
        if (rgb_data == NULL) {
            printf("RGB 内存分配失败\n");
            return -1;
        }

        /* 5. 调用你手搓的转换函数 */
        convert_yuyv_to_rgb((unsigned char *)yuyv_data, 640, 480, rgb_data);
        printf("图像转换完成，准备保存文件...\n");

        /* 6. 把转换后的 RGB 数据存到文件里 */
        FILE *fp = fopen("test_rgb.rgb", "wb");
        if (fp) {
            fwrite(rgb_data, 1, 640 * 640 * 3, fp);
            fclose(fp);
            printf("成功保存到 test_rgb.rgb (大小: %d 字节)\n", 640 * 640 * 3);
        }
        
        free(rgb_data);
    }

    /* 7. 清理资源 */
    camera_stop_and_cleanup(fd, buffers, buffer_count);
    printf("=== 测试结束 ===\n");

    return 0;
}
#include <stdio.h>
#include <stdlib.h>
#include "v4l2_camera.h"

int main() {
    const char *dev_name = "/dev/video10";
    int fd = -1;
    struct buffer *buffers = NULL;
    int buffer_count = 0;

    /* 1. 初始化 */
    if (camera_init(dev_name, &fd, &buffers, &buffer_count) != 0) {
        printf("初始化失败，程序退出\n");
        return EXIT_FAILURE;
    }

    /* 2. 启动视频流 */
    if (camera_start(fd,buffer_count) != 0) {
        printf("启动失败，程序退出\n");
        return EXIT_FAILURE;
    }


    /* 3. 连续抓取 10 帧画面，保存到文件 */
    printf("=== Starting to capture frames ===\n");
    
    for (int i = 0; i < 10; i++) {
        void *frame_data = NULL;
        size_t frame_size = 0;

        /* 从底层拿一帧（阻塞等待） */
        if (camera_get_frame(fd, buffers, &frame_data, &frame_size) == 0) {
            
            /* 把这一帧存为文件 */
            char filename[32];
            sprintf(filename, "frame_%d.yuv", i);
            
            FILE *fp = fopen(filename, "wb");
            if (fp) {
                fwrite(frame_data, 1, frame_size, fp);
                fclose(fp);
                printf("Saved: %s (Size: %zu bytes)\n", filename, frame_size);
            }
        }
    }

    /* 4. 停止视频流，清理所有资源 */
    camera_stop_and_cleanup(fd, buffers, buffer_count);

    return EXIT_SUCCESS;
}
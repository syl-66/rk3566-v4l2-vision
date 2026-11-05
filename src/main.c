#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>    /* 引入时间库，用于防抖计时 */

#include "v4l2_camera.h"
#include "rknn_engine.h"

/* 
 * 声明外部变量。
 * coco_classes 数组实际定义在 rknn_engine.c 中，这里用 extern 引入，
 * 以便在 main.c 中通过 class_id 查表获取物体的英文名称。
 */
extern const char *coco_classes[80];

int main(int argc, char *argv[]) {
    
    /* 硬件设备路径及模型路径配置 */
    const char *dev_name = "/dev/video10";
    const char *model_path = "/root/rk3566_vision/yolov8.rknn";
    
    int fd = -1;
    struct buffer *buffers = NULL;
    int buffer_count = 0;

    /* ================= 1. 初始化硬件与引擎 ================= */
    
    /* 1.1 初始化摄像头（V4L2 零拷贝准备） */
    printf("=== 初始化摄像头 ===\n");
    if (camera_init(dev_name, &fd, &buffers, &buffer_count) != 0) {
        printf("Camera init failed!\n");
        return EXIT_FAILURE;
    }

    /* 1.2 启动视频流（交还空缓冲区，开启采集） */
    printf("=== 启动视频流 ===\n");
    if (camera_start(fd, buffer_count) != 0) {
        printf("Camera start failed!\n");
        camera_stop_and_cleanup(fd, buffers, buffer_count);
        return EXIT_FAILURE;
    }

    /* 1.3 初始化 NPU 引擎（加载 YOLOv8 模型） */
    printf("=== 初始化 NPU 引擎 ===\n");
    if (rknn_engine_init(model_path) != 0) {
        printf("NPU init failed!\n");
        camera_stop_and_cleanup(fd, buffers, buffer_count);
        return EXIT_FAILURE;
    }

    /* 提前分配好 RGB 内存，防止在死循环中反复 malloc 导致内存碎片 */
    unsigned char *rgb_data = (unsigned char *)malloc(640 * 640 * 3);
    if (rgb_data == NULL) {
        printf("Failed to allocate rgb_data!\n");
        return EXIT_FAILURE;
    }

    /* 准备用于接收检测结果的结构体数组，最多同时接收 10 个目标 */
    DetectResult my_results[10]; 

    /* 
     * 用于防抖的时间记录数组。
     * 记录 80 个类别的最后一次播报时间，防止 AI 每帧都重复播报同一个东西。
     */
    time_t last_report_time[80] = {0}; 

    printf("=== 系统初始化完毕，开始实时导航识别 ===\n");

    /* ================= 2. 核心业务死循环 ================= */
    while (1) {
        
        void *yuyv_data = NULL;
        size_t frame_size = 0;

        /* 2.1 阻塞抓取一帧图像 */
        if (camera_get_frame(fd, buffers, &yuyv_data, &frame_size) == 0) {
            
            /* 2.2 图像预处理：YUV 转 RGB 并缩放到 640x640 */
            convert_yuyv_to_rgb((unsigned char *)yuyv_data, 640, 480, rgb_data);
            
            /* 2.3 送入 NPU 执行推理，返回找到的物体数量 */
            int found_count = rknn_engine_infer(rgb_data, my_results, 10);
            
            /* 获取当前系统时间（秒级） */
            time_t now = time(NULL);

            /* 2.4 业务逻辑：遍历检测结果，判断方位并播报 */
            if (found_count > 0) {
                for (int i = 0; i < found_count; i++) {
                    
                    /* 获取目标的 X 坐标（归一化 0.0 ~ 1.0）、类别 ID 和置信度 */
                    float x = my_results[i].x;
                    int class_id = my_results[i].class_id;
                    float conf = my_results[i].confidence;
                    
                    /* 
                     * 【防抖核心逻辑】
                     * 如果这个类别的物体在 3 秒内已经播报过，就直接跳过，
                     * 避免视障人士的耳朵被重复的信息轰炸。
                     */
                    if (now - last_report_time[class_id] < 3) {
                        continue;
                    }
                    
                    /* 更新该物体的最后播报时间 */
                    last_report_time[class_id] = now;

                    /* 根据 X 坐标判断物体在左、中、右哪个方位 */
                    if (x < 0.33) {
                        printf("🚨 左前方有 %s (置信度: %.2f)\n", coco_classes[class_id], conf);
                    } else if (x > 0.66) {
                        printf("🚨 右前方有 %s (置信度: %.2f)\n", coco_classes[class_id], conf);
                    } else {
                        printf("🚨 正前方有 %s (置信度: %.2f)\n", coco_classes[class_id], conf);
                    }
                }
            } else {
                /* 当前帧没有检测到物体，打印一个点表示系统存活 */
                printf("."); 
                fflush(stdout);
            }
        }
    }

    /* ================= 3. 资源清理（理论上死循环走不到这里） ================= */
    free(rgb_data);
    rknn_engine_destroy();
    camera_stop_and_cleanup(fd, buffers, buffer_count);

    return EXIT_SUCCESS;
}
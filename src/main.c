#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>    /* 引入时间库，用于防抖和音频冷却计时 */

#include "v4l2_camera.h"
#include "rknn_engine.h"
#include "speech.h"  /* 引入语音播报模块 */

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
     * 核心防抖变量：
     * 1. last_audio_time: 全局音频冷却计时器，保证 1.5 秒内只响一次，防止声卡打架。
     * 2. last_report_time: 记录 80 个类别的最后播报时间，同一个物体 5 秒内不重复播报。
     */
    time_t last_audio_time = 0; 
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

            /* 
             * 2.4 【音频防冲突机制】
             * 如果距离上一次发声不足 1.5 秒，说明声卡还在播报。
             * 我们直接跳过这一帧，等下一帧再处理，防止声音被截断。
             */
            if (now - last_audio_time < 1.5) {
                printf("."); 
                fflush(stdout);
                continue; 
            }

            /* 2.5 业务逻辑：排队播报 */
            if (found_count > 0) {
                int has_spoken = 0; /* 标记这一帧是否已经发过声了 */

                for (int i = 0; i < found_count; i++) {
                    float x = my_results[i].x;
                    int class_id = my_results[i].class_id;
                    float conf = my_results[i].confidence;
                    
                    /* 
                     * 【单物体防抖】
                     * 同一个类别的物体（比如你一直拿着杯子），5 秒内不重复播报。
                     */
                    if (now - last_report_time[class_id] < 5) {
                        continue;
                    }
                    
                    /* 
                     * 【单帧限流】
                     * 如果这一帧已经挑中了一个物体播报，剩下的即使满足条件也不报了。
                     * 留到下一帧（1.5秒后）再排着队播，这样听觉才不会乱。
                     */
                    if (has_spoken) {
                        continue;
                    }

                    /* 更新计时器 */
                    last_report_time[class_id] = now;
                    last_audio_time = now; 
                    has_spoken = 1; /* 标记本帧已播报 */

                    /* 判断方位并触发语音播报 */
                    if (x < 0.33) {
                        printf("🚨 左前方有 %s (置信度: %.2f)\n", coco_classes[class_id], conf);
                        speech_report("Left", coco_classes[class_id]);
                    } else if (x > 0.66) {
                        printf("🚨 右前方有 %s (置信度: %.2f)\n", coco_classes[class_id], conf);
                        speech_report("Right", coco_classes[class_id]);
                    } else {
                        printf("🚨 正前方有 %s (置信度: %.2f)\n", coco_classes[class_id], conf);
                        speech_report("Front", coco_classes[class_id]);
                    }
                }
                
                /* 如果检测到了东西，但因为冷却时间或防抖没播报，就打点 */
                if (!has_spoken) {
                    printf("."); 
                    fflush(stdout);
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
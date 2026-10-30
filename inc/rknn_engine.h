#ifndef RKNN_ENGINE_H
#define RKNN_ENGINE_H

/**
 * @brief 初始化 RKNN 引擎，加载 AI 模型
 * 
 * @param model_path 模型文件（.rknn）在板子上的绝对路径
 * @return int 成功返回 0，失败返回 -1
 */
int rknn_engine_init(const char *model_path);

/**
 * @brief 销毁 RKNN 引擎，释放 NPU 硬件资源
 */
void rknn_engine_destroy(void);

/**
 * @brief 将摄像头采集的 YUYV 格式图像转换并缩放为 640x640 的 RGB 图像
 * 
 * 背景说明：
 * V4L2 摄像头输出的是 YUYV 格式（YUV422），而 YOLOv5 模型需要的输入是
 * 尺寸为 640x640、色彩空间为 RGB、内存排布为 NHWC 的 UINT8 数据。
 * 本函数负责完成色彩转换（YUV->RGB）和尺寸缩放（480->640）。
 * 
 * @param yuyv_data   指向原始 YUYV 图像数据的指针（由 V4L2 采集得到）
 * @param src_w       原始图像的宽度（通常是 640）
 * @param src_h       原始图像的高度（通常是 480）
 * @param rgb_data    指向目标 RGB 缓冲区的指针（需提前 malloc 分配 640*640*3 字节）
 * @return int        成功返回 0
 */
int convert_yuyv_to_rgb(unsigned char *yuyv_data,int src_w,int src_h,unsigned char *rgb_data);

#endif // RKNN_ENGINE_H
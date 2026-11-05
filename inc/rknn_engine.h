#ifndef RKNN_ENGINE_H
#define RKNN_ENGINE_H
/**
 * @brief 定义单个目标检测结果的结构体
 * 
 * @details 用于在引擎与业务层（main.c）之间传递 AI 识别出的数据。
 */
typedef struct {
    int class_id;        /* 物体类别 ID (如 0 是人, 41 是杯子) */
    float confidence;    /* 置信度得分 (0.0 ~ 1.0) */
    float x;             /* 目标中心点 X 归一化坐标 (0.0 ~ 1.0) */
    float y;             /* 目标中心点 Y 归一化坐标 (0.0 ~ 1.0) */
    float w;             /* 目标宽度占比 */
    float h;             /* 目标高度占比 */
}DetectResult;

/**
 * @brief 初始化 RKNN NPU 引擎
 */
int rknn_engine_init(const char *model_path);

/**
 * @brief 执行 NPU 推理并提取所有检测到的目标
 * 
 * @param[in]  rgb_data    预处理好的 640x640 RGB 图像数据
 * @param[out] results     用于接收检测结果的数组指针（由调用者分配内存）
 * @param[in]  max_results 数组的最大容量（防止越界写入）
 * 
 * @return int 成功返回实际检测到的目标数量，失败返回 -1
 */
int rknn_engine_infer(unsigned char *rgb_data, DetectResult *results, int max_results);

/**
 * @brief 销毁 RKNN 引擎并释放硬件资源
 */
void rknn_engine_destroy(void);

/**
 * @brief 将摄像头采集的 YUYV 格式转换为 640x640 的 RGB
 */
int convert_yuyv_to_rgb(unsigned char *yuyv_data, int src_w, int src_h, unsigned char *rgb_data);
#endif
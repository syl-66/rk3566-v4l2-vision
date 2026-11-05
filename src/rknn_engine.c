/**
 * @file rknn_engine.c
 * @brief RKNN NPU 推理引擎模块 (YOLOv8 专属适配版，含 NMS 去重)
 */

#include "rknn_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rknn_api.h"

/* 全局变量：保存 NPU 的上下文句柄 */
static rknn_context ctx = 0;

/* COCO 数据集 80 个类别名称 */
const char *coco_classes[80] = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light",
    "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
    "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket", "bottle",
    "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange",
    "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch", "potted plant", "bed",
    "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone", "microwave", "oven",
    "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush"
};

/**
 * @brief 初始化 NPU 引擎
 */
int rknn_engine_init(const char *model_path){
    int ret;
    
    ret = rknn_init(&ctx, (void*)model_path, 0, 0, NULL); 
    if (ret < 0) {
        printf("rknn_init failed! error code = %d\n", ret);
        return -1;
    }
    printf("1. NPU 引擎初始化成功，成功加载模型: %s\n", model_path);

    rknn_input_output_num io_num;
    rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    printf("模型输入张量个数: %d, 输出张量个数: %d\n", io_num.n_input, io_num.n_output);

    return 0;
}

/**
 * @brief 执行 NPU 推理并提取结果
 */
int rknn_engine_infer(unsigned char *rgb_data, DetectResult *results, int max_results){
    int ret;
    int detect_count = 0;   
    
    /* 临时存放所有候选框，最多暂存 100 个 */
    DetectResult candidates[100];
    int cand_count = 0;

    /* 1. 构造输入数据 */
    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index  = 0;
    inputs[0].type   = RKNN_TENSOR_UINT8;
    inputs[0].size   = 640 * 640 * 3;         
    inputs[0].fmt    = RKNN_TENSOR_NHWC;      
    inputs[0].buf    = rgb_data;  

    ret = rknn_inputs_set(ctx, 1, inputs);
    if (ret < 0) return -1;
    
    /* 2. 触发计算 */
    ret = rknn_run(ctx, NULL);
    if (ret < 0) return -1;

    /* 3. 获取 YOLOv8 的 9 个输出张量 */
    rknn_output outputs[9];
    memset(outputs, 0, sizeof(outputs));
    for (int i = 0; i < 9; i++) {
        outputs[i].index = i;      
        outputs[i].want_float = 1; 
    }

    ret = rknn_outputs_get(ctx, 9, outputs, NULL);
    if (ret < 0) return -1;

    /* ================= 4. 后处理：多尺度解析 ================= */
    int grid_sizes[3] = {80, 40, 20};
    
    for (int s = 0; s < 3; s++) {
        int grid_size = grid_sizes[s];
        int stride = 640 / grid_size; 
        int total_pixels = grid_size * grid_size;
        
        float *p_cls = (float *)outputs[s * 3 + 1].buf;  
        float *p_conf = (float *)outputs[s * 3 + 2].buf; 

        for (int h = 0; h < grid_size; h++) {
            for (int w = 0; w < grid_size; w++) {
                int idx = h * grid_size + w;
                
                float conf = p_conf[idx];
                if (conf > 1.0f) conf = 1.0f / (1.0f + expf(-conf)); 
                
                if (conf < 0.35) continue;

                int max_class = 0;
                float max_prob = 0;
                for (int c = 0; c < 80; c++) {
                    float prob = p_cls[c * total_pixels + idx];
                    if (prob > max_prob) {
                        max_prob = prob;
                        max_class = c;
                    }
                }
                //置信度大于0.6才输出
                float final_score = conf * max_prob;
                if (final_score < 0.60 || cand_count >= 100) continue;

                /* 简化版坐标解码（取网格中心点固定大小） */
                candidates[cand_count].class_id   = max_class;
                candidates[cand_count].confidence = final_score;
                candidates[cand_count].x          = (w + 0.5f) * stride / 640.0f;
                candidates[cand_count].y          = (h + 0.5f) * stride / 640.0f;
                candidates[cand_count].w          = stride * 2.0f / 640.0f; 
                candidates[cand_count].h          = stride * 2.0f / 640.0f;
                
                cand_count++;
            }
        }
    }

    /* ================= 5. NMS 去重 ================= */
    /* 冒泡排序：按置信度降序排列，把最准的排在最前面 */
    for (int i = 0; i < cand_count - 1; i++) {
        for (int j = i + 1; j < cand_count; j++) {
            if (candidates[j].confidence > candidates[i].confidence) {
                DetectResult temp = candidates[i];
                candidates[i] = candidates[j];
                candidates[j] = temp;
            }
        }
    }

    /* 遍历剔除中心点距离过近的同类框 */
    for (int i = 0; i < cand_count; i++) {
        if (candidates[i].confidence == 0.0f) continue; /* 已经被剔除 */
        
        /* 保留这个框 */
        results[detect_count] = candidates[i];
        detect_count++;
        
        /* 检查后面的框，如果和当前框类别一样，且中心点距离极近（<0.1），就剔除 */
        for (int j = i + 1; j < cand_count; j++) {
            if (candidates[j].class_id == candidates[i].class_id) {
                float dx = candidates[i].x - candidates[j].x;
                float dy = candidates[i].y - candidates[j].y;
                float dist = sqrtf(dx * dx + dy * dy);
                
                if (dist < 0.10f) {
                    candidates[j].confidence = 0.0f; 
                }
            }
        }
    }

    rknn_outputs_release(ctx, 9, outputs);
    return detect_count; 
}

/**
 * @brief 销毁引擎
 */
void rknn_engine_destroy(void) {
    if (ctx != 0) {
        rknn_destroy(ctx);
        printf("2. NPU 资源已释放\n");
        ctx = 0;
    }
}

/**
 * @brief 图像预处理：YUV 转 RGB
 */
int convert_yuyv_to_rgb(unsigned char *yuyv_data, int src_w, int src_h, unsigned char *rgb_data) {
    int dst_w = 640;
    int dst_h = 640;

    for (int y = 0; y < dst_h; y++) {
        for (int x = 0; x < dst_w; x++) {
            int src_x = (x * src_w) / dst_w;
            int src_y = (y * src_h) / dst_h;

            if (src_x >= src_w) src_x = src_w - 1;
            if (src_y >= src_h) src_y = src_h - 1;

            int yuyv_index = (src_y * src_w + src_x) * 2; 
            unsigned char Y = yuyv_data[yuyv_index];
            
            unsigned char U, V;
            if (src_x % 2 == 0) {
                U = yuyv_data[yuyv_index + 1];
                V = yuyv_data[yuyv_index + 3];
            } else {
                U = yuyv_data[yuyv_index - 1];
                V = yuyv_data[yuyv_index + 1];
            }

            int r = Y + 1.402 * (V - 128);
            int g = Y - 0.344136 * (U - 128) - 0.714136 * (V - 128);
            int b = Y + 1.772 * (U - 128);

            if (r > 255) r = 255; else if (r < 0) r = 0;
            if (g > 255) g = 255; else if (g < 0) g = 0;
            if (b > 255) b = 255; else if (b < 0) b = 0;

            int rgb_index = (y * dst_w + x) * 3;
            rgb_data[rgb_index]     = (unsigned char)r;
            rgb_data[rgb_index + 1] = (unsigned char)g;
            rgb_data[rgb_index + 2] = (unsigned char)b;
        }
    }
    return 0;
}
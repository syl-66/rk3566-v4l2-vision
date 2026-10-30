#include "rknn_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rknn_api.h" /* 引入瑞芯微官方的 NPU 头文件 */

/* 全局变量：用于保存 NPU 的上下文句柄（类似于文件描述符 fd） */
static rknn_context ctx = 0;

int rknn_engine_init(const char *model_path){
    int ret;
    /* 
     * 调用 rknn_init 加载模型。
     * 参数 1 (&ctx): 成功后会在这里填入 NPU 的句柄。
     * 参数 2 (model_path): 模型的路径。
     * 参数 3 (0): 模型大小（传 0 代表让底层自己去读取文件）。
     * 参数 4 (0): 标志位。
     * 参数 5 (NULL): 扩展参数。
     */
    ret = rknn_init(&ctx,(void *)model_path,0,0,NULL);

    if(ret < 0){
        printf("rknn_init failed! error code = %d\n", ret);
        return -1;
    }
        printf("1. NPU 引擎初始化成功，成功加载模型: %s\n", model_path);

    /*查询模型输入输出信息*/
    rknn_input_output_num io_num;
    
    /* 查询模型有多少个输入和输出 */
    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret != RKNN_SUCC) {
        printf("rknn_query io_num failed! error code = %d\n", ret);
        return -1;
    }
    printf("模型输入张量个数: %d, 输出张量个数: %d\n", io_num.n_input, io_num.n_output);

    /* 查询输入张量的具体属性（宽、高、通道、数据格式） */
    rknn_tensor_attr input_attr;
    memset(&input_attr, 0, sizeof(input_attr));
    input_attr.index = 0; 
    
    ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &input_attr, sizeof(input_attr));
    if (ret != RKNN_SUCC) {
        printf("rknn_query input_attr failed! error code = %d\n", ret);
        return -1;
    }
    
    printf("输入信息 -> 宽: %d, 高: %d, 通道: %d, 格式(NCHW=0, NHWC=1): %d, 数据类型: %d\n",
           input_attr.dims[2], input_attr.dims[1], input_attr.dims[3], 
           input_attr.fmt, input_attr.type);
   
    return 0;
}

void rknn_engine_destroy(void) {
    /* 底层规矩：有借有还。关闭 NPU，释放内存 */
    if (ctx != 0) {
        rknn_destroy(ctx);
        printf("2. NPU 资源已释放\n");
        ctx = 0; /* 句柄清零，防止野指针 */
    }
}


int convert_yuyv_to_rgb(unsigned char *yuyv_data,int src_width,int src_hight,unsigned char *rgb_data){
    //NPU模型要求的输入宽度
    int dst_width = 640;
    //NPU模型要求的输入高度
    int dst_hight  = 640;

    /*遍历目标图像(640x640)的每一个像素点*/
    for(int y = 0;y < dst_hight;y++){
        for (int x = 0; x < dst_width; x++){
              /* =========================================================
             * 步骤 1：坐标映射（解决尺寸不一致问题）
             * 我们的目标是找出 640x640 画面中的 (x,y) 像素，
             * 对应的是 640x480 原图中的哪个坐标 (src_x, src_y)。
             * 这里使用“最近邻插值法”，直接用等比例缩放公式计算。
             * ========================================================= */ 
            int src_x = (x * src_width) / dst_width;
            int src_y = (y * src_hight) / dst_hight;

            /* 防御性编程：防止由于整数除法舍入导致的数组越界（极其重要，否则会段错误） */
            if (src_x >= src_width) src_x = src_width - 1;
            if (src_y >= src_hight) src_y = src_hight - 1;

            /* =========================================================
             * 步骤 2：提取 Y、U、V 分量（解决色彩格式问题）
             * YUYV (YUV422) 的内存排布极其特殊，每 4 个字节代表 2 个像素：
             * 内存排列： [Y0] [U0] [Y1] [V0] 
             * 其中：像素 0 使用 Y0, U0, V0；像素 1 使用 Y1, U0, V0。
             * 也就是说，U 和 V 是每两个相邻像素共享的。
             * ========================================================= */
            
            // 计算当前像素在原始 YUYV 数组中的基础偏移量（每个像素占 2 字节）
            int yuyv_index = (src_y * src_width + src_x) * 2; 
            
            // Y（亮度）分量总是位于基础偏移量处
            unsigned char Y = yuyv_data[yuyv_index];
            
            unsigned char U, V;
            // 判断 src_x 是奇数还是偶数，来获取正确的共享 U、V 值
            if (src_x % 2 == 0) {
                // 如果是偶数像素（比如第 0 个像素），U在+1，V在+3
                U = yuyv_data[yuyv_index + 1];
                V = yuyv_data[yuyv_index + 3];
            } else {
                // 如果是奇数像素（比如第 1 个像素），U在-1，V在+1
                U = yuyv_data[yuyv_index - 1];
                V = yuyv_data[yuyv_index + 1];
            }

            /* =========================================================
             * 步骤 3：YUV 到 RGB 的数学转换
             * 使用标准的 BT.601 色彩空间转换公式。
             * 注意：U 和 V 在计算机中通常以 128 为中心偏移（即 0 对应 -128），
             * 所以公式中会减去 128 进行还原。
             * ========================================================= */
            int r = Y + 1.402 * (V - 128);
            int g = Y - 0.344136 * (U - 128) - 0.714136 * (V - 128);
            int b = Y + 1.772 * (U - 128);

            /* =========================================================
             * 步骤 4：防止色彩溢出（Clamping）
             * YUV 转 RGB 的浮点运算结果可能会小于 0 或大于 255，
             * 但 RGB 每个通道只占 1 个字节（UINT8），必须强制截断。
             * ========================================================= */
            if (r > 255) r = 255; else if (r < 0) r = 0;
            if (g > 255) g = 255; else if (g < 0) g = 0;
            if (b > 255) b = 255; else if (b < 0) b = 0;

            /* =========================================================
             * 步骤 5：写入目标 RGB 数组（NHWC 内存排布）
             * 前面 rknn_query 查出来模型要求 NHWC 格式。
             * 即：像素1_R, 像素1_G, 像素1_B, 像素2_R, 像素2_G, 像素2_B...
             * 所以每填充一个像素，索引要向前跳 3 个字节。
             * ========================================================= */
            int rgb_index = (y * dst_width + x) * 3;
            rgb_data[rgb_index]     = (unsigned char)r; // R
            rgb_data[rgb_index + 1] = (unsigned char)g; // G
            rgb_data[rgb_index + 2] = (unsigned char)b; // B
        }
    }
           return 0;
}

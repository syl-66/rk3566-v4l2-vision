#include "rknn_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rknn_api.h" /* 引入瑞芯微官方的 NPU 头文件 */

/*
自己的理解
第一步
创建全局变量 ctx 让NPU知道我用的是哪一个模型，ctx跟fd一个作用，类似于给一个身份id

rknn_engine_init
第一步
通过rknn_init(&ctx,(void *)model_path,0,0,NULL);让NPU知道用的是什么模型，给ctx一个id

第二步
通过rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));获取模型输入/输出节点个数

第三步
通过rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &input_attr, sizeof(input_attr))；获取输入节点的具体属性；

第四步
rknn_destroy(ctx);关闭 NPU，释放内存，ctx = 0; 句柄清零，防止野指针 

convert_yuyv_to_rgb
第一步 
坐标映射，把640x480 换成 640x640

第二步
提取y
int yuyv_index = (src_y * src_width + src_x) * 2; 
unsigned char Y = yuyv_data[yuyv_index];

y总是排在内存的第一个位置，根据y在奇数位/偶数位 算出u v位置


第三步
用数学公式把 yuyv 转化为 rgb
int r = Y + 1.402 * (V - 128);
int g = Y - 0.344136 * (U - 128) - 0.714136 * (V - 128);
int b = Y + 1.772 * (U - 128);

第五步
写入rgb数组

rknn_engine_infer
第一步
rknn_input inputs[1];，构造 NPU 输入张量描述符 

第二步
rknn_inputs_set(ctx,1,inputs);，将数据提交至 NPU 内存

第三步
rknn_run(ctx,NULL);，触发 NPU 硬件计算

第四步
实例化接收结果的结构体数组， rknn_output outputs[3];
获取 NPU 推理结果，rknn_outputs_get(ctx, 3, outputs, NULL);发送指令，将结果从 NPU 内存拷贝回用户态内存

第五步
释放内存 rknn_outputs_release(ctx, 3, outputs);
*/

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
    
    /* 
     * 查询模型输入输出张量数量
     * 参数 1 (ctx)：RKNN 上下文句柄（NPU 身份标识）。
     * 参数 2 (cmd)：查询指令枚举，RKNN_QUERY_IN_OUT_NUM 表示查询输入/输出节点个数。
     * 参数 3 (info)：输出参数。传入结构体地址，底层会将查询到的数量填充至此。
     * 参数 4 (size)：防御性参数。告知底层该结构体的字节大小，防止写入越界。
     */
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
    
     /* 
     * 查询输入张量的详细规格说明（尺寸、通道、内存格式等）
     * 参数 1 (ctx)：RKNN 上下文句柄。
     * 参数 2 (cmd)：RKNN_QUERY_INPUT_ATTR，表示查询输入节点的属性。
     * 参数 3 (info)：指向 rknn_tensor_attr 结构体的指针，用于接收详细的属性参数。
     * 参数 4 (size)：该结构体的内存大小，供底层校验防越界。
     */
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

/**
 * @brief 执行 RKNN NPU 硬件推理
 * 
 * @details 该函数负责将经过预处理（尺寸缩放、色彩空间转换）的 RGB 图像数据，
 *          封装为 RKNN 的输入张量结构体，送入 NPU 硬件进行神经网络前向计算。
 * 
 * @param[in] rgb_data 指向符合模型输入规格（640x640x3, UINT8, NHWC）的 RGB 图像数据缓冲区指针
 * 
 * @return int 成功返回 0，失败返回 -1
 */
int rknn_engine_infer(unsigned char *rgb_data){
    int ret;
    /* ================= 1. 构造 NPU 输入张量描述符 ================= */
    rknn_input inputs[1];

    /* 将结构体清零，防止未初始化的垃圾字段导致底层解析异常 */
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index  = 0;                     /* 指定操作第 0 个输入节点 */
    inputs[0].type   = RKNN_TENSOR_UINT8;     /* 数据类型：8位无符号整型 (0~255) */
    inputs[0].size   = 640 * 640 * 3;         /* 数据总字节数：宽 * 高 * 通道数 */
    inputs[0].fmt    = RKNN_TENSOR_NHWC;      /* 内存排布格式：NHWC (像素交错排列) */
    inputs[0].buf    = rgb_data;              /* 挂载用户空间的图像数据地址 */

   /* ================= 2. 将数据提交至 NPU 内存 ================= */
    /* 
     * 调用 rknn_inputs_set 通知驱动将用户空间数据同步至 NPU 可访问的内存区域。
     * 参数 1: 上下文句柄，
     * 参数 2: 输入节点数量，
     * 参数 3: 输入描述符数组。
     */
    ret = rknn_inputs_set(ctx,1,inputs);

    if (ret < 0) {
        printf("rknn_inputs_set failed! error code = %d\n", ret);
        return -1;
    }

/* ================= 3. 触发 NPU 硬件计算 ================= */
    /* 
     * 调用 rknn_run 启动硬件加速器执行网络前向推理。
     * 该操作为同步阻塞式，耗时通常取决于模型的复杂度（YOLOv5s 约数十毫秒）。
     */
    ret = rknn_run(ctx,NULL);
    if (ret < 0) {
        printf("rknn_run failed! error code = %d\n", ret);
        return -1;
    }

    /* ================= 4. 获取 NPU 推理结果 ================= */
    
    /* 实例化接收结果的结构体数组，YOLOv5 有 3 个输出节点 */
    rknn_output outputs[3];
    memset(outputs, 0, sizeof(outputs));

    for (int i = 0; i < 3; i++) {
        outputs[i].index = i;      /* 指定获取第 i 个输出 */
        outputs[i].want_float = 1; /* 告诉 NPU：给我输出浮点数格式（而不是量化的整数） */
    }

    /* 发送指令，将结果从 NPU 内存拷贝回用户态内存 */
    ret = rknn_outputs_get(ctx, 3, outputs, NULL);
    if (ret < 0) {
        printf("rknn_outputs_get failed! error code = %d\n", ret);
        return -1;
    }

    printf("成功获取 NPU 输出结果！\n");
    
    /* 为了测试，先打印一下第 0 个输出张量的大小和前几个数字 */
    printf("输出节点 0 的大小: %d 字节\n", outputs[0].size);
    float *data = (float *)outputs[0].buf;
    printf("输出节点 0 的前 5 个数字: %f, %f, %f, %f, %f\n", data[0], data[1], data[2], data[3], data[4]);
    
    /* 极其重要：用完输出结果后，必须释放它，否则会内存泄漏！ */
    rknn_outputs_release(ctx, 3, outputs);
    

    printf("NPU inference completed successfully.\n");

    return 0;
}

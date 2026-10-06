#pragma once

#include <stdint.h>
#include <stddef.h>

/*
 * 从灰度图解码 EAN-13 条码（ISBN 书号的标准编码）。
 *
 * 参数:
 *   gray   灰度图像缓冲区，每像素 1 字节（0=黑，255=白）
 *   width  图像宽度
 *   height 图像高度
 *   out    成功时输出 13 个 ASCII 数字（不含结尾 '\0'）
 *
 * 返回:
 *   true  解码成功（校验位已验证）
 *   false 未找到可解码的条码
 *
 * 说明: 在图像中间带采样多条水平扫描线逐个尝试，对轻度倾斜（约 ±30°）可容错；
 * 条码接近水平时识别率最高。请勿将条码旋转 90°。
 */
bool ean13_decode(const uint8_t *gray, int width, int height, char out[13]);

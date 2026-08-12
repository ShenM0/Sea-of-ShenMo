/**
 * @file hiwonder_err.h
 * @author LuYongping
 * @brief Error code definition
 * @version 0.1
 * @date 2024-01-13
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#ifndef HIWONDER_ERR_H
#define HIWONDER_ERR_H

#include "hiwonder_err.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HIWONDER_OK = 0,
    HIWONDER_ERR = -1,
    HIWONDER_ERR_INVALID_PARAM = -2,
    HIWONDER_ERR_INVALID_STATE = -3,
    HIWONDER_ERR_INVALID_ID = -4,
    HIWONDER_ERR_INVALID_TICKS = -5,
    HIWONDER_ERR_INVALID_REPEAT = -6,
    HIWONDER_ERR_INVALID_STAGE = -7,
    HIWONDER_ERR_INVALID_TICKS_ON = -8,
    HIWONDER_ERR_INVALID_TICKS_OFF = -9,
    HIWONDER_ERR_INVALID_TICKS_PERIOD = -10,
    HIWONDER_ERR_INVALID_TICKS_PERIOD_ON = -11,
    HIWONDER_ERR_INVALID_TICKS_PERIOD_OFF = -12,
    HIWONDER_ERR_INVALID_TICKS_PERIOD_REPEAT = -13,
    HIWONDER_ERR_NULL_POINTER = -14,
    HIWONDER_ERR_INVALID_PACKET_FUNC = -15,
} HiWonderErr;


#ifdef __cplusplus
}   
#endif


#endif
/***********************************************************************************************************************
* DISCLAIMER
* This software is supplied by Renesas Electronics Corporation and is only intended for use with Renesas products. No 
* other uses are authorized. This software is owned by Renesas Electronics Corporation and is protected under all 
* applicable laws, including copyright laws. 
* THIS SOFTWARE IS PROVIDED 'AS IS' AND RENESAS MAKES NO WARRANTIES REGARDING
* THIS SOFTWARE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY, 
* FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. ALL SUCH WARRANTIES ARE EXPRESSLY DISCLAIMED. TO THE MAXIMUM
* EXTENT PERMITTED NOT PROHIBITED BY LAW, NEITHER RENESAS ELECTRONICS CORPORATION NOR ANY OF ITS AFFILIATED COMPANIES 
* SHALL BE LIABLE FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES FOR ANY REASON RELATED TO THIS 
* SOFTWARE, EVEN IF RENESAS OR ITS AFFILIATES HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
* Renesas reserves the right, without notice, to make changes to this software and to discontinue the availability of 
* this software. By using this software, you agree to the additional terms and conditions found by accessing the 
* following link:
* http://www.renesas.com/disclaimer 
*
* Changed from original python code to C source code.
* Copyright (C) 2017 Renesas Electronics Corporation. All rights reserved.
***********************************************************************************************************************/
/***********************************************************************************************************************
* File Name    : layer_shapes_fruit.h
* Version      : 1.00
* Description  : Initializations
***********************************************************************************************************************/
/**********************************************************************************************************************
* History : DD.MM.YYYY Version  Description
*         : 16.06.2017 1.00     First Release
***********************************************************************************************************************/

#include "Typedef.h"
#include <stdlib.h>
#ifndef LAYER_SHAPES_FRUIT_H_
#define LAYER_SHAPES_FRUIT_H_
 
TsOUT* dnn_compute_fruit(TsIN*, TsInt*);
 
TsInt8 dnn_buffer1[110592];
TsInt8 dnn_buffer2[46080];
TsOUT StatefulPartitionedCall_0[4];
 
struct shapes_fruit{
    TFloat tfl_quantize_shape[3];
    TsInt sequential_1_conv2d_Relu_shape[16];
    TsInt sequential_1_max_pooling2d_MaxPool_shape[11];
    TsInt sequential_1_conv2d_1_Relu_shape[16];
    TsInt sequential_1_max_pooling2d_1_MaxPool_shape[11];
    TsInt sequential_1_conv2d_2_Relu_shape[16];
    TsInt sequential_1_max_pooling2d_2_MaxPool_shape[11];
    TsInt sequential_1_max_pooling2d_2_MaxPool_tr_shape[5];
    TsInt sequential_1_dense_MatMul_shape[7];
    TsInt sequential_1_dense_1_MatMul_shape[7];
    TsInt StatefulPartitionedCall_01_shape;
    TFloat StatefulPartitionedCall_0_shape[3];
};
 
struct shapes_fruit layer_shapes_fruit ={
    {27648,0.003737024264410138,-128},
    {1,3,96,96,12,3,3,3,96,96,1,1,1,1,1,1},
    {1,12,96,96,48,48,2,2,2,2,0},
    {1,12,48,48,20,12,3,3,48,48,1,1,1,1,1,1},
    {1,20,48,48,24,24,2,2,2,2,0},
    {1,20,24,24,24,20,3,3,24,24,1,1,1,1,1,1},
    {1,24,24,24,12,12,2,2,2,2,0},
    {1,24,12,12,1},
    {1,3456,3456,32,1,1,0},
    {1,32,32,4,1,1,0},
    4,
    {4,0.00390625,-128}
};
 
#endif

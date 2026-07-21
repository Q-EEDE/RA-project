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
* File Name    : dnn_compute_fruit.c
* Version      : 1.00
* Description  : The function calls
***********************************************************************************************************************/
/**********************************************************************************************************************
* History : DD.MM.YYYY Version  Description
*         : 16.06.2017 1.00     First Release
***********************************************************************************************************************/

 
#include "layer_shapes_fruit.h"
#include "layer_graph_fruit.h"
#include "weights_fruit.h"
 
TsOUT* dnn_compute_fruit(TsIN* serving_default_input_1_0, TsInt *errorcode)
{
  TsInt i;
  TsInt channel;
  TsInt channels;
  TsInt pixels_per_channel;
  *errorcode = 0;

  /* The application stores pixels as RGBRGB (HWC), while the generated
   * convolution reads one complete channel at a time (CHW). Reorder into the
   * existing DNN work buffer while converting UINT8 input to INT8. */
  channels = (TsInt) layer_shapes_fruit.sequential_1_conv2d_Relu_shape[1];
  pixels_per_channel = (TsInt) layer_shapes_fruit.tfl_quantize_shape[0] / channels;
  for(i = 0; i < pixels_per_channel; i++)
  {
    for(channel = 0; channel < channels; channel++)
    {
      dnn_buffer1[(channel * pixels_per_channel) + i] =
        (TsInt8) (serving_default_input_1_0[(i * channels) + channel] +
                  layer_shapes_fruit.tfl_quantize_shape[2]);
    }
  }
  convolution(dnn_buffer1,dnn_buffer2,sequential_1_conv2d_Relu_weights,sequential_1_conv2d_Relu_biases,sequential_1_conv2d_Relu_multiplier,sequential_1_conv2d_Relu_shift,sequential_1_conv2d_Relu_offset,dnn_buffer1,layer_shapes_fruit.sequential_1_conv2d_Relu_shape,errorcode);
  pooling_without_pad(dnn_buffer1,dnn_buffer2,layer_shapes_fruit.sequential_1_max_pooling2d_MaxPool_shape,errorcode);
  convolution(dnn_buffer2,dnn_buffer1,sequential_1_conv2d_1_Relu_weights,sequential_1_conv2d_1_Relu_biases,sequential_1_conv2d_1_Relu_multiplier,sequential_1_conv2d_1_Relu_shift,sequential_1_conv2d_1_Relu_offset,dnn_buffer2,layer_shapes_fruit.sequential_1_conv2d_1_Relu_shape,errorcode);
  pooling_without_pad(dnn_buffer2,dnn_buffer1,layer_shapes_fruit.sequential_1_max_pooling2d_1_MaxPool_shape,errorcode);
  convolution(dnn_buffer1,dnn_buffer2,sequential_1_conv2d_2_Relu_weights,sequential_1_conv2d_2_Relu_biases,sequential_1_conv2d_2_Relu_multiplier,sequential_1_conv2d_2_Relu_shift,sequential_1_conv2d_2_Relu_offset,dnn_buffer1,layer_shapes_fruit.sequential_1_conv2d_2_Relu_shape,errorcode);
  pooling_without_pad(dnn_buffer1,dnn_buffer2,layer_shapes_fruit.sequential_1_max_pooling2d_2_MaxPool_shape,errorcode);
   
  transpose4d(dnn_buffer2,dnn_buffer1,layer_shapes_fruit.sequential_1_max_pooling2d_2_MaxPool_tr_shape,errorcode);
   
  innerproduct(dnn_buffer1,sequential_1_dense_MatMul_weights,sequential_1_dense_MatMul_biases,sequential_1_dense_MatMul_multiplier,sequential_1_dense_MatMul_shift,sequential_1_dense_MatMul_offset,dnn_buffer2,layer_shapes_fruit.sequential_1_dense_MatMul_shape,errorcode);
   
  innerproduct(dnn_buffer2,sequential_1_dense_1_MatMul_weights,sequential_1_dense_1_MatMul_biases,sequential_1_dense_1_MatMul_multiplier,sequential_1_dense_1_MatMul_shift,sequential_1_dense_1_MatMul_offset,dnn_buffer1,layer_shapes_fruit.sequential_1_dense_1_MatMul_shape,errorcode);
  softmax(dnn_buffer1,StatefulPartitionedCall_01_multiplier,StatefulPartitionedCall_01_offset,dnn_buffer1,layer_shapes_fruit.StatefulPartitionedCall_01_shape,errorcode);
  //postprocessing of the INT8 output to UINT8;
  for(i = 0; i < layer_shapes_fruit.StatefulPartitionedCall_0_shape[0]; i++)
  {
    StatefulPartitionedCall_0[i]  = (dnn_buffer1[i]-layer_shapes_fruit.StatefulPartitionedCall_0_shape[2]);
  }
  return(StatefulPartitionedCall_0);
}

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
/**********************************************************************************************************************
* Copyright 2015 Google Inc. All Rights Reserved.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
***********************************************************************************************************************/

/***********************************************************************************************************************
* File Name    : network_fruit.c
* Version      : 1.00
* Description  : Definitions of all functions
***********************************************************************************************************************/
/**********************************************************************************************************************
* History : DD.MM.YYYY Version  Description
*         : 16.06.2017 1.00     First Release
***********************************************************************************************************************/

#include "stdlib.h"
#include "Typedef.h"
#include "math.h"
#include "layer_graph_fruit.h"
#define C_INT8 1
/***********************************************************************************************************************
* Function Name: padding
* Description  : - Padding operation
*				 - Creates a new output matrix with zero's padded on the original input 
* Arguments    : dData		- Pointer to the input data
*                in_offset  - value to the input zero point  
*				 dPad		- Pointer to the padding output
*				 iShapes	- Dimensions 
*					( N, input channels, input height, input width, output height, output width ,pad top, pad left)
*                errorcode - errorcode if any issue
* Return Value : no return value
***********************************************************************************************************************/
void padding(TsInt8 *dData, TsInt8 *offset, TsInt8 *dPad, TsInt *iShapes,TsInt *errorcode)
{
	if (*errorcode!=1)
	{
		TsInt iD1,iD2,iD3,iD4;
		TsInt iN = iShapes[0];		// Number of examples
		TsInt iC = iShapes[1];		// Number of channels
		TsInt iH = iShapes[2];		// Input image height
		TsInt iW = iShapes[3];		// Input image width
		TsInt iPH = iShapes[4];		// Output height
		TsInt iPW = iShapes[5];		// Output width
		TsInt pad_top = iShapes[6];	// Number of pixels to be added to the height
		TsInt pad_left = iShapes[7];	// Number of pixels to be added to the width

		// Padding operation
		for(iD1=0; iD1 < iN*iC*iPH*iPW; iD1++)
			dPad[iD1]= offset[0];

		for(iD2 = 0; iD2 < iC; iD2++)
		{
			for(iD3 = pad_top; iD3 < iH + pad_top; iD3++)
			{				
				for(iD4 = pad_left; iD4 < iW + pad_left; iD4++)
				{
					dPad[(iD2*iPH*iPW)+(iD3*iPW)+(iD4)] = dData[(iD2*iH*iW)+((iD3-pad_top)*iW)+(iD4-pad_left)];
				}
			}
		}
	}
	
}

/***********************************************************************************************************************
* Function Name: averae pooling padding
* Description  : - Padding operation
*				 - Creates a new output matrix with zero's padded on the original input 
* Arguments    : dData		- Pointer to the input data
*                offset     - Pointer to the zero point value  
*				 dPad		- Pointer to the padding output
*				 iShapes	- Dimensions 
*					( N, input channels, input height, input width, output height, output width ,pad top, pad left)
* Return Value : no return value
***********************************************************************************************************************/
void avgpool_padding(TsInt8 *dData, TPrecision *dAve, TsInt *iShapes, TsInt *errorcode)
{
	if (*errorcode!=1)
	{
		TsInt iD1,iD2,iD3,iD4;
		TsInt iN = iShapes[0];		// Number of examples
		TsInt iC = iShapes[1];		// Number of channels
		TsInt iH = iShapes[2];		// Input image height
		TsInt iW = iShapes[3];		// Input image width
		TsInt iPH = iShapes[4];		// Output height
		TsInt iPW = iShapes[5];		// Output width
		TsInt pad_top = iShapes[6];	// Number of pixels to be added to the height
		TsInt pad_left = iShapes[7];	// Number of pixels to be added to the width

		// Padding operation
		for(iD1=0; iD1 < iN*iC*iPH*iPW; iD1++)
			dAve[iD1]= -1000;

		for(iD1=0; iD1 < iN; iD1++)
		{
			for(iD2 = 0; iD2 < iC; iD2++)
			{
				for(iD3 = pad_top; iD3 < iH + pad_top; iD3++)
				{				
					for(iD4 = pad_left; iD4 < iW + pad_left; iD4++)
					{
						dAve[(iD1*iC*iPH*iPW)+(iD2*iPH*iPW)+(iD3*iPW)+(iD4)] = (TPrecision)dData[(iD1*iC*iH*iW)+(iD2*iH*iW)+((iD3-pad_top)*iW)+(iD4-pad_left)];
					}
				}
			}
		}
	}
	
}


/***********************************************************************************************************************
* Function Name: convolution
* Description  : - Convolution layer
*				 - Performs elementwise multiplication of selected input data and weights and add them up with biases 
*				   by taking in the filter size and strides as the convolution parameters
* Arguments    : dData		- Pointer to the input data
*				 dPad		- Pointer to the padding output
*			 	 dWeights	- Pointer to the weights
*				 dBiases	- Pointer to the biases
*                scale      - Pointer to the multiplier value
*                shift      - Pointer to the shift value
*                offset     - Pointer to the zero point value   
*				 dOut		- Pointer to the convolution output (to be filled with values during convolution operation)
*				 iShapes	- Dimensions 
*					( N, Channels, input height, input width, No. of filters, channels, filter height, filter width, 
*					  output height,output width,pad top, pad bottom, pad left, pad right, stride height,stride width)
*                 errorcode - errorcode if any issue  
* Return Value : no return value
***********************************************************************************************************************/
void convolution(TsInt8 *dData, TsInt8 *dPad, TsInt8 *dWeights, TsInt32 *dBiases, TsInt32 *scale, TsInt8 *shift, TsInt8 *offset, TsInt8 *dOut, TsInt *iShapes, TsInt *errorcode)
{
    if (*errorcode!=1)
    {
        TsInt iD2,iD3,iD4;   // Dimensions
        TsInt iItr1,iItr2,iItr3;
        TsInt iC = iShapes[1];     // Number of channels
        TsInt iH = iShapes[2];     // Input image height
        TsInt iW = iShapes[3];     // Input image width
        TsInt iF = iShapes[4];     // Number of filters
        TsInt iFH = iShapes[6];    // Filter height
        TsInt iFW = iShapes[7];    // Filter width
        TsInt iHp = iShapes[8];    // Output height
        TsInt iWp = iShapes[9];    // Output width

        TsInt pad_top = iShapes[10];	//No. of pixels to pad on top of input
        TsInt pad_bottom = iShapes[11];	//No. of pixels to pad on bottom of input
        TsInt pad_left = iShapes[12];	//No. of pixels to pad on left of input
        TsInt pad_right = iShapes[13];	//No. of pixels to pad on right of input

        TsInt iSH = iShapes[14];  // Stride height
        TsInt iSW = iShapes[15];  // Stride width

        TsInt iws, ihs, iPH, iPW ;
        TsInt pad_shapes[] = {1, iC, iH, iW, iH + pad_top + pad_bottom, iW + pad_left + pad_right, pad_top, pad_left};
        
        iPH = iH + pad_top + pad_bottom;   // Padded input height
        iPW = iW + pad_left + pad_right;	   // Padded input width

       
        if (pad_top!=0 || pad_bottom!=0 || pad_left!=0 || pad_right!=0)
        {
            padding(dData, offset, dPad, pad_shapes, errorcode);
        }
        else
        {
            dPad = dData;
        }
        // convolution operation
        TPrecision dvalue, floor_val, diff;
        TPrecision right_shift;
        TsInt64 quantized_val, a_64, b_64, ab_64;
        TsInt32 nudge, ab_x2_high32;



        // Filtering operation
        for (iD2=0; iD2<iF; iD2++)
        {
            quantized_val = scale[iD2];
            right_shift = (1 << shift[iD2]);

            for (iD3=0; iD3<iHp;iD3++)
            {
                ihs = iD3 * iSH;
                for (iD4=0; iD4<iWp; iD4++)
                {
                    iws = iD4 * iSW;
                    dvalue=0;
                    for(iItr1=0; iItr1<iC; iItr1++)
                    {
                        for(iItr2=ihs; iItr2<(ihs+iFH); iItr2++)
                        {
                            for(iItr3=iws; iItr3<(iws+iFW); iItr3++)
                            {
                                dvalue += (TPrecision)(dPad[(iItr1*iPH*iPW)+(iItr2*iPW)+iItr3] - offset[0]) * dWeights[(iD2*iC*iFH*iFW)+(iItr1*iFH*iFW)+((iItr2-ihs)*iFW)+(iItr3-iws)];
                            }
                        }
                    }
                    if (dBiases) 
                    {
                        dvalue = dvalue +(TPrecision) dBiases[iD2];
                    }
                    else 
                    {
                        dvalue = dvalue;
                    }

                    a_64 = (TsInt32)dvalue;
                    b_64 = (TsInt32)quantized_val;
                    ab_64 = a_64 * b_64;
                    
                    if (ab_64 >= 0)
                        nudge = 1073741824;//(1 << 30);
                    else
                        nudge = -1073741823;//(1 - (1 << 30));

                    ab_x2_high32 = (TsInt32)((ab_64 + nudge) / 2147483648);//(1ll << 31);
                    dvalue = (TPrecision)(ab_x2_high32)/right_shift;

                    floor_val = (TPrecision)(floor(dvalue));
                    diff = dvalue - floor_val;
                    if (diff < 0.5f)
                    {
                        dvalue = floor_val;
                    }
                    else
                    {
                        dvalue = floor_val + 1.0f;
                    }
                    dvalue = (dvalue + offset[1]);
                    if (dvalue < offset[3])
                    {
                        dOut[(iD2*iHp*iWp)+(iD3*iWp)+iD4] = offset[3];
                    }
                    else if(dvalue > 127)
                    {
                        dOut[(iD2*iHp*iWp)+(iD3*iWp)+iD4] = 127;      // output
                    }
                    else
                    {
                        dOut[(iD2*iHp*iWp)+(iD3*iWp)+iD4] =(TsInt8) dvalue;
                    }
                }
            }
        }
    }
    
}

/***********************************************************************************************************************
* Function Name: average_pooling_without_pad
* Description  : - AVERAGE pooling layer without padding operation.
*				 - Creates a new output matrix where each element is the average of a region in the original input.
* Arguments    : dData		- Pointer to the input data
*				 dOut		- Pointer to the average pooling output
*				 iShapes	- Dimensions 
*					( N, input channels, input height, input width, output height, output width, pool height, 
*					   pool width, stride height, stride width, pooling type)
*                errorcode - errorcode if any issue
* Return Value : no return value
***********************************************************************************************************************/
void average_pooling_without_pad(TsInt8 *dData, TsInt8 *dOut, TsInt *iShapes,TsInt *errorcode)
{
	if (*errorcode!=1)
	{
		TsInt sD2,sD3,sD4;			// Array dimension
		TsInt sInnerItr,sOuterItr,sOffset;
		TsInt iC = iShapes[1];			//Input Channels
		TsInt iH = iShapes[2];			// Input data height
		TsInt iW = iShapes[3];			// Input data width
		TsInt iHp = iShapes[4];			// Output data height
		TsInt iWp = iShapes[5];			// Output data width

		TsInt PH = iShapes[6];			// Pool height
		TsInt PW = iShapes[7];			// Pool width
		TsInt stride_H = iShapes[8];	// Stride height
		TsInt stride_W = iShapes[9];	// Stride width

		TsInt iWs, iHs, k;
		TsInt dSum;

		// Average pooling operation
		for (sD2=0; sD2<iC; sD2++)
		{
			for (sD3=0; sD3<iHp; sD3++)
			{
				iHs = sD3 * stride_H;
				for (sD4=0; sD4<iWp; sD4++)
				{
					iWs = sD4 * stride_W;
					dSum = 0.0;
					k = 0;
					for(sInnerItr=iHs; sInnerItr<(iHs+PH); sInnerItr++)
					{
						for(sOuterItr=iWs; sOuterItr<(iWs+PW); sOuterItr++)
						{
							sOffset = (sD2*iH*iW)+(sInnerItr*iW)+sOuterItr;
							dSum += dData[sOffset];
							k = k+1;
						}
					}
					if(dSum > 0)
						dSum = (dSum + k / 2) / k;
					else
						dSum = (dSum - k / 2) / k;
					if (dSum < -128)
					{
						dOut[(sD2*iHp*iWp)+(sD3*iWp)+sD4] = -128;
					}
					else if(dSum > 127)
					{
						dOut[(sD2*iHp*iWp)+(sD3*iWp)+sD4] = 127;      // output
					}
					else
					{
						dOut[(sD2*iHp*iWp)+(sD3*iWp)+sD4] =(TsInt8) dSum;
					}
				}
			}
		}		
	}
	
}

/***********************************************************************************************************************
* Function Name: max_pooling_without_pad
* Description  : - MAX Pooling layer without padding operation.
*				 - taking the most responsive node of the given TsInterest region
*				 - Creates a new output matrix where each element is the max of a region in the original input
* Arguments    : dData		- Pointer to the input data
*				 dOut		- Pointer to the max pooling output
*				 iShapes	- Dimensions 
*					( N, input channels, input height, input width, output height, output width, pool height, 
*					  pool width, stride height, stride width, pooling type)
*                errorcode - errorcode if any issue
* Return Value : no return value
***********************************************************************************************************************/
void max_pooling_without_pad(TsInt8 *dData, TsInt8 *dOut, TsInt *iShapes,TsInt *errorcode)
{
	if (*errorcode!=1)
	{
		TsInt sD2,sD3,sD4;			// Array dimension
		TsInt sInnerItr,sOuterItr,sOffset;
		TsInt iC = iShapes[1];			//Input Channels
		TsInt iH = iShapes[2];			// Input data height
		TsInt iW = iShapes[3];			// Input data width
		TsInt iHp = iShapes[4];			// Output data height
		TsInt iWp = iShapes[5];			// Output data width

		TsInt PH = iShapes[6];			// Pool height
		TsInt PW = iShapes[7];			// Pool width
		TsInt stride_H = iShapes[8];	// Stride height
		TsInt stride_W = iShapes[9];	// Stride width

		TsInt iWs, iHs, flag;
		TsInt dMax= dData[0];

		for (sD2=0; sD2<iC; sD2++)
		{
			for (sD3=0; sD3<iHp; sD3++)
			{
				iHs = sD3 * stride_H;
				for (sD4=0; sD4<iWp; sD4++)
				{
					iWs = sD4 * stride_W;
					flag = 0;
					for(sInnerItr=iHs; sInnerItr<(iHs+PH); sInnerItr++)
					{
						for(sOuterItr=iWs; sOuterItr<(iWs+PW); sOuterItr++)
						{
							sOffset = (sD2*iH*iW)+(sInnerItr*iW)+sOuterItr;
							if(flag == 0)
							{
								dMax=dData[sOffset];
								flag = 1;
							}
							else
							{
								if(dMax < dData[sOffset])
									dMax = dData[sOffset];
							}
						}
					}
					if (dMax < -128)
					{
						dOut[(sD2*iHp*iWp)+(sD3*iWp)+sD4] = -128;
					}
					else if(dMax > 127)
					{
						dOut[(sD2*iHp*iWp)+(sD3*iWp)+sD4] = 127;      // output
					}
					else
					{
						dOut[(sD2*iHp*iWp)+(sD3*iWp)+sD4] = (TsInt8)dMax;
					}
				}
			}
		}
	}
	
}

/***********************************************************************************************************************
* Function Name: pooling_without_pad
* Description  : - Pooling layer without padding operation.
*				 - Creates a new output matrix where each element is filled based on the pooling type
*					(either max pooling or average pooling)
* Arguments    : dData		- Pointer to the input data
*				 dOut		- Pointer to the pooling output
*				 iShapes	- Dimensions
*					( N, input channels, input height, input width, output height, output width, pool height,
*					  pool width, stride height, stride width, pooling type)
*                errorcode - errorcode if any issue
* Return Value : no return value
***********************************************************************************************************************/
void pooling_without_pad(TsInt8 *dData, TsInt8 *dOut, TsInt *iShapes,TsInt *errorcode)
{
	TsInt ipool_type = iShapes[10];		//Type of Pooling
	if (ipool_type == 0){		// MAX pooling
		max_pooling_without_pad(dData, dOut, iShapes,errorcode);
	}
	else{						// AVERAGE pooling
		average_pooling_without_pad(dData, dOut, iShapes,errorcode);
	}
}


/***********************************************************************************************************************
* Function Name: transpose
* Description  : Performs matrix transpose
* Arguments    : dData		- array of input data
*				 dOut		- placeholder for the output
*				 iShapes	- dimensions of input array + flag to represent transpose axis (i.e., data)
*                errorcode - errorcode if any issue
* Return Value : no return value
***********************************************************************************************************************/
void transpose4d(TsInt8 *data, TsInt8 *output, TsInt *iShapes, TsInt *errorcode)
{    
    if (*errorcode!=1)
    {
        TsInt flag = iShapes[4];

        // Execute RXDSP or CMSIS Transpose4d or C Transpose 4d or CCRX complier or CCRL complier
        TsInt c = 0;
        TsInt i,j;
        if(flag == 0){      // NHWC to NCHW
            TsInt N = iShapes[0]; // Number of samples
            TsInt H = iShapes[1]; // Number of channels
            TsInt W = iShapes[2]; // Input image height
            TsInt C = iShapes[3]; // Input image width
            
            for(i = 0; i < N*C ; i++){
                for(j = 0; j < H*W ; j++){        
                    output[c] = data[i+(j*C)];
                    c++;
                }
            }

        }
        else if(flag == 1){     // NCHW to NHWC
            TsInt N = iShapes[0]; // Number of samples
            TsInt C = iShapes[1]; // Number of channels
            TsInt H = iShapes[2]; // Input image height
            TsInt W = iShapes[3]; // Input image width

            for(i = 0; i < H*W ; i++){
                for(j = 0; j < N*C ; j++){      
                    output[c] = data[i+(j*H*W)];
                    c++;
                }

            }
        }
    }
    
}

/***********************************************************************************************************************
* Function Name: innerproduct
* Description  : - Fully connected layer
*                - Performs dot product of data and weights and add them up with biases
*                   (Matrix Multiplication of data and weights and addition of biases)
* Arguments    : data           - Array of input data
*                weights        - Array of weights (transposed)
*                biases 		- Array of biases
*                scale          - Pointer to the multiplier value
*                shift          - Pointer to the shift value
*                offset         - Pointer to the zero point value  
*                out            - Placeholder for the output
*                shapes         - Dimensions of data and weights (N, D, F, D)
*                errorcode - errorcode if any issue
* Return Value : no return value
***********************************************************************************************************************/
void innerproduct(TsInt8 *data, TsInt8 *weights, TsInt32 *biases, TsInt32 *scale, TsInt8 *shift, TsInt8 *offset, TsInt8 *out, TsInt *shapes,TsInt *errorcode)
{
    if (*errorcode!=1)
    {
        TsInt iColumn;
        TsInt iInneritr;
        TsInt D = shapes[1];
        TsInt F = shapes[3];

        TsInt8 in_offset = offset[0];       // input offset
        TsInt8 out_offset = offset[1];      // output offset
        
        // Execute C Innerproduct or CCRX complier or CCRL complier    
        TPrecision dSum = 0;
        TPrecision floor_val, diff;
        TPrecision right_shift;
        TsInt64 quantized_val, a_64, b_64, ab_64;
        TsInt32 nudge, ab_x2_high32;

        quantized_val = scale[0];

        right_shift = (TPrecision)(1 << shift[0]);

        for(iColumn=0; iColumn<F; iColumn++)
        {
            dSum = 0;
            for(iInneritr=0; iInneritr<D;iInneritr++)
            {
                dSum += (TPrecision)(data[iInneritr]- in_offset) * weights[(iInneritr*F)+iColumn];
            }
            if(biases)
            {
                dSum = dSum +(TPrecision)biases[iColumn];				// output
            }

            a_64 = (TsInt32)dSum;
            b_64 = (TsInt32)quantized_val;
            ab_64 = a_64 * b_64;
            
            if (ab_64 >= 0)
                nudge = 1073741824;//(1 << 30);
            else
                nudge = -1073741823;//(1 - (1 << 30));

            ab_x2_high32 = (TsInt32)((ab_64 + nudge) / 2147483648);//(1ll << 31);
            dSum = (TPrecision)(ab_x2_high32)/right_shift;

            floor_val =(TPrecision)(floor(dSum));
            diff = dSum - floor_val;
            if (diff < 0.5f)
            {
                dSum = floor_val;
            }
            else
            {
                dSum = floor_val + 1.0f;
            }
            dSum = (dSum + out_offset);
            if (dSum < offset[3])
            {
                out[iColumn] = offset[3];
            }
            else if(dSum > 127)
            {
                out[iColumn] = 127;      // output
            }
            else
            {
                out[iColumn] = (TsInt8)dSum;
            }
        }
    }
    
}

/***********************************************************************************************************************
* Function Name: softmax
* Description  : - Activation function
*                - Squashes an array of arbitrary real values to an array of real values 
*                  in the range(0, 1) that add up to 1	
* Arguments    : dData      - Array of input data
*                multiplier - Pointer to the scale multiplier value
*                offset     - Pointer to the zero point value  
*                dOut       - Pointer to the output data
*                iShapes	- Size of the input array
*                errorcode - errorcode if any issue
* Return Value : no return value
***********************************************************************************************************************/
void softmax(TsInt8 *dData, TPrecision *multiplier, TsInt8 *offset, TsInt8 *dOut, TsInt iShapes, TsInt *errorcode)
{

    if (*errorcode!=1)
    {
        TPrecision dMax, dvalue, dCal, floor_val, diff, dSum = 0;
        TsInt iRow;

        dMax = dData[0];
        for (iRow = 1; iRow < iShapes; iRow++)
        {
            if (dData[iRow] > dMax)
            {
                dMax = (dData[iRow]);
            }
        }
        for (iRow = 0; iRow < iShapes; iRow++)
        {
            dvalue = (((dData[iRow])) - dMax)*multiplier[0];
            dSum = (TPrecision)(dSum + exp(dvalue));
        }
        for (iRow = 0; iRow < iShapes; iRow++)
        {
            dCal = (dData[iRow] - dMax)*multiplier[0];
            dvalue =(TPrecision)(exp(dCal)/dSum);    
            dvalue = dvalue/multiplier[1];     
            floor_val = (TPrecision)(floor(dvalue));
            diff = dvalue - floor_val;
            if (diff < 0.5f)
            {
                dvalue = floor_val;
            }
            else
            {
                dvalue = floor_val + 1.0f;
            }
            dvalue = dvalue+offset[1];
            if (dvalue < -128)
            {
                dOut[iRow] = -128;
            }
            else if(dvalue > 127)
            {
                dOut[iRow] = 127;
            }
            else
            {
                dOut[iRow] =(TsInt8) dvalue;
            }
        }
    }
    

}




/*
 * Copyright (c) 2017-2020 Arm Limited.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef ARM_COMPUTE_NEFLATTENLAYER_H
#define ARM_COMPUTE_NEFLATTENLAYER_H

#include "arm_compute/core/Types.h"
#include "arm_compute/runtime/IFunction.h"
#include "arm_compute/runtime/NEON/functions/NEReshapeLayer.h"

namespace arm_compute
{
class ITensor;
class ITensorInfo;

/** Basic function to execute flatten layer kernel. */
class NEFlattenLayer : public IFunction
{
public:
    /** Initialise the kernel's input and output.
     *
     * @param[in]  input  First input tensor to flatten with at least 3 dimensions. The dimensions over the third will be interpreted as batches. Data types supported: All
     * @param[out] output Output tensor with shape [w*h*d, input_batches] where:
     *             w = width input tensor, h = height input tensor and d = depth input tensor. Data type supported: same as @p input
     */
    void configure(const ITensor *input, ITensor *output);

    /** Static function to check if given info will lead to a valid configuration of @ref NEFlattenLayer
     *
     * @param[in]  input  First input tensor to flatten with at least 3 dimensions.
     *                    The dimensions above the third will be interpreted as batches. Data types supported: All
     * @param[out] output Output tensor with shape [w*h*d, input_batches] where:
     *                    w = width input tensor, h = height input tensor and d = depth input tensor. Data type supported: same as @p input
     *
     * @return a status
     */
    static Status validate(const ITensorInfo *input, const ITensorInfo *output);

    // Inherited methods overridden:
    void run() override;

private:
    NEReshapeLayer _reshape{};
};
} // namespace arm_compute

#endif /* ARM_COMPUTE_NEFLATTENLAYER_H */
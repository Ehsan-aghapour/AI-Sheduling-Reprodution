/*
 * Copyright (c) 2021 Arm Limited.
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
#ifndef ARM_COMPUTE_CPU_ELEMENTWISE_KERNEL_H
#define ARM_COMPUTE_CPU_ELEMENTWISE_KERNEL_H

#include "src/core/common/Macros.h"
#include "src/core/cpu/ICpuKernel.h"

namespace arm_compute
{
class ITensor;
namespace cpu
{
namespace kernels
{
/** Interface for an element-wise operation kernel
 *
 * Element-wise operation is computed by:
 * @f[ output(x,y) = OP(input1(x,y), input2(x,y))@f]
 *
 */
class CpuElementwiseKernel : public ICpuKernel
{
public:
    const char *name() const override
    {
        return "CpuElementwiseKernel";
    }

    CpuElementwiseKernel() = default;
    ARM_COMPUTE_DISALLOW_COPY_ALLOW_MOVE(CpuElementwiseKernel);

    /** Common signature for all the specialised arithmetic functions
     *
     * @param[in]  input1 First tensor input info. Data types supported: QASYMM8/S16/F16/S32/F32.
     * @param[in]  input2 Second tensor input info. Data types supported: Same as @p input1.
     * @param[out] output Output tensor info. Data types supported: Dependent on subclass.
     * @param[in]  window Region on which to execute the kernel.
     */
    using ElementwiseFunction = void(const ITensor *, const ITensor *, ITensor *, const Window &);

    // Inherited methods overridden:
    void run_op(ITensorPack &tensors, const Window &window, const ThreadInfo &info) override;

protected:
    /** Validate the argument passed to the kernel
     *
     * @param[in] input1 First tensor input. Data types supported: QASYMM8/S16/F16/S32/F32.
     * @param[in] input2 Second tensor input. Data types supported: Same as @p input1.
     * @param[in] output Output tensor. Data types supported: Dependent on subclass.
     */
    static Status validate_arguments_common(const ITensorInfo &input1, const ITensorInfo &input2, const ITensorInfo &output);

    /** Commmon configure function for element-wise operators with no additional options (e.g. Min, Max, SquaredDiff)
     *
     */
    void configure_common(const ITensorInfo *input1, const ITensorInfo *input2, ITensorInfo *output);

    /** Function to get the micro kernel implementation
     *
     * @param[in] input1 First input tensor information
     * @param[in] input2 Second input tensor information
     * @param[in] output Output tensor information
     *
     * @return the function instance for the micro kernel
     */
    virtual std::function<ElementwiseFunction> get_implementation(const ITensorInfo *input1, const ITensorInfo *input2, ITensorInfo *output) = 0;
};

class CpuArithmeticKernel : public CpuElementwiseKernel
{
public:
    /** Default constructor */
    CpuArithmeticKernel() = default;

    /** Configure kernel
     *
     * @param[in]  op     Arithmetic operation to be executed.
     * @param[in]  input1 First tensor input info. Data types supported: QASYMM8/S16/F16/S32/F32.
     * @param[in]  input2 Second tensor input info. Data types supported: Same as @p input1.
     * @param[out] output Output tensor info. Data types supported: Same as @p input1.
     */
    void configure(ArithmeticOperation op, const ITensorInfo *input1, const ITensorInfo *input2, ITensorInfo *output);

    /** Static function to check if given info will lead to a valid configuration of @ref cpu::kernels::CpuArithmeticKernel
     *
     * @param[in] op     Arithmetic operation to be executed.
     * @param[in] input1 First tensor input info. Data types supported: QASYMM8/S16/F16/S32/F32.
     * @param[in] input2 Second tensor input info. Data types supported: Same as @p input1.
     * @param[in] output Output tensor info. Data types supported: Same as @p input1.
     *
     * @return a Status
     */
    static Status validate(ArithmeticOperation op, const ITensorInfo *input1, const ITensorInfo *input2, const ITensorInfo *output);

protected:
    // Inherited methods overridden:
    static Status validate_arguments(const ITensorInfo &input1, const ITensorInfo &input2, const ITensorInfo &output);

    ArithmeticOperation _op{};

private:
    /** Function to get the micro kernel implementation
     *
     * @param[in] input1 First input tensor information
     * @param[in] input2 Second input tensor information
     * @param[in] output Output tensor information
     *
     * @return the function instance for the micro kernel
     */
    std::function<ElementwiseFunction> get_implementation(const ITensorInfo *input1, const ITensorInfo *input2, ITensorInfo *output) override;
};

class CpuDivisionKernel : public CpuArithmeticKernel
{
public:
    /** Default constructor */
    CpuDivisionKernel() = default;

    /** Configure kernel
     *
     * @param[in]  input1 First tensor input info. Data types supported: S32/F16/F32.
     * @param[in]  input2 Second tensor input info. Data types supported: Same as @p input1.
     * @param[out] output Output tensor info. Data types supported: Same as @p input1.
     */
    void configure(const ITensorInfo *input1, const ITensorInfo *input2, ITensorInfo *output);

    /** Static function to check if given info will lead to a valid configuration of @ref CpuDivisionKernel
     *
     * @param[in] input1 First tensor input info. Data types supported: S32/F16/F32.
     * @param[in] input2 Second tensor input info. Data types supported: Same as @p input1.
     * @param[in] output Output tensor info. Data types supported: Same as @p input1.
     *
     * @return a Status
     */
    static Status validate(const ITensorInfo *input1, const ITensorInfo *input2, const ITensorInfo *output);

protected:
    // Inherited methods overridden:
    static Status validate_arguments(const ITensorInfo &input1, const ITensorInfo &input2, const ITensorInfo &output);
};

class CpuPowerKernel : public CpuArithmeticKernel
{
public:
    /** Default constructor */
    CpuPowerKernel() = default;

    /** Configure kernel
     *
     * @param[in]  input1 First tensor input info. Data types supported: F16/F32.
     * @param[in]  input2 Second tensor input info. Data types supported: Same as @p input1.
     * @param[out] output Output tensor info. Data types supported: Same as @p input1.
     */
    void configure(const ITensorInfo *input1, const ITensorInfo *input2, ITensorInfo *output);

    /** Static function to check if given info will lead to a valid configuration of @ref CpuPowerKernel
     *
     * @param[in] input1 First tensor input info. Data types supported: F16/F32.
     * @param[in] input2 Second tensor input info. Data types supported: Same as @p input1.
     * @param[in] output Output tensor info. Data types supported: Same as @p input1.
     *
     * @return a Status
     */
    static Status validate(const ITensorInfo *input1, const ITensorInfo *input2, const ITensorInfo *output);

protected:
    // Inherited methods overridden:
    static Status validate_arguments(const ITensorInfo &input1, const ITensorInfo &input2, const ITensorInfo &output);
};

class CpuComparisonKernel : public CpuElementwiseKernel
{
public:
    /** Default constructor */
    CpuComparisonKernel() = default;

    /** Configure kernel
     *
     * @param[in]  op     Comparison operation to be executed.
     * @param[in]  input1 First tensor input info. Data types supported: QASYMM8/QASYMM8_SIGNED/S16/F16/S32/F32.
     * @param[in]  input2 Second tensor input info. Data types supported: Same as @p input1.
     * @param[out] output Output tensor info. Data types supported: U8.
     */
    void configure(ComparisonOperation op, const ITensorInfo *input1, const ITensorInfo *input2, ITensorInfo *output);

    /** Static function to check if given info will lead to a valid configuration of @ref cpu::kernels::CpuComparisonKernel
     *
     * @param[in] op     Comparison operation to be executed.
     * @param[in] input1 First tensor input info. Data types supported: QASYMM8/QASYMM8_SIGNED/S16/F16/S32/F32.
     * @param[in] input2 Second tensor input info. Data types supported: Same as @p input1.
     * @param[in] output Output tensor info. Data types supported: U8.
     *
     * @return a Status
     */
    static Status validate(ComparisonOperation op, const ITensorInfo *input1, const ITensorInfo *input2, const ITensorInfo *output);

protected:
    // Inherited methods overridden:
    static Status validate_arguments(const ITensorInfo &input1, const ITensorInfo &input2, const ITensorInfo &output);

private:
    /** Function to get the micro kernel implementation
     *
     * @param[in] input1 First input tensor information
     * @param[in] input2 Second input tensor information
     * @param[in] output Output tensor information
     *
     * @return the function instance for the micro kernel
     */
    std::function<ElementwiseFunction> get_implementation(const ITensorInfo *input1, const ITensorInfo *input2, ITensorInfo *output) override;

    ComparisonOperation _op{};
};
} // namespace kernels
} // namespace cpu
} // namespace arm_compute
#endif /* ARM_COMPUTE_CPU_ELEMENTWISE_KERNEL_H */
/*
 * Copyright (c) 2018-2020 Arm Limited.
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
#ifndef ARM_COMPUTE_TUNERS_H
#define ARM_COMPUTE_TUNERS_H

#include "arm_compute/runtime/CL/tuners/BifrostTuner.h"
#include "arm_compute/runtime/CL/tuners/MidgardTuner.h"

#include <memory>

namespace arm_compute
{
namespace tuners
{
/** Tuner factory class */
class TunerFactory final
{
public:
    static std::unique_ptr<ICLTuner> create_tuner(GPUTarget target)
    {
        GPUTarget arch = get_arch_from_target(target);
        switch(arch)
        {
            case GPUTarget::BIFROST:
                return std::make_unique<BifrostTuner>();
            case GPUTarget::MIDGARD:
                return std::make_unique<MidgardTuner>();
            default:
                return nullptr;
        }
    }
};
} // namespace tuners
} // namespace arm_compute
#endif /*ARM_COMPUTE_TUNERS_H */

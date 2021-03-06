/*
 * Copyright (c) 2017-2021 Arm Limited.
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
#ifndef My_print
#include "arm_compute/gl_vs.h"
#endif


#include "arm_compute/runtime/CL/functions/CLGEMM.h"

#include "arm_compute/core/CL/CLKernelLibrary.h"
#include "arm_compute/core/CL/ICLTensor.h"
#include "arm_compute/core/Error.h"
#include "arm_compute/core/GPUTarget.h"
#include "arm_compute/core/Helpers.h"
#include "arm_compute/core/KernelDescriptors.h"
#include "arm_compute/core/Log.h"
#include "arm_compute/core/TensorInfo.h"
#include "arm_compute/core/Types.h"
#include "arm_compute/core/Utils.h"
#include "arm_compute/core/Validate.h"
#include "arm_compute/core/utils/misc/ShapeCalculator.h"
#include "arm_compute/runtime/CL/CLScheduler.h"
#include "arm_compute/runtime/ITensorAllocator.h"
#include "src/core/CL/kernels/CLGEMMMatrixMultiplyKernel.h"
#include "src/core/CL/kernels/CLGEMMMatrixMultiplyReshapedKernel.h"
#include "src/core/CL/kernels/CLGEMMMatrixMultiplyReshapedOnlyRHSKernel.h"
#include "src/core/CL/kernels/CLGEMMReshapeLHSMatrixKernel.h"
#include "src/core/CL/kernels/CLGEMMReshapeRHSMatrixKernel.h"
#include "src/core/helpers/AutoConfiguration.h"
#include "src/core/utils/helpers/float_ops.h"
#include "src/runtime/CL/gemm/CLGEMMKernelSelection.h"
#include "src/runtime/CL/gemm_auto_heuristics/CLGEMMAutoHeuristics.h"
#include "support/Cast.h"
#include "utils/TypePrinter.h"

namespace arm_compute
{
using namespace arm_compute::misc::shape_calculator;
using namespace arm_compute::cl_gemm;
using namespace arm_compute::utils::cast;

namespace weights_transformations
{
CLGEMMReshapeRHSMatrixKernelManaged::CLGEMMReshapeRHSMatrixKernelManaged()
    : _kernel(std::make_unique<CLGEMMReshapeRHSMatrixKernel>())
{
}

CLGEMMReshapeRHSMatrixKernelManaged::~CLGEMMReshapeRHSMatrixKernelManaged() = default;

void CLGEMMReshapeRHSMatrixKernelManaged::run()
{
    _output.allocator()->allocate();
    CLScheduler::get().enqueue(*_kernel, false);
    _reshape_run = true;
}

void CLGEMMReshapeRHSMatrixKernelManaged::release()
{
    _output.allocator()->free();
}

ICLTensor *CLGEMMReshapeRHSMatrixKernelManaged::get_weights()
{
    return &_output;
}

uint32_t CLGEMMReshapeRHSMatrixKernelManaged::uid()
{
    return _uid;
}

void CLGEMMReshapeRHSMatrixKernelManaged::configure(const ICLTensor *input, GEMMRHSMatrixInfo info)
{
    configure(CLKernelLibrary::get().get_compile_context(), input, info);
}

void CLGEMMReshapeRHSMatrixKernelManaged::configure(const CLCompileContext &compile_context, const ICLTensor *input, GEMMRHSMatrixInfo info)
{
    _kernel->configure(compile_context, input, &_output, info);
}
} // namespace weights_transformations

namespace
{
inline bool validate_gemm_kernel(CLGEMMKernelType kernel_type)
{
    switch(kernel_type)
    {
        case CLGEMMKernelType::NATIVE_V1:
        case CLGEMMKernelType::RESHAPED_ONLY_RHS:
        case CLGEMMKernelType::RESHAPED_V1:
        case CLGEMMKernelType::RESHAPED:
        {
            return true;
        }
        default:
        {
            return false;
        }
    }
}
//Automatically select between mlgo (prioritized) and default heuristics for gemm kernel type
inline CLGEMMKernelType auto_select_gemm_kernel(auto_heuristics::CommonQuery query, bool reshape_b_only_on_first_run)
{
    auto gemm_kernel = auto_heuristics::select_mlgo_gemm_kernel(query, reshape_b_only_on_first_run);
    //Ehsan gemm_kernel is 0 There is not mlgo_heuristics in CLScheduler::get().gemm_heuristics()
    //if(!bool(gemm_kernel))
    //    std::cout<<"\nThere is not mlgo_heuristics in CLScheduler::get().gemm_heuristics()\n";
    if(bool(gemm_kernel))
    {
        if(validate_gemm_kernel(gemm_kernel.gemm_type))
        {
            ARM_COMPUTE_LOG_INFO_MSG_WITH_FORMAT_CORE("Use gemm kernel from mlgo heuristics: %s.", to_string(gemm_kernel.gemm_type).c_str());
            return gemm_kernel.gemm_type;
        }
    }
    //Ehsan: Select kernel based on gpu and dimensions

    gemm_kernel = auto_heuristics::select_default_gemm_kernel(query, reshape_b_only_on_first_run);
#if My_print > 0
    printf("Use gemm kernel from default heuristics: %s.\n", to_string(gemm_kernel.gemm_type).c_str());
#endif
    ARM_COMPUTE_LOG_INFO_MSG_WITH_FORMAT_CORE("Use gemm kernel from default heuristics: %s.", to_string(gemm_kernel.gemm_type).c_str());
    return gemm_kernel.gemm_type;
}
// Validate lhs_info and rhs_info for reshaped only rhs kernel
inline bool validate_lhs_rhs_info_reshaped_only_rhs(const GEMMLHSMatrixInfo &lhs_info, const GEMMRHSMatrixInfo &rhs_info, const ITensorInfo *a, const ITensorInfo *b, const ITensorInfo *c,
                                                    const ITensorInfo *output, GEMMKernelInfo gemm_kernel_info)
{
    // Validate GEMMLHSMatrixInfo and GEMMRHSMatrixInfo for reshaped only rhs kernel
    TensorInfo tmp_b_info{};
    // Validate reshape RHS kernel
    auto_init_if_empty(tmp_b_info, b->clone()->set_tensor_shape(compute_rhs_reshaped_shape(*b, rhs_info)));
    if(!bool(CLGEMMReshapeRHSMatrixKernel::validate(b, &tmp_b_info, rhs_info)))
    {
        return false;
    }
    // Validate mm kernel
    gemm_kernel_info.lhs_info  = lhs_info;
    gemm_kernel_info.rhs_info  = rhs_info;
    gemm_kernel_info.has_pad_y = false;
    if(!bool(CLGEMMMatrixMultiplyReshapedOnlyRHSKernel::validate(a, &tmp_b_info, c, output, 1.f, 0.f, lhs_info, rhs_info, gemm_kernel_info)))
    {
        return false;
    }
    gemm_kernel_info.has_pad_y = true;
    if(!bool(CLGEMMMatrixMultiplyReshapedOnlyRHSKernel::validate(a, &tmp_b_info, c, output, 1.f, 0.f, lhs_info, rhs_info, gemm_kernel_info)))
    {
        return false;
    }
    return true;
}

//Automatically select between mlgo (prioritized) and default heuristics for reshaped only rhs kernel configs
inline std::pair<GEMMLHSMatrixInfo, GEMMRHSMatrixInfo> auto_select_gemm_config_reshaped_only_rhs(auto_heuristics::CommonQuery query, GEMMKernelInfo kernel_info, const ITensorInfo *a,
                                                                                                 const ITensorInfo *b,
                                                                                                 const ITensorInfo *c, const ITensorInfo *output)
{
    auto config = auto_heuristics::select_mlgo_gemm_config_reshaped_only_rhs(query);
    if(config)
    {
        if(validate_lhs_rhs_info_reshaped_only_rhs(config.lhs_info, config.rhs_info, a, b, c, output, kernel_info))
        {
            ARM_COMPUTE_LOG_INFO_MSG_WITH_FORMAT_CORE("Use reshaped_only_rhs config from mlgo heuristics: LHS info: %s ; RHS info: %s ", to_string(config.lhs_info).c_str(), to_string(config.rhs_info).c_str());
            return { config.lhs_info, config.rhs_info };
        }
    }

    config = auto_heuristics::select_default_gemm_config_reshaped_only_rhs(query);
#if My_print > 0
    printf("Auto_select_gemm_config_reshaped_only_rhs, there is not mlgo_heuristics in CLScheduler::get().gemm_heuristics()\n");
    printf("Use reshaped_only_rhs config from default heuristics: LHS info: %s ; RHS info: %s ", to_string(config.lhs_info).c_str(), to_string(config.rhs_info).c_str());
#endif
    ARM_COMPUTE_LOG_INFO_MSG_WITH_FORMAT_CORE("Use reshaped_only_rhs config from default heuristics: LHS info: %s ; RHS info: %s ", to_string(config.lhs_info).c_str(), to_string(config.rhs_info).c_str());
    return { config.lhs_info, config.rhs_info };
}

// Validate lhs_info and rhs_info for reshaped kernel
inline bool validate_lhs_rhs_info_reshaped(const GEMMLHSMatrixInfo &lhs_info, const GEMMRHSMatrixInfo &rhs_info, const ITensorInfo *a, const ITensorInfo *b, const ITensorInfo *c,
                                           const ITensorInfo *output, GEMMKernelInfo gemm_kernel_info, bool reinterpret_input_as_3d)
{
    // Validate GEMMLHSMatrixInfo and GEMMRHSMatrixInfo for reshaped kernel
    TensorInfo tmp_a_info{};
    TensorInfo tmp_b_info{};

    // Validate reshape LHS kernel
    auto_init_if_empty(tmp_a_info, a->clone()->set_tensor_shape(compute_lhs_reshaped_shape(*a, lhs_info, reinterpret_input_as_3d)));
    if(!bool(CLGEMMReshapeLHSMatrixKernel::validate(a, &tmp_a_info, lhs_info, reinterpret_input_as_3d)))
    {
        return false;
    }

    // Validate reshape RHS kernel
    auto_init_if_empty(tmp_b_info, b->clone()->set_tensor_shape(compute_rhs_reshaped_shape(*b, rhs_info)));
    if(!bool(CLGEMMReshapeRHSMatrixKernel::validate(b, &tmp_b_info, rhs_info)))
    {
        return false;
    }
    // Validate mm kernel
    gemm_kernel_info.lhs_info = lhs_info;
    gemm_kernel_info.rhs_info = rhs_info;
    if(!bool(CLGEMMMatrixMultiplyReshapedKernel::validate(&tmp_a_info, &tmp_b_info, c, output, 1.f, 0.f, lhs_info, rhs_info, gemm_kernel_info)))
    {
        return false;
    }
    return true;
}

//Automatically select between mlgo (prioritized) and default heuristics for reshaped kernel configs
inline std::pair<GEMMLHSMatrixInfo, GEMMRHSMatrixInfo> auto_select_gemm_config_reshaped(auto_heuristics::CommonQuery query, GEMMKernelInfo kernel_info, const ITensorInfo *a, const ITensorInfo *b,
                                                                                        const ITensorInfo *c, const ITensorInfo *output, bool reinterpret_input_as_3d)
{
    auto config = auto_heuristics::select_mlgo_gemm_config_reshaped(query);
    if(config)
    {
        if(validate_lhs_rhs_info_reshaped(config.lhs_info, config.rhs_info, a, b, c, output, kernel_info, reinterpret_input_as_3d))
        {
            ARM_COMPUTE_LOG_INFO_MSG_WITH_FORMAT_CORE("Use reshaped config from mlgo heuristics: LHS info: %s ; RHS info: %s ", to_string(config.lhs_info).c_str(), to_string(config.rhs_info).c_str());
            return { config.lhs_info, config.rhs_info };
        }
    }
    config = auto_heuristics::select_default_gemm_config_reshaped(query);
    ARM_COMPUTE_LOG_INFO_MSG_WITH_FORMAT_CORE("Use reshaped config from default heuristics: LHS info: %s ; RHS info: %s ", to_string(config.lhs_info).c_str(), to_string(config.rhs_info).c_str());
    return { config.lhs_info, config.rhs_info };
}

} // namespace

CLGEMM::CLGEMM(std::shared_ptr<IMemoryManager> memory_manager, IWeightsManager *weights_manager)
    : _memory_group(std::move(memory_manager)),
      _weights_manager(weights_manager),
      _mm_kernel(std::make_unique<CLGEMMMatrixMultiplyKernel>()),
      _reshape_lhs_kernel(std::make_unique<CLGEMMReshapeLHSMatrixKernel>()),
      _reshape_rhs_kernel(std::make_unique<CLGEMMReshapeRHSMatrixKernel>()),
      _reshape_rhs_kernel_managed(std::make_unique<weights_transformations::CLGEMMReshapeRHSMatrixKernelManaged>()),
      _mm_reshaped_kernel(std::make_unique<CLGEMMMatrixMultiplyReshapedKernel>()),
      _mm_reshaped_only_rhs_kernel(std::make_unique<CLGEMMMatrixMultiplyReshapedOnlyRHSKernel>()),
      _mm_reshaped_only_rhs_fallback_kernel(std::make_unique<CLGEMMMatrixMultiplyReshapedOnlyRHSKernel>()),
      _tmp_a(),
      _tmp_b(),
      _original_b(nullptr),
      _lhs(nullptr),
      _dst(nullptr),
      _reshape_b_only_on_first_run(false),
      _is_prepared(false),
      _gemm_kernel_type(CLGEMMKernelType::NATIVE_V1)
{
}

CLGEMM::~CLGEMM() = default;

void CLGEMM::configure_native_v1(const CLCompileContext &compile_context, const ICLTensor *a, const ICLTensor *b, const ICLTensor *c, ICLTensor *output, float alpha, float beta,
                                 const GEMMInfo &gemm_info)
{
    const unsigned int m          = gemm_info.reinterpret_input_as_3d() ? (a->info()->dimension(1) * a->info()->dimension(2)) : a->info()->dimension(1);
    const unsigned int n          = b->info()->dimension(0);
    const unsigned int k          = a->info()->dimension(0);
    const GPUTarget    gpu_target = CLScheduler::get().target();

    // Set the target for the kernels
    _mm_kernel->set_target(gpu_target);

    GEMMReshapeInfo reshape_info(m, n, k, 1, 1, gemm_info.depth_output_gemm3d(), gemm_info.reinterpret_input_as_3d(), gemm_info.broadcast_bias());

    // Configure and tune matrix multiply kernel
    _mm_kernel->configure(compile_context, a, b, c, output, alpha, beta, false, reshape_info, gemm_info.fp_mixed_precision(), gemm_info.activation_info());

    // Tune kernel statically
    CLScheduler::get().tune_kernel_static(*_mm_kernel);
}

void CLGEMM::configure_reshaped_v1(const CLCompileContext &compile_context, const ICLTensor *a, const ICLTensor *b, const ICLTensor *c, ICLTensor *output, float alpha, float beta,
                                   const GEMMInfo &gemm_info)
{
    bool               reinterpret_input_as_3d   = gemm_info.reinterpret_input_as_3d();
    const unsigned int m                         = reinterpret_input_as_3d ? (a->info()->dimension(1) * a->info()->dimension(2)) : a->info()->dimension(1);
    const unsigned int n                         = b->info()->dimension(0);
    const unsigned int k                         = a->info()->dimension(0);
    const int          depth_output_gemm3d       = gemm_info.depth_output_gemm3d();
    const GPUTarget    gpu_target                = CLScheduler::get().target();
    int                mult_transpose1xW_width   = 1;
    int                mult_interleave4x4_height = 1;

    // Set the target for the kernels
    _reshape_lhs_kernel->set_target(gpu_target);
    _mm_kernel->set_target(gpu_target);

    if(get_arch_from_target(gpu_target) == GPUTarget::BIFROST)
    {
        mult_transpose1xW_width   = 4;
        mult_interleave4x4_height = 2;
    }

    GEMMRHSMatrixInfo rhs_info;
    rhs_info.n0         = 16 / b->info()->element_size();
    rhs_info.k0         = 1;
    rhs_info.h0         = mult_transpose1xW_width;
    rhs_info.interleave = false;
    rhs_info.transpose  = false;

    GEMMLHSMatrixInfo lhs_info;
    lhs_info.m0         = 4;
    lhs_info.k0         = 4;
    lhs_info.v0         = mult_interleave4x4_height;
    lhs_info.interleave = true;
    lhs_info.transpose  = true;

    GEMMReshapeInfo reshape_info(m, n, k, mult_transpose1xW_width, mult_interleave4x4_height, depth_output_gemm3d, false, gemm_info.broadcast_bias());

    const bool use_mm_b = (!_weights_manager || !_weights_manager->are_weights_managed(b));

    // Manage intermediate buffers
    _memory_group.manage(&_tmp_a);

    if(!_reshape_b_only_on_first_run && use_mm_b)
    {
        _memory_group.manage(&_tmp_b);
    }

    // Configure interleave kernel
    _reshape_lhs_kernel->configure(compile_context, a, &_tmp_a, lhs_info, reinterpret_input_as_3d);

    // Configure transpose kernel
    ICLTensor *reshaped_rhs = &_tmp_b;
    if(_weights_manager && _weights_manager->are_weights_managed(b))
    {
        _reshape_rhs_kernel_managed->configure(compile_context, b, rhs_info);
        reshaped_rhs = utils::cast::polymorphic_downcast<ICLTensor *>(_weights_manager->acquire(b, _reshape_rhs_kernel_managed.get()));
    }
    else
    {
        _reshape_rhs_kernel->configure(compile_context, b, &_tmp_b, rhs_info);
    }

    // Configure and tune matrix multiply kernel
    _mm_kernel->configure(compile_context, &_tmp_a, reshaped_rhs, c, output, alpha, beta, true, reshape_info, gemm_info.fp_mixed_precision(), gemm_info.activation_info());

    CLScheduler::get().tune_kernel_static(*_mm_kernel);

    // Allocate intermediate tensors
    _tmp_a.allocator()->allocate();

    if(!_reshape_b_only_on_first_run && use_mm_b)
    {
        _tmp_b.allocator()->allocate();
    }
}

void CLGEMM::configure_reshaped_v2(const CLCompileContext &compile_context, const ICLTensor *a, const ICLTensor *b, const ICLTensor *c, ICLTensor *output, float alpha, float beta,
                                   const GEMMInfo &gemm_info)
{
    DataType           data_type               = a->info()->data_type();
    bool               reinterpret_input_as_3d = gemm_info.reinterpret_input_as_3d();
    const unsigned int m                       = reinterpret_input_as_3d ? (a->info()->dimension(1) * a->info()->dimension(2)) : a->info()->dimension(1);
    const unsigned int n                       = b->info()->dimension(0);
    const unsigned int k                       = a->info()->dimension(0);
    const unsigned int batch_size              = reinterpret_input_as_3d ? a->info()->dimension(3) : a->info()->dimension(2);
    const int          depth_output_gemm3d     = gemm_info.depth_output_gemm3d();
    const GPUTarget    gpu_target              = CLScheduler::get().target();
    bool               broadcast_bias          = gemm_info.broadcast_bias();

    GEMMKernelInfo kernel_info;
    kernel_info.m                       = m;
    kernel_info.n                       = n;
    kernel_info.k                       = k;
    kernel_info.depth_output_gemm3d     = depth_output_gemm3d;
    kernel_info.reinterpret_input_as_3d = false;
    kernel_info.broadcast_bias          = broadcast_bias;
    kernel_info.activation_info         = gemm_info.activation_info();

    // Set the target for the kernels
    _reshape_lhs_kernel->set_target(gpu_target);
    _mm_kernel->set_target(gpu_target);

    const bool use_mm_b = (!_weights_manager || !_weights_manager->are_weights_managed(b));

    // Manage intermediate buffers
    _memory_group.manage(&_tmp_a);

    if(!_reshape_b_only_on_first_run && use_mm_b)
    {
        _memory_group.manage(&_tmp_b);
    }

    // _tmp_a and _tmp_b will be auto configured in _interleave_kernel and in _transpose_kernel

    GEMMLHSMatrixInfo lhs_info{};
    GEMMRHSMatrixInfo rhs_info{};

    // Pick up the GEMM configuration
    std::tie(lhs_info, rhs_info) = auto_select_gemm_config_reshaped(auto_heuristics::CommonQuery{ gpu_target, data_type, m, n, k, batch_size }, kernel_info, a->info(), b->info(),
                                                                    c == nullptr ? nullptr : c->info(), output->info(), gemm_info.reinterpret_input_as_3d());

    _reshape_lhs_kernel->configure(compile_context, a, &_tmp_a, lhs_info, gemm_info.reinterpret_input_as_3d());

    ICLTensor *reshaped_rhs = &_tmp_b;
    if(_weights_manager && _weights_manager->are_weights_managed(b))
    {
        _reshape_rhs_kernel_managed->configure(compile_context, b, rhs_info);
        reshaped_rhs = utils::cast::polymorphic_downcast<ICLTensor *>(_weights_manager->acquire(b, _reshape_rhs_kernel_managed.get()));
    }
    else
    {
        _reshape_rhs_kernel->configure(compile_context, b, &_tmp_b, rhs_info);
    }

    // Configure and tune matrix multiply kernel
    _mm_reshaped_kernel->configure(compile_context, &_tmp_a, reshaped_rhs, c, output, alpha, beta, lhs_info, rhs_info, kernel_info);

    // Allocate intermediate tensors
    _tmp_a.allocator()->allocate();

    if(!_reshape_b_only_on_first_run && use_mm_b)
    {
        _tmp_b.allocator()->allocate();
    }
}

void CLGEMM::configure_reshaped_only_rhs(const CLCompileContext &compile_context, const ICLTensor *a, const ICLTensor *b, const ICLTensor *c, ICLTensor *output, float alpha, float beta,
                                         const GEMMInfo &gemm_info)
{
    DataType           data_type               = a->info()->data_type();
    bool               reinterpret_input_as_3d = gemm_info.reinterpret_input_as_3d();
    const unsigned int m                       = reinterpret_input_as_3d ? (a->info()->dimension(1) * a->info()->dimension(2)) : a->info()->dimension(1);
    const unsigned int n                       = b->info()->dimension(0);
    const unsigned int k                       = a->info()->dimension(0);
    const unsigned int batch_size              = reinterpret_input_as_3d ? a->info()->dimension(3) : a->info()->dimension(2);
    const int          depth_output_gemm3d     = gemm_info.depth_output_gemm3d();
    const GPUTarget    gpu_target              = CLScheduler::get().target();
    bool               broadcast_bias          = gemm_info.broadcast_bias();

    GEMMKernelInfo kernel_info;
    kernel_info.m                       = m;
    kernel_info.n                       = n;
    kernel_info.k                       = k;
    kernel_info.depth_output_gemm3d     = depth_output_gemm3d;
    kernel_info.reinterpret_input_as_3d = reinterpret_input_as_3d;
    kernel_info.broadcast_bias          = broadcast_bias;
    kernel_info.activation_info         = gemm_info.activation_info();

    // Set the target for the kernels
    _mm_kernel->set_target(gpu_target);

    const bool use_mm_b = (!_weights_manager || !_weights_manager->are_weights_managed(b));

    // Manage intermediate buffers
    if(!_reshape_b_only_on_first_run && use_mm_b)
    {
        _memory_group.manage(&_tmp_b);
    }

    GEMMLHSMatrixInfo lhs_info{};
    GEMMRHSMatrixInfo rhs_info{};

    // Pick up the GEMM configuration
    std::tie(lhs_info, rhs_info) = auto_select_gemm_config_reshaped_only_rhs(auto_heuristics::CommonQuery{ gpu_target, data_type, m, n, k, batch_size }, kernel_info, a->info(), b->info(),
                                                                             c == nullptr ? nullptr : c->info(), output->info());

    //Ehsan

    bool w=false;
    bool w2=false;
    if(_weights_manager){
    	w=true;
    	if( _weights_manager->are_weights_managed(b) )
        	w2=true;
    }
#if My_print > 0
    printf("\nCLGEMM::configure_reshaped_only_rhs, weight manager:{%d}, and wm are weights managed:{%d}\n_reshape_b_only_on_first_run:%d\n",w,w2,int(_reshape_b_only_on_first_run) );
    std::cout<<"CLGEMM, _tmp_b or reshaped rhs shape: "<<_tmp_b.info()->tensor_shape()<<" output shape:"<<output->info()->tensor_shape()<<std::endl;
    //Ehsan: Fully connected layers has weight manager and b is managed but for conv there is not _weight_manager
#endif
    ICLTensor *reshaped_rhs = &_tmp_b;
    if(_weights_manager && _weights_manager->are_weights_managed(b))
    {
        _reshape_rhs_kernel_managed->configure(compile_context, b, rhs_info);
        reshaped_rhs = utils::cast::polymorphic_downcast<ICLTensor *>(_weights_manager->acquire(b, _reshape_rhs_kernel_managed.get()));
    }
    else
    {
        _reshape_rhs_kernel->configure(compile_context, b, &_tmp_b, rhs_info);
    }

    //Ehsan
#if My_print > 0
    std::cout<<"CLGEMM, After configuring _reshape_rhs_kernel, _tmp_b or reshaped rhs shape: "<<_tmp_b.info()->tensor_shape()<<" output shape:"<<output->info()->tensor_shape()<<std::endl;
#endif
    // Configure two variants of CLGEMMMatrixMultiplyReshapedOnlyRHSKernel (has_pad_y = false/true)
    // During the prepare stage we check the padding requirement for the lhs and dst tensors. If they do not have
    // pad y, we dispatch CLGEMMMatrixMultiplyReshapedOnlyRHSKernel with has_pad_y = false

    // Configure matrix multiply kernel with no y padding support
    kernel_info.has_pad_y = false;
    _mm_reshaped_only_rhs_kernel->configure(compile_context, a, reshaped_rhs, c, output, alpha, beta, lhs_info, rhs_info, kernel_info);

    // Configure matrix multiply kernel with y padding support
    kernel_info.has_pad_y = true;
    _mm_reshaped_only_rhs_fallback_kernel->configure(compile_context, a, reshaped_rhs, c, output, alpha, beta, lhs_info, rhs_info, kernel_info);

    if(!_reshape_b_only_on_first_run && use_mm_b)
    {
        _tmp_b.allocator()->allocate();
    }
}

Status CLGEMM::validate_native_v1(const ITensorInfo *a, const ITensorInfo *b, const ITensorInfo *c, const ITensorInfo *output, float alpha, float beta, const GEMMInfo &gemm_info)
{
    ARM_COMPUTE_UNUSED(alpha);
    ARM_COMPUTE_UNUSED(output);

    // Get the GPU target
    const GPUTarget    gpu_target              = CLScheduler::get().target();
    bool               reinterpret_input_as_3d = gemm_info.reinterpret_input_as_3d();
    const unsigned int m                       = reinterpret_input_as_3d ? (a->dimension(1) * a->dimension(2)) : a->dimension(1);
    const unsigned int n                       = b->dimension(0);
    const unsigned int k                       = a->dimension(0);
    const int          depth_output_gemm3d     = gemm_info.depth_output_gemm3d();

    const GEMMReshapeInfo reshape_info = GEMMReshapeInfo(m, n, k, 1, 1, depth_output_gemm3d, reinterpret_input_as_3d, gemm_info.broadcast_bias());

    // Validate matrix multiply
    ARM_COMPUTE_RETURN_ON_ERROR(CLGEMMMatrixMultiplyKernel::validate(a, b, c, output, alpha, beta,
                                                                     false, reshape_info, gpu_target, gemm_info.fp_mixed_precision(), gemm_info.activation_info()));

    return Status{};
}

Status CLGEMM::validate_reshaped_v1(const ITensorInfo *a, const ITensorInfo *b, const ITensorInfo *c, const ITensorInfo *output, float alpha, float beta, const GEMMInfo &gemm_info)
{
    ARM_COMPUTE_UNUSED(alpha);
    ARM_COMPUTE_UNUSED(output);

    TensorInfo tmp_a_info{};
    TensorInfo tmp_b_info{};

    // Get the GPU target
    const GPUTarget    gpu_target                = CLScheduler::get().target();
    const unsigned int m                         = gemm_info.reinterpret_input_as_3d() ? (a->dimension(1) * a->dimension(2)) : a->dimension(1);
    const unsigned int n                         = b->dimension(0);
    const unsigned int k                         = a->dimension(0);
    int                mult_transpose1xW_width   = 1;
    int                mult_interleave4x4_height = 1;
    const int          depth_output_gemm3d       = gemm_info.depth_output_gemm3d();

    if(get_arch_from_target(gpu_target) == GPUTarget::BIFROST)
    {
        mult_transpose1xW_width   = 4;
        mult_interleave4x4_height = 2;
    }

    GEMMRHSMatrixInfo rhs_info;
    rhs_info.n0         = 16 / b->element_size();
    rhs_info.k0         = 1;
    rhs_info.h0         = mult_transpose1xW_width;
    rhs_info.interleave = false;
    rhs_info.transpose  = false;

    GEMMLHSMatrixInfo lhs_info;
    lhs_info.m0         = 4;
    lhs_info.k0         = 4;
    lhs_info.v0         = mult_interleave4x4_height;
    lhs_info.interleave = true;
    lhs_info.transpose  = true;

    const GEMMReshapeInfo reshape_info = GEMMReshapeInfo(m, n, k, mult_transpose1xW_width, mult_interleave4x4_height, depth_output_gemm3d, false, gemm_info.broadcast_bias());

    // Validate interleave kernel
    auto_init_if_empty(tmp_a_info, a->clone()->set_tensor_shape(compute_lhs_reshaped_shape(*a, lhs_info, gemm_info.reinterpret_input_as_3d())));
    ARM_COMPUTE_RETURN_ON_ERROR(CLGEMMReshapeLHSMatrixKernel::validate(a, &tmp_a_info, lhs_info, gemm_info.reinterpret_input_as_3d()));

    // Validate transpose kernel
    auto_init_if_empty(tmp_b_info, b->clone()->set_tensor_shape(compute_rhs_reshaped_shape(*b, rhs_info)));
    ARM_COMPUTE_RETURN_ON_ERROR(CLGEMMReshapeRHSMatrixKernel::validate(b, &tmp_b_info, rhs_info));

    // Validate matrix multiply
    ARM_COMPUTE_RETURN_ON_ERROR(CLGEMMMatrixMultiplyKernel::validate(&tmp_a_info, &tmp_b_info, c, output, alpha, beta,
                                                                     true, reshape_info, gpu_target, gemm_info.fp_mixed_precision(), gemm_info.activation_info()));

    return Status{};
}

Status CLGEMM::validate_reshaped(const ITensorInfo *a, const ITensorInfo *b, const ITensorInfo *c, const ITensorInfo *output, float alpha, float beta, const GEMMInfo &gemm_info)
{
    ARM_COMPUTE_UNUSED(alpha);
    ARM_COMPUTE_UNUSED(output);

    TensorInfo tmp_a_info{};
    TensorInfo tmp_b_info{};

    // Get the GPU target
    const GPUTarget    gpu_target              = CLScheduler::get().target();
    DataType           data_type               = a->data_type();
    bool               reinterpret_input_as_3d = gemm_info.reinterpret_input_as_3d();
    const unsigned int m                       = reinterpret_input_as_3d ? (a->dimension(1) * a->dimension(2)) : a->dimension(1);
    const unsigned int n                       = b->dimension(0);
    const unsigned int k                       = a->dimension(0);
    const unsigned int batch_size              = reinterpret_input_as_3d ? a->dimension(3) : a->dimension(2);
    const int          depth_output_gemm3d     = gemm_info.depth_output_gemm3d();
    const bool         broadcast_bias          = gemm_info.broadcast_bias();

    GEMMKernelInfo kernel_info;
    kernel_info.m                       = m;
    kernel_info.n                       = n;
    kernel_info.k                       = k;
    kernel_info.depth_output_gemm3d     = depth_output_gemm3d;
    kernel_info.reinterpret_input_as_3d = false;
    kernel_info.broadcast_bias          = broadcast_bias;
    kernel_info.activation_info         = gemm_info.activation_info();

    GEMMLHSMatrixInfo lhs_info;
    GEMMRHSMatrixInfo rhs_info;

    // Pick up the GEMM configuration
    // NOTE: No need to validate mlgo configurations as they automatically fall back to default heuristics if validation fails
    const auto gemm_config = select_default_gemm_config_reshaped(auto_heuristics::CommonQuery{ gpu_target, data_type, m, n, k, batch_size });
    lhs_info               = gemm_config.lhs_info;
    rhs_info               = gemm_config.rhs_info;

    auto_init_if_empty(tmp_a_info, a->clone()->set_tensor_shape(compute_lhs_reshaped_shape(*a, lhs_info, gemm_info.reinterpret_input_as_3d())));
    ARM_COMPUTE_RETURN_ON_ERROR(CLGEMMReshapeLHSMatrixKernel::validate(a, &tmp_a_info, lhs_info, gemm_info.reinterpret_input_as_3d()));

    auto_init_if_empty(tmp_b_info, b->clone()->set_tensor_shape(compute_rhs_reshaped_shape(*b, rhs_info)));
    ARM_COMPUTE_RETURN_ON_ERROR(CLGEMMReshapeRHSMatrixKernel::validate(b, &tmp_b_info, rhs_info));

    // Validate matrix multiply
    ARM_COMPUTE_RETURN_ON_ERROR(CLGEMMMatrixMultiplyReshapedKernel::validate(&tmp_a_info, &tmp_b_info, c, output, alpha, beta, lhs_info, rhs_info, kernel_info));

    return Status{};
}

Status CLGEMM::validate_reshaped_only_rhs(const ITensorInfo *a, const ITensorInfo *b, const ITensorInfo *c, const ITensorInfo *output, float alpha, float beta, const GEMMInfo &gemm_info)
{
    ARM_COMPUTE_UNUSED(alpha);
    ARM_COMPUTE_UNUSED(output);

    TensorInfo tmp_b_info{};

    // Get the GPU target
    const GPUTarget    gpu_target              = CLScheduler::get().target();
    const DataType     data_type               = a->data_type();
    bool               reinterpret_input_as_3d = gemm_info.reinterpret_input_as_3d();
    const unsigned int m                       = reinterpret_input_as_3d ? (a->dimension(1) * a->dimension(2)) : a->dimension(1);
    const unsigned int n                       = b->dimension(0);
    const unsigned int k                       = a->dimension(0);
    const unsigned int batch_size              = reinterpret_input_as_3d ? a->dimension(3) : a->dimension(2);
    const int          depth_output_gemm3d     = gemm_info.depth_output_gemm3d();
    const bool         broadcast_bias          = gemm_info.broadcast_bias();

    GEMMKernelInfo kernel_info;
    kernel_info.m                       = m;
    kernel_info.n                       = n;
    kernel_info.k                       = k;
    kernel_info.depth_output_gemm3d     = depth_output_gemm3d;
    kernel_info.reinterpret_input_as_3d = reinterpret_input_as_3d;
    kernel_info.broadcast_bias          = broadcast_bias;
    kernel_info.activation_info         = gemm_info.activation_info();

    GEMMLHSMatrixInfo lhs_info;
    GEMMRHSMatrixInfo rhs_info;

    // Pick up the GEMM configuration
    // NOTE: No need to validate mlgo configurations as they automatically fall back to default heuristics if validation fails
    const auto gemm_config = select_default_gemm_config_reshaped_only_rhs(auto_heuristics::CommonQuery{ gpu_target, data_type, m, n, k, batch_size });
    lhs_info               = gemm_config.lhs_info;
    rhs_info               = gemm_config.rhs_info;

    auto_init_if_empty(tmp_b_info, b->clone()->set_tensor_shape(compute_rhs_reshaped_shape(*b, rhs_info)));
    ARM_COMPUTE_RETURN_ON_ERROR(CLGEMMReshapeRHSMatrixKernel::validate(b, &tmp_b_info, rhs_info));

    // Validate matrix multiply
    kernel_info.has_pad_y = false;
    ARM_COMPUTE_RETURN_ON_ERROR(CLGEMMMatrixMultiplyReshapedOnlyRHSKernel::validate(a, &tmp_b_info, c, output, alpha, beta, lhs_info, rhs_info, kernel_info));

    kernel_info.has_pad_y = true;
    ARM_COMPUTE_RETURN_ON_ERROR(CLGEMMMatrixMultiplyReshapedOnlyRHSKernel::validate(a, &tmp_b_info, c, output, alpha, beta, lhs_info, rhs_info, kernel_info));

    return Status{};
}

void CLGEMM::configure(const ICLTensor *a, const ICLTensor *b, const ICLTensor *c, ICLTensor *output, float alpha, float beta, const GEMMInfo &gemm_info)
{
    configure(CLKernelLibrary::get().get_compile_context(), a, b, c, output, alpha, beta, gemm_info);
}

void CLGEMM::configure(const CLCompileContext &compile_context, const ICLTensor *a, const ICLTensor *b, const ICLTensor *c, ICLTensor *output, float alpha, float beta, const GEMMInfo &gemm_info)
{
    ARM_COMPUTE_ERROR_ON_NULLPTR(a, b, output);

    // Perform validation step
    ARM_COMPUTE_ERROR_THROW_ON(validate(a->info(), b->info(), c != nullptr ? c->info() : nullptr, output->info(), alpha, beta, gemm_info));

    // Check if we need to reshape the matrix B only on the first run
    _reshape_b_only_on_first_run = gemm_info.reshape_b_only_on_first_run();//Ehsan true
    _is_prepared                 = gemm_info.retain_internal_weights();//Ehsan false
    //Ehsan retaining weights=0
    //std::cout<<"^^^^^^^^^^^^^^^Retaining weights: "<<_is_prepared<<std::endl;
    _original_b                  = b;
    _lhs                         = a;
    _dst                         = output;

    //(_lhs or a) input with shape  k * m
    //(_original_b or b) weight with sahpe n * k
    //m=(out[0]*out[1])
    //n=num_kernels
    //k=(w[h]*w[w]*channels/num_groups)

    bool               reinterpret_input_as_3d = gemm_info.reinterpret_input_as_3d();
    const unsigned int m                       = reinterpret_input_as_3d ? (a->info()->dimension(1) * a->info()->dimension(2)) : a->info()->dimension(1);
    const unsigned int n                       = b->info()->dimension(0);
    const unsigned int k                       = a->info()->dimension(0);
    const unsigned int batch_size              = reinterpret_input_as_3d ? a->info()->dimension(3) : a->info()->dimension(2);

#if My_print > 0
    //Ehsan
    std::cout<<"\nInput shape: "<<a->info()->tensor_shape()
    		<<" Weights shape: "<<b->info()->tensor_shape()
    		<<" m: "<<m
    		<<" n: "<<n
			<<" k: "<<k
			<<" Batch size: "<<batch_size
			<<std::endl;
#endif
    // Select GEMMType
    _gemm_kernel_type = auto_select_gemm_kernel(auto_heuristics::CommonQuery{ CLScheduler::get().target(), a->info()->data_type(), m, n, k, batch_size }, _reshape_b_only_on_first_run);

    const bool fuse_add_c = (!(helpers::float_ops::is_zero(beta)) && c != nullptr);
    //Ehsan true
    //if(fuse_add_c)
    	//printf("fuse add c is true\n");
    const ICLTensor *c_to_use = fuse_add_c ? c : nullptr;
    //std::string tt;
    //std::cin>>tt;

    switch(_gemm_kernel_type)
    {
        case CLGEMMKernelType::NATIVE_V1:
        {
            configure_native_v1(compile_context, a, b, c_to_use, output, alpha, beta, gemm_info);
            break;
        }
        case CLGEMMKernelType::RESHAPED_V1:
        {
            configure_reshaped_v1(compile_context, a, b, c_to_use, output, alpha, beta, gemm_info);
            break;
        }
        case CLGEMMKernelType::RESHAPED:
        {
        	//Ehsan
        	//printf("&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&\n\n");
            configure_reshaped_v2(compile_context, a, b, c_to_use, output, alpha, beta, gemm_info);
            break;
        }
        case CLGEMMKernelType::RESHAPED_ONLY_RHS:
        {
            configure_reshaped_only_rhs(compile_context, a, b, c_to_use, output, alpha, beta, gemm_info);
            break;
        }
        default:
        {
            ARM_COMPUTE_ERROR("GEMMType not supported");
        }
    }
}

Status CLGEMM::validate(const ITensorInfo *a, const ITensorInfo *b, const ITensorInfo *c, const ITensorInfo *output, float alpha, float beta, const GEMMInfo &gemm_info)
{
    // Get the GPU target
    bool               reinterpret_input_as_3d = gemm_info.reinterpret_input_as_3d();
    const unsigned int m                       = reinterpret_input_as_3d ? (a->dimension(1) * a->dimension(2)) : a->dimension(1);
    const unsigned int n                       = b->dimension(0);
    const unsigned int k                       = a->dimension(0);
    const unsigned int batch_size              = reinterpret_input_as_3d ? a->dimension(3) : a->dimension(2);

    // Select GEMMType
    CLGEMMKernelType gemm_kernel_type = auto_select_gemm_kernel(auto_heuristics::CommonQuery
    {
        CLScheduler::get().target(), a->data_type(), m, n, k, batch_size,
    },
    gemm_info.reshape_b_only_on_first_run());

    const bool fuse_add_c = (!(helpers::float_ops::is_zero(beta)) && c != nullptr);

    const ITensorInfo *c_to_use = fuse_add_c ? c : nullptr;

    switch(gemm_kernel_type)
    {
        case CLGEMMKernelType::NATIVE_V1:
        {
            ARM_COMPUTE_RETURN_ON_ERROR(validate_native_v1(a, b, c_to_use, output, alpha, beta, gemm_info));
            break;
        }
        case CLGEMMKernelType::RESHAPED_V1:
        {
            ARM_COMPUTE_RETURN_ON_ERROR(validate_reshaped_v1(a, b, c_to_use, output, alpha, beta, gemm_info));
            break;
        }
        case CLGEMMKernelType::RESHAPED:
        {
            ARM_COMPUTE_RETURN_ON_ERROR(validate_reshaped(a, b, c_to_use, output, alpha, beta, gemm_info));
            break;
        }
        case CLGEMMKernelType::RESHAPED_ONLY_RHS:
        {
            ARM_COMPUTE_RETURN_ON_ERROR(validate_reshaped_only_rhs(a, b, c_to_use, output, alpha, beta, gemm_info));
            break;
        }
        default:
        {
            ARM_COMPUTE_RETURN_ERROR_MSG("GEMMType not supported");
        }
    }

    return Status{};
}

void CLGEMM::run()
{
    prepare();
    MemoryGroupResourceScope scope_mg(_memory_group);

    // Run matrix multiply kernel
    switch(_gemm_kernel_type)
    {
        case CLGEMMKernelType::NATIVE_V1:
        {
            CLScheduler::get().enqueue(*_mm_kernel, true);
            break;
        }
        case CLGEMMKernelType::RESHAPED_V1:
        {
            // Run interleave kernel
            CLScheduler::get().enqueue(*_reshape_lhs_kernel, false);

            if(!_reshape_b_only_on_first_run)
            {
                // Run transpose kernel
                if(_weights_manager && _weights_manager->are_weights_managed(_original_b))
                {
                    _weights_manager->run(_original_b, _reshape_rhs_kernel_managed.get());
                }
                else
                {
                    CLScheduler::get().enqueue(*_reshape_rhs_kernel, false);
                }
            }

            CLScheduler::get().enqueue(*_mm_kernel, true);
            break;
        }
        case CLGEMMKernelType::RESHAPED:
        {
            // Run interleave kernel
            CLScheduler::get().enqueue(*_reshape_lhs_kernel, false);

            if(!_reshape_b_only_on_first_run)
            {
                // Run transpose kernel
                if(_weights_manager && _weights_manager->are_weights_managed(_original_b))
                {
                    _weights_manager->run(_original_b, _reshape_rhs_kernel_managed.get());
                }
                else
                {
                    CLScheduler::get().enqueue(*_reshape_rhs_kernel, false);
                }
            }

            CLScheduler::get().enqueue(*_mm_reshaped_kernel, true);
            break;
        }
        case CLGEMMKernelType::RESHAPED_ONLY_RHS:
        {
            if(!_reshape_b_only_on_first_run)
            {
                // Run transpose kernel
                if(_weights_manager && _weights_manager->are_weights_managed(_original_b))
                {
                    _weights_manager->run(_original_b, _reshape_rhs_kernel_managed.get());
                }
                else
                {
                    CLScheduler::get().enqueue(*_reshape_rhs_kernel, false);
                }
            }
            // In case of RESHAPED_ONLY_RHS, we need to check the padding requirement
            // Check if the lhs or dst tensors have padding
            const unsigned int cross_plane_pad_lhs = _lhs->info()->padding().top + _lhs->info()->padding().bottom;
            const unsigned int cross_plane_pad_dst = _dst->info()->padding().top + _dst->info()->padding().bottom;

            bool has_pad_y = (cross_plane_pad_lhs != 0) || (cross_plane_pad_dst != 0);
            if(has_pad_y)
            {
                CLScheduler::get().enqueue(*_mm_reshaped_only_rhs_fallback_kernel, true);
            }
            else
            {
                CLScheduler::get().enqueue(*_mm_reshaped_only_rhs_kernel, true);
            }
            break;
        }
        default:
        {
            ARM_COMPUTE_ERROR("GEMMType not supported");
        }
    }
}

void CLGEMM::prepare()
{
    if(!_is_prepared)
    {
        if(_gemm_kernel_type != CLGEMMKernelType::NATIVE_V1 && _reshape_b_only_on_first_run)
        {
            if(_weights_manager && _weights_manager->are_weights_managed(_original_b))
            {
                _weights_manager->run(_original_b, _reshape_rhs_kernel_managed.get());
            }
            else
            {
                // Run transpose kernel and mark original weights tensor as unused
                _tmp_b.allocator()->allocate();
                CLScheduler::get().enqueue(*_reshape_rhs_kernel, false);
                _original_b->mark_as_unused();
            }
        }
        CLScheduler::get().queue().finish();
        _is_prepared = true;
    }
}
} // namespace arm_compute

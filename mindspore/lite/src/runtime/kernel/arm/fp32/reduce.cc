/**
 * Copyright 2020 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/runtime/kernel/arm/fp32/reduce.h"
#include "schema/model_generated.h"
#include "src/kernel_registry.h"
#include "include/errorcode.h"
#include "src/runtime/runtime_api.h"
#include "nnacl/fp32/reduce.h"
#include "src/runtime/kernel/arm/base/reduce_base.h"

using mindspore::kernel::KERNEL_ARCH::kCPU;
using mindspore::lite::KernelRegistrar;
using mindspore::lite::RET_ERROR;
using mindspore::lite::RET_NULL_PTR;
using mindspore::lite::RET_OK;
using mindspore::schema::PrimitiveType_Mean;
using mindspore::schema::PrimitiveType_Reduce;
using mindspore::schema::ReduceMode;
using mindspore::schema::ReduceMode_ReduceMax;
using mindspore::schema::ReduceMode_ReduceMean;
using mindspore::schema::ReduceMode_ReduceMin;
using mindspore::schema::ReduceMode_ReduceProd;
using mindspore::schema::ReduceMode_ReduceSum;
using mindspore::schema::ReduceMode_ReduceSumSquare;

namespace mindspore::kernel {

int ReduceCPUKernel::Init() {
  auto ret = ReduceBaseCPUKernel::Init();
  if (ret != RET_OK) {
    return ret;
  }

  switch (mode_) {
    case static_cast<int>(ReduceMode_ReduceSum): {
      reducer_ = ReduceSum;
      break;
    }
    case static_cast<int>(ReduceMode_ReduceMean): {
      reducer_ = ReduceMean;
      break;
    }
    case static_cast<int>(ReduceMode_ReduceMax): {
      reducer_ = ReduceMax;
      break;
    }
    case static_cast<int>(ReduceMode_ReduceMin): {
      reducer_ = ReduceMin;
      break;
    }
    case static_cast<int>(ReduceMode_ReduceProd): {
      reducer_ = ReduceProd;
      break;
    }
    case static_cast<int>(ReduceMode_ReduceSumSquare): {
      reducer_ = ReduceSumSquare;
      break;
    }
    default:
      MS_LOG(ERROR) << "Reduce unsupported reduce mode: " << mode_;
      return RET_ERROR;
  }

  if (!InferShapeDone()) {
    return RET_OK;
  }
  return ReSize();
}

int ReduceCPUKernel::ReSize() { return ReduceBaseCPUKernel::ReSize(); }

int ReduceCPUKernel::CallReduceUnit(int task_id) {
  auto ret = reducer_(outer_size_, inner_size_, axis_size_, src_data_, dst_data_, task_id, context_->thread_num_);
  return ret;
}

int ReduceImpl(void *cdata, int task_id) {
  auto reduce = reinterpret_cast<ReduceCPUKernel *>(cdata);
  auto error_code = reduce->CallReduceUnit(task_id);
  if (error_code != RET_OK) {
    MS_LOG(ERROR) << "Reduce Run error task_id[" << task_id << "] error_code[" << error_code << "]";
    return RET_ERROR;
  }
  return RET_OK;
}

int ReduceCPUKernel::Run() {
  auto prepare_ret = Prepare();
  if (prepare_ret != RET_OK) {
    MS_LOG(ERROR) << "Prepare fail!ret: " << prepare_ret;
    return prepare_ret;
  }
  auto ret = MallocTmpBuffer();
  if (ret != RET_OK) {
    FreeTmpBuffer();
    return ret;
  }

  src_data_ = static_cast<float *>(in_tensors_.at(0)->MutableData());
  for (size_t i = 0; i < static_cast<size_t>(num_axes_); ++i) {
    if (i != static_cast<size_t>(num_axes_ - 1)) {
      dst_data_ = data_buffers_[i];
    } else {
      dst_data_ = reinterpret_cast<float *>(out_tensors_.at(0)->MutableData());
    }
    outer_size_ = outer_sizes_[i];
    inner_size_ = inner_sizes_[i];
    axis_size_ = axis_sizes_[i];
    auto error_code = ParallelLaunch(THREAD_POOL_DEFAULT, ReduceImpl, this, context_->thread_num_);
    if (error_code != RET_OK) {
      MS_LOG(ERROR) << "Reduce run error, error_code[" << error_code << "]";
      FreeTmpBuffer();
      return RET_ERROR;
    }
    src_data_ = dst_data_;
  }
  FreeTmpBuffer();
  return RET_OK;
}

int ReduceCPUKernel::MallocTmpBuffer() {
  data_buffers_.clear();
  for (auto size : buffer_sizes_) {
    float *buffer = reinterpret_cast<float *>(context_->allocator->Malloc(size * sizeof(float)));
    if (buffer == nullptr) {
      MS_LOG(ERROR) << "Malloc data failed.";
      return RET_ERROR;
    }
    data_buffers_.emplace_back(buffer);
  }
  return RET_OK;
}

void ReduceCPUKernel::FreeTmpBuffer() {
  for (size_t i = 0; i < data_buffers_.size(); i++) {
    float *buffer = data_buffers_[i];
    if (buffer != nullptr) {
      context_->allocator->Free(buffer);
      buffer = nullptr;
    }
  }
  data_buffers_.clear();
}
}  // namespace mindspore::kernel

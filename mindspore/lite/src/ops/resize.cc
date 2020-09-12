/**
 * Copyright 2019-2020 Huawei Technologies Co., Ltd
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

#include "src/ops/resize.h"

namespace mindspore {
namespace lite {
#ifdef PRIMITIVE_WRITEABLE
int Resize::GetFormat() const { return this->primitive_->value.AsResize()->format; }
int Resize::GetMethod() const { return this->primitive_->value.AsResize()->method; }
int64_t Resize::GetNewHeight() const { return this->primitive_->value.AsResize()->newHeight; }
int64_t Resize::GetNewWidth() const { return this->primitive_->value.AsResize()->newWidth; }
bool Resize::GetAlignCorners() const { return this->primitive_->value.AsResize()->alignCorners; }
bool Resize::GetPreserveAspectRatio() const { return this->primitive_->value.AsResize()->preserveAspectRatio; }

void Resize::SetFormat(int format) { this->primitive_->value.AsResize()->format = (schema::Format)format; }
void Resize::SetMethod(int method) { this->primitive_->value.AsResize()->method = (schema::ResizeMethod)method; }
void Resize::SetNewHeight(int64_t new_height) { this->primitive_->value.AsResize()->newHeight = new_height; }
void Resize::SetNewWidth(int64_t new_width) { this->primitive_->value.AsResize()->newWidth = new_width; }
void Resize::SetAlignCorners(bool align_corners) { this->primitive_->value.AsResize()->alignCorners = align_corners; }
void Resize::SetPreserveAspectRatio(bool preserve_aspect_ratio) {
  this->primitive_->value.AsResize()->preserveAspectRatio = preserve_aspect_ratio;
}

int Resize::UnPackAttr(const Primitive &prim, const std::vector<AnfNodePtr> &inputs) {
  if (this->primitive_ == nullptr) {
    this->primitive_ = new (std::nothrow) schema::PrimitiveT;
    if (this->primitive_ == nullptr) {
      MS_LOG(ERROR) << "new primitiveT failed";
      return RET_ERROR;
    }
    this->primitive_->value.type = schema::PrimitiveType_Resize;
  }
  if (this->primitive_->value.type != schema::PrimitiveType_Resize) {
    MS_LOG(ERROR) << "primitive_ type is error:" << this->primitive_->value.type;
    return RET_ERROR;
  }
  if (this->primitive_->value.value == nullptr) {
    auto attr = new (std::nothrow) schema::ResizeT();
    if (prim.instance_name() == "ResizeNearestNeighbor") {
      attr->method = schema::ResizeMethod_NEAREST_NEIGHBOR;
    } else if (prim.instance_name() == "ResizeBilinear") {
      attr->method = schema::ResizeMethod_BILINEAR;
    } else {
      MS_LOG(ERROR) << "wrong resize type";
      return RET_ERROR;
    }
    std::vector<int> targetSize = GetValue<std::vector<int>>(prim.GetAttr("size"));
    attr->newHeight = targetSize[0];
    attr->newWidth = targetSize[1];
    attr->alignCorners = GetValue<bool>(prim.GetAttr("align_corners"));

    this->primitive_->value.value = attr;
    if (this->primitive_->value.value == nullptr) {
      MS_LOG(ERROR) << "new primitiveT value failed";
      return RET_ERROR;
    }
  }
  return RET_OK;
}
#else

int Resize::GetFormat() const { return this->primitive_->value_as_Resize()->format(); }
int Resize::GetMethod() const { return this->primitive_->value_as_Resize()->method(); }
int64_t Resize::GetNewHeight() const { return this->primitive_->value_as_Resize()->newHeight(); }
int64_t Resize::GetNewWidth() const { return this->primitive_->value_as_Resize()->newWidth(); }
bool Resize::GetAlignCorners() const { return this->primitive_->value_as_Resize()->alignCorners(); }
bool Resize::GetPreserveAspectRatio() const { return this->primitive_->value_as_Resize()->preserveAspectRatio(); }
int Resize::UnPackToFlatBuilder(const schema::Primitive *primitive, flatbuffers::FlatBufferBuilder *fbb) {
  MS_ASSERT(nullptr != primitive);
  MS_ASSERT(nullptr != fbb);
  auto attr = primitive->value_as_Resize();
  if (attr == nullptr) {
    MS_LOG(ERROR) << "value_as_Resize return nullptr";
    return RET_ERROR;
  }
  auto val_offset = schema::CreateResize(*fbb, attr->format(), attr->method(), attr->newHeight(), attr->newWidth(),
                                         attr->alignCorners(), attr->preserveAspectRatio());
  auto prim_offset = schema::CreatePrimitive(*fbb, schema::PrimitiveType_Resize, val_offset.o);
  fbb->Finish(prim_offset);
  return RET_OK;
}
#endif
namespace {
constexpr int kInputRank = 4;
}  // namespace
int Resize::InferShape(std::vector<lite::Tensor *> inputs_, std::vector<lite::Tensor *> outputs_) {
  MS_ASSERT(this->primitive_ != nullptr);
  auto input = inputs_.front();
  if (input == nullptr) {
    return 1;
  }
  if (input->shape().size() != kInputRank) {
    MS_LOG(ERROR) << "Size of input shape is wrong.";
    return RET_ERROR;
  }

  auto output = outputs_.front();
  if (output == nullptr) {
    return 1;
  }
  output->set_data_type(input->data_type());
  output->SetFormat(input->GetFormat());
  if (!GetInferFlag()) {
    return RET_OK;
  }
  auto new_height = GetNewHeight();
  auto new_width = GetNewWidth();

  std::vector<int> output_shape;
  output_shape.push_back(input->Batch());
  output_shape.push_back(new_height);
  output_shape.push_back(new_width);
  output_shape.push_back(input->Channel());
  output->set_shape(output_shape);

  return RET_OK;
}
}  // namespace lite
}  // namespace mindspore
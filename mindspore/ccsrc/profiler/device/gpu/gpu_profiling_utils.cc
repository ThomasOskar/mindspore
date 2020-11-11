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

#include "profiler/device/gpu/gpu_profiling_utils.h"
#include "backend/kernel_compiler/kernel.h"
#include "backend/session/anf_runtime_algorithm.h"
#include "utils/ms_utils.h"
#include "utils/ms_context.h"
#include "utils/utils.h"

namespace mindspore {
namespace profiler {
namespace gpu {
constexpr char kFpStartNode[] = "PROFILING_FP_START";
constexpr char kBpEndNode[] = "PROFILING_BP_END";
constexpr char kIterEndNode[] = "PROFILING_ITER_END";
constexpr int fpExistGraphId = 2;

uint32_t ProfilingUtils::last_graph_id = 0;
bool ProfilingUtils::have_communication_op = false;
ProfilingTraceInfo ProfilingUtils::profiling_trace = {"", "", "", "", false};

ProfilingTraceInfo ProfilingUtils::GetProfilingTraceFromEnv(NotNull<const session::KernelGraph *> graph_ptr) {
  MS_LOG(INFO) << "get current subgraph op name start.";
  auto &cnode_exec_order = graph_ptr->execution_order();
  if (cnode_exec_order.empty()) {
    return profiling_trace;
  }
  uint32_t current_graph_id = graph_ptr->graph_id();
  // current graph id less than last graph id indicates all subgraph have called.
  if (current_graph_id < last_graph_id) {
    profiling_trace.IsFirstStepEnd = true;
    OutputStepTraceOpNameStatus();
    return profiling_trace;
  }

  SetTraceIterStart(cnode_exec_order);
  SetTraceIterEnd(cnode_exec_order);
  SetTraceFpStart(cnode_exec_order, current_graph_id);
  SetTraceBpEnd(cnode_exec_order);
  GetTraceHccl(cnode_exec_order);

  last_graph_id = current_graph_id;
  return profiling_trace;
}

void ProfilingUtils::OutputStepTraceOpNameStatus() {
  if (!profiling_trace.IsValid()) {
    MS_LOG(ERROR) << "Did not get all the step_trace op name.";
  }
  MS_LOG(INFO) << "[profiling]trace_iter_start: " << profiling_trace.trace_iter_start
               << "trace_fp_start: " << profiling_trace.trace_fp_start
               << "trace_bp_end: " << profiling_trace.trace_bp_end
               << "trace_iter_end: " << profiling_trace.trace_iter_end;
  MS_LOG(INFO) << "get step_trace op name end.";
}

void ProfilingUtils::GetTraceHccl(const std::vector<CNodePtr> &cnode_exec_order) {
  for (const auto &node : cnode_exec_order) {
    if (AnfAlgo::IsCommunicationOp(node)) {
      MS_EXCEPTION_IF_NULL(node);
      if (std::find(profiling_trace.trace_custom_node.begin(), profiling_trace.trace_custom_node.end(),
                    node->fullname_with_scope()) == profiling_trace.trace_custom_node.end()) {
        profiling_trace.trace_custom_node.push_back(node->fullname_with_scope());
      }
      MS_LOG(INFO) << "[profiling]Get hccl node:" << node->fullname_with_scope();
    }
  }
}

void ProfilingUtils::SetTraceIterStart(const std::vector<CNodePtr> &cnode_exec_order) {
  if (!profiling_trace.trace_iter_start.empty()) {
    return;
  }

  auto first_node = cnode_exec_order.front();
  MS_EXCEPTION_IF_NULL(first_node);
  if (AnfAlgo::GetCNodeName(first_node) == kGetNextOpName) {
    profiling_trace.trace_iter_start = first_node->fullname_with_scope();
  }
}

void ProfilingUtils::SetTraceFpStart(const std::vector<CNodePtr> &cnode_exec_order, uint32_t graph_id) {
  if (!profiling_trace.trace_fp_start.empty()) {
    return;
  }

  const char *trace_fp_start = std::getenv(kFpStartNode);
  if (trace_fp_start != nullptr) {
    profiling_trace.trace_fp_start = std::string(trace_fp_start);
    MS_LOG(INFO) << "Set the Fp Start Op Name from Environment Variable:" << profiling_trace.trace_fp_start;
    return;
  }

  if (graph_id == fpExistGraphId) {
    auto first_node = cnode_exec_order.front();
    MS_EXCEPTION_IF_NULL(first_node);
    profiling_trace.trace_fp_start = first_node->fullname_with_scope();
  }
}

void ProfilingUtils::SetTraceBpEnd(const std::vector<CNodePtr> &cnode_exec_order) {
  const char *trace_bp_end = std::getenv(kBpEndNode);
  if (trace_bp_end != nullptr) {
    profiling_trace.trace_bp_end = std::string(trace_bp_end);
    MS_LOG(INFO) << "Set the Bp End Op Name from Environment Variable:" << profiling_trace.trace_bp_end;
    return;
  }

  std::string bp_end_str;
  // Contain hccl kernel (try to find the last communication op)
  auto iter = cnode_exec_order.rbegin();
  while (iter != cnode_exec_order.rend()) {
    if (AnfAlgo::IsCommunicationOp(*iter)) {
      break;
    }
    ++iter;
  }
  // If find the communication op
  if (iter != cnode_exec_order.rend()) {
    // store communication op input nodes' name
    std::set<std::string> ar_input_node_names;
    for (size_t i = 0; i < AnfAlgo::GetInputTensorNum(*iter); ++i) {
      auto input_node_with_index = AnfAlgo::GetPrevNodeOutput(*iter, i);
      auto input_node = input_node_with_index.first;
      ar_input_node_names.insert(input_node->fullname_with_scope());
    }
    // start from previous node
    ++iter;
    // find input names in previous node
    while (iter != cnode_exec_order.rend()) {
      if (ar_input_node_names.find((*iter)->fullname_with_scope()) != ar_input_node_names.end()) {
        bp_end_str = (*iter)->fullname_with_scope();
        break;
      }
      ++iter;
    }
  }

  if (bp_end_str.empty() && !have_communication_op) {
    bp_end_str = GetGraphSecondLastKernelName(cnode_exec_order);
  }

  if (!bp_end_str.empty()) {
    profiling_trace.trace_bp_end = bp_end_str;
  }
}

void ProfilingUtils::SetTraceIterEnd(const std::vector<CNodePtr> &cnode_exec_order) {
  const char *trace_iter_end = std::getenv(kIterEndNode);
  if (trace_iter_end != nullptr) {
    profiling_trace.trace_iter_end = std::string(trace_iter_end);
    MS_LOG(INFO) << "Set the Iter End Op Name from Environment Variable:" << profiling_trace.trace_iter_end;
    return;
  }

  auto iter_end = cnode_exec_order.rbegin();
  profiling_trace.trace_iter_end = (*iter_end)->fullname_with_scope();
}

std::string ProfilingUtils::GetGraphSecondLastKernelName(const std::vector<CNodePtr> &cnode_exec_order) {
  std::string second_last_kernel_name;
  auto iter = cnode_exec_order.rbegin();
  ++iter;
  if (iter != cnode_exec_order.rend()) {
    second_last_kernel_name = (*iter)->fullname_with_scope();
  }

  return second_last_kernel_name;
}

}  // namespace gpu
}  // namespace profiler
}  // namespace mindspore

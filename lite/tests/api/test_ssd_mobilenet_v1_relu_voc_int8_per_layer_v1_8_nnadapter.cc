// Copyright (c) 2019 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <vector>
#include "lite/api/paddle_api.h"
#include "lite/api/test/lite_api_test_helper.h"
#include "lite/api/test/test_helper.h"
#include "lite/tests/api/PASCALVOC2012_utility.h"
#include "lite/tests/api/utility.h"

DEFINE_string(data_dir, "", "data dir");
DEFINE_int32(iteration, 10, "iteration times to run");
DEFINE_int32(batch, 1, "batch of image");
DEFINE_int32(channel, 3, "image channel");
DEFINE_int32(height, 300, "image height");
DEFINE_int32(width, 300, "image width");

namespace paddle {
namespace lite {

TEST(ssd_mobilentert_v1,
     test_ssd_mobilenet_v1_relu_voc_int8_per_layer_v1_8_nnadapter) {
  std::vector<std::string> nnadapter_device_names;
  std::string nnadapter_context_properties;
  std::vector<paddle::lite_api::Place> valid_places;
  valid_places.push_back(lite_api::Place{TARGET(kNNAdapter), PRECISION(kInt8)});
  valid_places.push_back(
      lite_api::Place{TARGET(kNNAdapter), PRECISION(kFloat)});
#if defined(LITE_WITH_ARM)
  valid_places.push_back(lite_api::Place{TARGET(kARM), PRECISION(kInt8)});
  valid_places.push_back(lite_api::Place{TARGET(kARM), PRECISION(kFloat)});
#elif defined(LITE_WITH_X86)
  valid_places.push_back(lite_api::Place{TARGET(kX86), PRECISION(kInt8)});
  valid_places.push_back(lite_api::Place{TARGET(kX86), PRECISION(kFloat)});
#else
  LOG(INFO) << "Unsupported host arch!";
  return;
#endif
#if defined(LITE_WITH_NNADAPTER)
#if defined(NNADAPTER_WITH_ROCKCHIP_NPU)
  nnadapter_device_names.emplace_back("rockchip_npu");
#elif defined(NNADAPTER_WITH_MEDIATEK_APU)
  nnadapter_device_names.emplace_back("mediatek_apu");
#elif defined(NNADAPTER_WITH_VERISILICON_TIMVX)
  nnadapter_device_names.emplace_back("verisilicon_timvx");
#elif defined(NNADAPTER_WITH_ANDROID_NNAPI)
  nnadapter_device_names.emplace_back("android_nnapi");
#elif defined(NNADAPTER_WITH_GOOGLE_XNNPACK)
  nnadapter_device_names.emplace_back("google_xnnpack");
#else
  return;
#endif
#else
  return;
#endif
  std::shared_ptr<paddle::lite_api::PaddlePredictor> predictor = nullptr;
  // Use the full api with CxxConfig to generate the optimized model
  lite_api::CxxConfig cxx_config;
  cxx_config.set_model_dir(FLAGS_model_dir);
  cxx_config.set_valid_places(valid_places);
  cxx_config.set_nnadapter_device_names(nnadapter_device_names);
  cxx_config.set_nnadapter_context_properties(nnadapter_context_properties);
  predictor = lite_api::CreatePaddlePredictor(cxx_config);
  predictor->SaveOptimizedModel(FLAGS_model_dir,
                                paddle::lite_api::LiteModelType::kNaiveBuffer);
  // Use the light api with MobileConfig to load and run the optimized model
  paddle::lite_api::MobileConfig mobile_config;
  mobile_config.set_model_from_file(FLAGS_model_dir + ".nb");
  mobile_config.set_threads(FLAGS_threads);
  mobile_config.set_power_mode(
      static_cast<lite_api::PowerMode>(FLAGS_power_mode));
  mobile_config.set_nnadapter_device_names(nnadapter_device_names);
  mobile_config.set_nnadapter_context_properties(nnadapter_context_properties);
  predictor = paddle::lite_api::CreatePaddlePredictor(mobile_config);

  std::string raw_data_dir = FLAGS_data_dir + std::string("/raw_data");
  std::vector<int> input_shape{
      FLAGS_batch, FLAGS_channel, FLAGS_height, FLAGS_width};
  auto raw_data = ReadRawData(raw_data_dir, input_shape, FLAGS_iteration);

  int input_size = 1;
  for (auto i : input_shape) {
    input_size *= i;
  }

  FLAGS_warmup = 1;
  for (int i = 0; i < FLAGS_warmup; ++i) {
    SetDetectionInput(predictor, input_shape, std::vector<float>(), input_size);
    predictor->Run();
  }

  std::vector<std::vector<float>> out_rets;
  out_rets.resize(FLAGS_iteration);
  double cost_time = 0;
  for (size_t i = 0; i < raw_data.size(); ++i) {
    SetDetectionInput(predictor, input_shape, raw_data[i], input_size);

    double start = GetCurrentUS();
    predictor->Run();
    cost_time += (GetCurrentUS() - start);

    auto output_tensor = predictor->GetOutput(0);
    auto output_shape = output_tensor->shape();
    auto output_data = output_tensor->data<float>();
    ASSERT_EQ(output_shape.size(), 2UL);
    ASSERT_GT(output_shape[0], 0);
    ASSERT_EQ(output_shape[1], 6);

    int output_size = output_shape[0] * output_shape[1];
    out_rets[i].resize(output_size);
    memcpy(&(out_rets[i].at(0)), output_data, sizeof(float) * output_size);
  }

  LOG(INFO) << "================== Speed Report ===================";
  LOG(INFO) << "Model: " << FLAGS_model_dir << ", threads num " << FLAGS_threads
            << ", warmup: " << FLAGS_warmup << ", batch: " << FLAGS_batch
            << ", iteration: " << FLAGS_iteration << ", spend "
            << cost_time / FLAGS_iteration / 1000.0 << " ms in average.";
}

}  // namespace lite
}  // namespace paddle

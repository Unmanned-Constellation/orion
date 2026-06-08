#ifdef ORION_ENABLE_DEEPSTREAM

// Custom nvinfer bounding-box parser for Ultralytics RT-DETR-R18 FP16.
//
// Expected output tensor (after Ultralytics TensorRT export with half=True):
//   output0: shape [batch, 300, nc+4]
//     indices 0-3: cx, cy, w, h — normalized to [0,1] relative to model input dims
//     indices 4 to 4+nc: class scores (not softmax, raw logits after sigmoid)
//
// This file is compiled into a shared library (rtdetr_parser.so) loaded by
// nvinfer at runtime via the custom-lib-path property. It is NOT linked into
// orion_perception.
//
// Verify the output tensor layout against the actual exported model before
// deploying — Ultralytics may change the export format across versions.

#include <algorithm>
#include <vector>

#include <nvdsinfer_custom_impl.h>

extern "C"
{

    // NOLINTNEXTLINE(readability-identifier-naming) — DeepStream-mandated symbol name
    auto NvDsInferParseRtDetr(std::vector<NvDsInferLayerInfo> const&     outputLayersInfo,
                              NvDsInferNetworkInfo const&                networkInfo,
                              NvDsInferParseDetectionParams const&       detectionParams,
                              std::vector<NvDsInferObjectDetectionInfo>& objectList) -> bool
    {
        if (outputLayersInfo.empty())
        {
            return false;
        }

        const auto& layer       = outputLayersInfo[0];
        const auto* data        = static_cast<const float*>(layer.buffer);
        const auto  num_dets    = static_cast<int>(layer.inferDims.d[0]); // 300
        const auto  num_values  = static_cast<int>(layer.inferDims.d[1]); // nc + 4
        const auto  num_classes = num_values - 4;

        if (num_classes <= 0)
        {
            return false;
        }

        // Use the first per-class threshold if available, else fall back to 0.5.
        const auto conf_threshold =
            detectionParams.numClassesConfigured > 0 ? detectionParams.perClassThreshold[0] : 0.5F;

        for (int det_idx = 0; det_idx < num_dets; ++det_idx)
        {
            const auto* det = data + det_idx * num_values;

            // Find the class with the highest score.
            const auto* scores_begin = det + 4;
            const auto* scores_end   = det + 4 + num_classes;
            const auto  max_it       = std::max_element(scores_begin, scores_end);
            const auto  max_score    = *max_it;

            if (max_score < conf_threshold)
            {
                continue;
            }

            const auto class_id = static_cast<int>(max_it - scores_begin);

            // Convert normalized [cx, cy, w, h] → pixel-space [left, top, w, h]
            const auto cx = det[0];
            const auto cy = det[1];
            const auto bw = det[2];
            const auto bh = det[3];

            auto obj                = NvDsInferObjectDetectionInfo{};
            obj.left                = (cx - bw * 0.5F) * static_cast<float>(networkInfo.width);
            obj.top                 = (cy - bh * 0.5F) * static_cast<float>(networkInfo.height);
            obj.width               = bw * static_cast<float>(networkInfo.width);
            obj.height              = bh * static_cast<float>(networkInfo.height);
            obj.classId             = class_id;
            obj.detectionConfidence = max_score;

            objectList.push_back(obj);
        }

        return true;
    }

} // extern "C"

#endif // ORION_ENABLE_DEEPSTREAM

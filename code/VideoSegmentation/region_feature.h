//
// Created by yanhang on 10/11/16.
//

#ifndef DYNAMICSTEREO_REGION_DESCRIPTOR_H
#define DYNAMICSTEREO_REGION_DESCRIPTOR_H

#include "types.h"
#include "pixel_feature.h"

#include <opencv2/opencv.hpp>
#include <memory>
#include <vector>
#include <iostream>

#include <glog/logging.h>

namespace dynamic_stereo{

    namespace video_segment{

        struct Region{
            std::vector<int> pix_id;
            cv::Mat desc;
        };

        class RegionFeatureExtractorBase: public FeatureBase{
        public:
            virtual void ExtractFromPixelFeatures(const cv::InputArray pixel_features,
                                                  const std::vector<Region*>& region,
                                                  const cv::OutputArray output) const = 0;

            virtual void MergeDescriptor(const cv::InputArray desc1, const cv::InputArray desc2,
                                         const cv::OutputArray merged) const = 0;
        };


        class RegionTransitionPattern: public RegionFeatureExtractorBase{
        public:
            RegionTransitionPattern(const int kFrames, const int s1, const int s2, const float theta,
                                    const DistanceMetricBase* pixel_distance,
                                    const TemporalFeatureExtractorBase* spatial_extractor_);

            virtual void ExtractFromPixelFeatures(const cv::InputArray pixel_features,
                                                  const std::vector<Region*>& region,
                                                  const cv::OutputArray output) const;

            virtual void MergeDescriptor(const cv::InputArray desc1, const cv::InputArray desc2,
                                         const cv::OutputArray merged) const{}

            virtual void printFeature(const cv::InputArray input) const{
                CHECK_NOTNULL(transition_pattern_.get())->printFeature(input);
            }

        private:
            std::shared_ptr<TransitionPattern> transition_pattern_;
            const TemporalFeatureExtractorBase* spatial_extractor_;
        };

        class RegionColorHist: public RegionFeatureExtractorBase{

        };

        class CombinedRegionExtractor: public RegionFeatureExtractorBase{
        public:
            CombinedRegionExtractor();
            virtual void ExtractFromPixelFeatures(const cv::InputArray pixel_features,
                                                  const std::vector<Region*>& region,
                                                  cv::OutputArray output) const;

            virtual void MergeDescriptor(const cv::InputArray desc1, const cv::InputArray desc2,
                                         const cv::OutputArray merged) const;

            virtual void printFeature(const cv::InputArray input) const;
        private:
        };

    }

}//namespace dynamic_stereo

#endif //DYNAMICSTEREO_REGION_DESCRIPTOR_H

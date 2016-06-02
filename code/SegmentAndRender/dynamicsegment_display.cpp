//
// Created by yanhang on 4/29/16.
//

#include <unordered_set>
#include "dynamicsegment.h"
#include "../base/thread_guard.h"
#include "../external/segment_gb/segment-image.h"
#include "../external/video_segmentation/segment_util/segmentation_io.h"
#include "../external/video_segmentation/segment_util/segmentation_util.h"

using namespace std;
using namespace cv;
using namespace Eigen;

namespace dynamic_stereo{
	cv::Mat getClassificationResult(const std::vector<cv::Mat>& input,
	                                const std::shared_ptr<Feature::FeatureConstructor> descriptor, const cv::Ptr<cv::ml::StatModel> classifier,
	                                const int stride){
		CHECK(!input.empty());
		CHECK(descriptor.get());
		CHECK(classifier.get());

		const int width = input[0].cols;
		const int height = input[0].rows;
		CHECK_EQ(width % stride, 0);
		CHECK_EQ(height % stride, 0);

		const int kFrame = (int)input.size();
		const int nSamples = width * height / stride / stride;

		int index = 0;
		//Notice: input images are in BGR color space!!!
		const int chuckSize = nSamples;
		Mat samplesCV(chuckSize, descriptor->getDim(), CV_32F, cv::Scalar::all(0));
		vector<float> array((size_t)kFrame * 3);
		Mat res(height/stride, width/stride, CV_8UC1, Scalar::all(0));
		for(auto y=0; y<height; y+=stride){
			for(auto x=0; x<width; x+=stride, ++index){
				for(auto v=0; v<input.size(); ++v){
					Vec3b pix = input[v].at<Vec3b>(y,x);
					array[v*3] = (float)pix[2];
					array[v*3+1] = (float)pix[1];
					array[v*3+2] = (float)pix[0];
				}
				vector<float> cursample;
				descriptor->constructFeature(array, cursample);
				for(auto d=0; d<descriptor->getDim(); ++d)
					samplesCV.at<float>(index % chuckSize, d) = cursample[d];
				if((index + 1) % chuckSize == 0){
					Mat chuckResult;
					classifier->predict(samplesCV, chuckResult);
					CHECK_EQ(chuckResult.rows, chuckSize);
					const float* pChuckResult = (float*) chuckResult.data;
					for(auto i=0; i<chuckSize; ++i){
						CHECK_GE(index-chuckSize+i+1, 0);
						CHECK_LT(index-chuckSize+i+1, nSamples);
						if(std::abs(pChuckResult[i] - 0.0f) < FLT_EPSILON)
							res.data[index - chuckSize + i + 1] = (uchar)0;
						if(std::abs(pChuckResult[i] - 1.0f) < FLT_EPSILON)
							res.data[index - chuckSize + i + 1] = (uchar)255;
						else
							CHECK(true) << "Invalid classification result";
					}
				}
			}
		}

		cv::resize(res, res,input[0].size(),INTER_NEAREST);
		return res;
	}

	void segmentDisplay(const FileIO& file_io, const int anchor,
	                    const std::vector<cv::Mat> &input, const cv::Mat& inputMask,
	                    const string& classifierPath, cv::Mat& displayLabels,
	                    std::vector<std::vector<Eigen::Vector2d> >& segmentsDisplay){
		CHECK(!input.empty());
		CHECK(inputMask.data);
		CHECK_EQ(inputMask.channels(), 1);
		char buffer[1024] = {};

		const int width = input[0].cols;
		const int height = input[0].rows;

		Mat segnetMask;
		cv::resize(inputMask, segnetMask, cv::Size(width, height), INTER_NEAREST);

		sprintf(buffer, "%s/midres/classification%05d.png", file_io.getDirectory().c_str(), anchor);
		Mat preSeg = imread(buffer, IMREAD_GRAYSCALE);
		if(!preSeg.data) {
			//shared_ptr<Feature::FeatureConstructor> descriptor(new Feature::RGBHist());
			Feature::ColorSpace cspace(Feature::ColorSpace::RGB);
			vector<int> kBins{10, 10, 10};
			shared_ptr<Feature::FeatureConstructor> descriptor(new Feature::ColorHist(cspace, kBins));
			printf("Dimension: %d\n", descriptor->getDim());


#ifdef __linux
			cv::Ptr<ml::StatModel> classifier = ml::SVM::load<ml::SVM>(classifierPath);
#else
			cv::Ptr<ml::StatModel> classifier = ml::SVM::load(classifierPath);
#endif

			printf("Running classification...\n");
			preSeg = getClassificationResult(input, descriptor, classifier, 2);
			imwrite(buffer, preSeg);
			imshow("classification", preSeg);
		}
		vector<Mat> videoSeg;
		sprintf(buffer, "%s/midres/segment%05d.pb", file_io.getDirectory().c_str(), anchor);
		printf("Imporing video segmentation...\n");
		importVideoSegmentation(string(buffer), videoSeg);
		CHECK_EQ(videoSeg.size(), input.size());
		filterBoudary(videoSeg, preSeg);
		//filterBoudary(input, preSeg);
		return;

		cv::erode(preSeg, preSeg, cv::getStructuringElement(MORPH_ELLIPSE, cv::Size(5,5)));
		cv::dilate(preSeg, preSeg, cv::getStructuringElement(MORPH_ELLIPSE, cv::Size(5,5)));

//
//        Depth dynamicness;
//        //computeColorConfidence(input, dynamicness);
//        dynamicness.updateStatics();
//
//        sprintf(buffer, "%s/temp/conf_dynamicness%05d.jpg", file_io.getDirectory().c_str(), anchor);
//        dynamicness.saveImage(string(buffer));
//
//        const double dynamic_thres = dynamicness.getAverageDepth() + 2 * dynamicness.getDepthVariance();
//        const double static_thres = dynamicness.getAverageDepth();
//
//        printf("Dynamic threshold: %.3f\n", dynamic_thres);
//        Mat regionCan(height, width, CV_8UC1, Scalar::all(0));
//        Mat staticCan(height, width, CV_8UC1, Scalar::all(0));
//
//        for(auto i=0; i<width * height; ++i){
//            if(dynamicness[i] >= dynamic_thres)
//                regionCan.data[i] = 255;
//            if(dynamicness[i] <= static_thres)
//                staticCan.data[i] = 255;
//        }
//        //morphological operation
//        const int r1 = 3, r2 = 9, r3 = 11;
//        cv::erode(regionCan,regionCan,cv::getStructuringElement(MORPH_ELLIPSE,cv::Size(r1,r1)));
//        cv::dilate(regionCan,regionCan,cv::getStructuringElement(MORPH_ELLIPSE,cv::Size(r2,r2)));
//
//        cv::erode(staticCan,staticCan,cv::getStructuringElement(MORPH_ELLIPSE,cv::Size(r3,r3)));
//        //cv::dilate(staticCan,staticCan,cv::getStructuringElement(MORPH_ELLIPSE,cv::Size(r2,r2)));
//
//        sprintf(buffer, "%s/temp/conf_dynRegion%05d.jpg", file_io.getDirectory().c_str(), anchor);
//        imwrite(buffer, regionCan);
//
//        sprintf(buffer, "%s/temp/conf_staRegion%05d.jpg", file_io.getDirectory().c_str(), anchor);
//        imwrite(buffer, staticCan);
//
		//connect component analysis
		Mat labels, stats, centroid;
		int nLabel = cv::connectedComponentsWithStats(preSeg, labels, stats, centroid);
		const int min_area = 300;
		const int fR = 10;
		const int* pLabel = (int*) labels.data;
		const int min_multi = 2;
		const int kComponent = 5;
		const int min_nSample = 1000;
		const double max_areagain = 3.0;
		const double maxRatioOcclu = 0.3;
//
		displayLabels = Mat(height, width, labels.type(), Scalar::all(0));
		int kOutputLabel = 0;
//
//        //compute nLog threshold
//        printf("Computing nLog threshold...\n");
//        //const double nLogThres = computeNlogThreshold(input, segnetMask, kComponent);
//        //const double nLogThres = 20;
//        //const double probThres = 0.2;
//
		const int testL = -1;

		for(auto l=1; l<nLabel; ++l){
			if(testL > 0 && l != testL)
				continue;

			const int area = stats.at<int>(l, CC_STAT_AREA);
			//search for bounding box.
			//The number of static samples inside the window should be at least twice of of area

			const int cx = stats.at<int>(l,CC_STAT_LEFT) + stats.at<int>(l,CC_STAT_WIDTH) / 2;
			const int cy = stats.at<int>(l,CC_STAT_TOP) + stats.at<int>(l,CC_STAT_HEIGHT) / 2;

			printf("========================\n");
			printf("label:%d/%d, centroid:(%d,%d), area:%d\n", l, nLabel, cx, cy, area);
			if(segnetMask.at<uchar>(cy,cx) < 200)
				continue;
			if(area < min_area) {
				printf("Area too small\n");
				continue;
			}

			int nOcclu = 0;
			for(auto y=0; y<height; ++y){
				for(auto x=0; x<width; ++x){
					if(pLabel[y*width+x] != l)
						continue;
					int pixOcclu = 0;
					for(auto v=0; v<input.size(); ++v){
						if(input[v].at<Vec3b>(y,x) == Vec3b(0,0,0))
							pixOcclu++;
					}
					if(pixOcclu > (int)input.size() / 3)
						nOcclu++;
				}
			}
			if(testL == l){
				printf("nOcclu:%d\n", nOcclu);
			}
			if(nOcclu > maxRatioOcclu * area) {
				printf("Violate occlusion constraint\n");
				continue;
			}

//			if(l == testL){
//				Mat tempMat = input[input.size()/2].clone();
//				for(auto y=0; y<height; ++y){
//					for(auto x=0; x<width; ++x){
//						if(pLabel[y*width+x] == l)
//							tempMat.at<Vec3b>(y,x) = tempMat.at<Vec3b>(y,x) * 0.5 + Vec3b(0,0,128);
//						else
//							tempMat.at<Vec3b>(y,x) = tempMat.at<Vec3b>(y,x) * 0.5 + Vec3b(128,0,0);
//					}
//				}
//				sprintf(buffer, "%s/temp/component%03d.jpg", file_io.getDirectory().c_str(), l);
//				imwrite(buffer, tempMat);
//			}


			//refine segmentation
//			Mat segRes = input[input.size()/2].clone();
//			for(auto y=0; y<height; ++y){
//				for(auto x=0; x<width; ++x){
//					if(labels.at<int>(y,x) == l){
//						displayLabels.at<int>(y,x) = kOutputLabel;
//						segRes.at<Vec3b>(y,x) = segRes.at<Vec3b>(y,x) * 0.5 + Vec3b(0,0,255) * 0.5;
//					}else
//						segRes.at<Vec3b>(y,x) = segRes.at<Vec3b>(y,x) * 0.5 + Vec3b(255,0,0) * 0.5;
//				}
//			}
			kOutputLabel++;
//            sprintf(buffer, "%s/temp/segmask_b%05d_com%03d.jpg", file_io.getDirectory().c_str(), anchor, l);
//            imwrite(buffer, segRes);
		}

//		Mat labelsLarge;
//		cv::resize(displayLabels, labelsLarge, cv::Size(width * downsample, height * downsample), 0, 0, INTER_NEAREST);
//		segmentsDisplay.resize((size_t)kOutputLabel);
//		for(auto y=0; y<labelsLarge.rows; ++y){
//			for(auto x=0; x<labelsLarge.cols; ++x){
//				int l = labelsLarge.at<int>(y,x);
//				CHECK_LE(l, segmentsDisplay.size());
//				if(l > 0){
//					segmentsDisplay[l-1].push_back(Vector2d(x,y));
//				}
//			}
//		}
	}


	void filterBoudary(const std::vector<cv::Mat> &seg, cv::Mat &input){
		char buffer[1024] = {};
		CHECK(!seg.empty());
		CHECK_EQ(input.type(), CV_8UC1);
		printf("Filter boundary\n");
		for(auto i=0; i<seg.size(); ++i){
			CHECK_NOTNULL(seg[i].data);
			CHECK_EQ(seg[i].type(), CV_32S);
			CHECK_EQ(seg[i].size(), input.size());
			Mat vis = segment_gb::visualizeSegmentation(seg[i]);
			sprintf(buffer, "seg_video%05d.jpg", i);
			imwrite(buffer, vis);
		}
		const int width = seg[0].cols;
		const int height = seg[0].rows;
		const int kPix = width * height;

		int nLabel = -1;
		//get number of labels
		for(auto i=0; i<seg.size(); ++i){
			double minl, maxl;
			cv::minMaxLoc(seg[i], &minl, &maxl);
			nLabel = std::max(nLabel, (int)maxl);
		}
		nLabel++;

		//group pixel to labels for fast query
		//vector<vector<int> > pixelGroups((size_t)nLabel);
		vector< vector<float> > posNum(seg.size());
		vector< vector<float> > totalNum(seg.size());
		for(auto i=0; i<seg.size(); ++i){
			posNum[i].resize((size_t)nLabel, 0.0);
			totalNum[i].resize((size_t)nLabel, 0.0);
			for(auto pid=0; pid < width * height; ++pid){
				int segId = seg[i].at<int>(pid/width, pid%width);
				CHECK_LT(segId, nLabel);
//				pixelGroups[segId].push_back(i*kPix+pid);
				totalNum[i][segId] += 1.0f;
				if(input.data[pid] > 200)
					posNum[i][segId] += 1.0f;
			}
		}

		vector<vector<float> > segConf((size_t)kPix);
		for(auto& s: segConf)
			s.resize(seg.size(), 0.0f);

		for(auto v=0; v<seg.size(); ++v){
			for(auto pid=0; pid<width * height; ++pid){
				int label = seg[v].at<int>(pid/width, pid%width);
				CHECK_GT(totalNum[v][label], 0);
				segConf[pid][v] = posNum[v][label] / totalNum[v][label];
			}
//			Mat confVis(height, width, CV_32F, Scalar::all(0));
//			for(auto i=0; i<width*height; ++i){
//				confVis.at<float>(i/width, i%width) = segConf[i][v];
//			}
//			imshow("segConf", confVis);
//			waitKey(0);
		}

		//for each positive pixels compute average number of positive samples in all segments it belongs
		Mat conf(height, width, CV_32F, Scalar::all(0));
		float* pConf = (float *)conf.data;
		const size_t kth = seg.size() / 2;
		for(auto pid=0; pid<height * width; ++pid){
			if(input.data[pid] > 200){
				nth_element(segConf[pid].begin(), segConf[pid].begin()+kth, segConf[pid].end());
				pConf[pid] = segConf[pid][kth];
			}
		}
		const float thres = 0.2;
		for(auto i=0; i<kPix; ++i){
			if(pConf[i] > thres)
				input.data[i] = 255;
			else
				input.data[i] = 0;
		}
		imshow("filtered", input);
		waitKey(0);
	}

	//video_segments:
	void importVideoSegmentation(const std::string& path, std::vector<cv::Mat>& video_segments){
		segmentation::SegmentationReader segment_reader(path);
		const float level = 0.4;
		CHECK(segment_reader.OpenFileAndReadHeaders());
		vector<int> segment_headers = segment_reader.GetHeaderFlags();
		segmentation::Hierarchy hierarchy;
		const int kFrames = segment_reader.NumFrames();

		printf("kFrame: %d\n", kFrames);
		segmentation::Hierarchy hierachy;
		int absLevel = -1;

		for(auto f=0; f<kFrames; ++f){
			segment_reader.SeekToFrame(f);
			segmentation::SegmentationDesc cursegment;
			segment_reader.ReadNextFrame(&cursegment);

			if(cursegment.hierarchy_size() > 0){
				hierarchy.Clear();
				hierarchy.MergeFrom(cursegment.hierarchy());
				absLevel = level * (float)hierarchy.size();
			}
			const int frame_width = cursegment.frame_width();
			const int frame_height = cursegment.frame_height();
			cv::Mat segId(frame_height, frame_width, CV_32S, Scalar::all(0));
			segmentation::SegmentationDescToIdImage(absLevel, cursegment, &hierarchy, &segId);
			video_segments.push_back(segId);
		}

	}
}//namespace dynamic_stereo

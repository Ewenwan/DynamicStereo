//
// Created by yanhang on 4/10/16.
//

#include "dynamicsegment.h"
#include "../base/utility.h"
#include "../GeometryModule/dynamic_utility.h"
#include "external/MRF2.2/GCoptimization.h"
#include "../VideoSegmentation/videosegmentation.h"

using namespace std;
using namespace cv;
using namespace Eigen;

namespace dynamic_stereo{

	void computeFrequencyConfidence(const std::vector<cv::Mat> &warppedImg, Depth &result){
		CHECK(!warppedImg.empty());
		const int width = warppedImg[0].cols;
		const int height = warppedImg[0].rows;
		result.initialize(width, height, 0.0);
		const double alpha = 2, beta = 2.5;
		const int min_frq = 5;
		const int tx = -1, ty= -1;

        const int N = (int)warppedImg.size();

		vector<Mat> smoothed(warppedImg.size());
		for(auto v=0; v<warppedImg.size(); ++v){
			cv::blur(warppedImg[v], smoothed[v], cv::Size(3,3));
		}

		//build intensity map. Pixel with low intensities should not be considered flashing
		Mat intensity_map(height, width, CV_8UC1, Scalar::all(0));
		{
			const size_t kth = 0.8 * N;
			vector<vector<uchar> > intensity_array(width * height);
			for(auto& ia: intensity_array){
				ia.resize(N, (uchar)0);
			}

			for(auto v=0; v<N; ++v){
				Mat gray;
				cvtColor(smoothed[v], gray, CV_BGR2GRAY);
				for(auto i=0; i < width * height; ++i){
					intensity_array[i][v] = gray.data[i];
				}
			}

			for(auto i=0; i<width * height; ++i){
				std::nth_element(intensity_array[i].begin(), intensity_array[i].begin() + kth, intensity_array[i].end());
				intensity_map.data[i] = intensity_array[i][kth];
			}
		}

        //Divide the visible range to kInt intervals, exhaustively search for all ranges
        const int kInt = 10;
        const int minInt = kInt / 2;
        const int interval = N / kInt;
		const uchar intensity_threshold = static_cast<uchar>(255.0*0.5);

#pragma omp parallel for
		for(auto y=0; y<height; ++y){
			for(auto x=0; x<width; ++x){
				if(intensity_map.at<uchar>(y,x) < intensity_threshold){
					result(x,y) = 0;
					continue;
				}
				Mat colorArray = Mat(3, N, CV_32FC1, Scalar::all(0));
				float* pArray = (float*)colorArray.data;
				for(auto v=0; v<N; ++v) {
					Vec3b pixv = smoothed[v].at<Vec3b>(y, x);
					pArray[v] = (float) pixv[0];
					pArray[N + v] = (float) pixv[1];
					pArray[2 * N + v] = (float) pixv[2];
				}

                Mat sumMat = Mat(3, N+1, CV_32FC1, Scalar::all(0));
                for(auto i=1; i<sumMat.cols; ++i)
                    sumMat.col(i) = sumMat.col(i-1) + colorArray.col(i-1);

                //start searching
                vector<double> frqConfs;
                for(auto sid =0; sid < minInt * interval; sid += interval){
                    for(auto eid = sid + minInt * interval; eid < N; eid += interval){
                        Mat curArray;
                        colorArray.colRange(sid, eid+1).copyTo(curArray);
                        Mat meanColor = (sumMat.col(eid+1) - sumMat.col(sid)) / (float)(eid-sid+1);
                        for(auto i=0; i<curArray.cols; ++i)
                            curArray.col(i) -= meanColor;
                        double curConf = utility::getFrequencyScore(curArray, min_frq);
                        frqConfs.push_back(curConf);
                    }
                }
                double frqConf = *std::max_element(frqConfs.begin(), frqConfs.end());
                result(x,y) = 1 / (1 + std::exp(-1*alpha*(frqConf - beta)));
			}
		}

	}


	void segmentFlashy(const FileIO& file_io, const int anchor,
	                   const std::vector<cv::Mat> &input, cv::Mat &result){
		char buffer[1024] = {};

		const int width = input[0].cols;
		const int height = input[0].rows;

		Mat preSeg(height, width, CV_8UC1, Scalar::all(0));
		//repetative pattern
		printf("Computing frequency confidence...\n");
		Depth frequency;
		computeFrequencyConfidence(input, frequency);
		printf("Done\n");
        sprintf(buffer, "%s/temp/conf_frquency%05d.jpg", file_io.getDirectory().c_str(), anchor);
        frequency.saveImage(string(buffer), 255);

		double freThreshold = 0.5;

		for(auto i=0; i<width * height; ++i){
			if(frequency[i] > freThreshold)
				preSeg.data[i] = (uchar)255;
		}
		const int rh = 5;
		cv::dilate(preSeg,preSeg,cv::getStructuringElement(MORPH_ELLIPSE,cv::Size(rh,rh)));
		cv::erode(preSeg,preSeg,cv::getStructuringElement(MORPH_ELLIPSE,cv::Size(rh,rh)));

		sprintf(buffer, "%s/temp/segment_flashy%05d.jpg", file_io.getDirectory().c_str(), anchor);
		imwrite(buffer, preSeg);
        //result = video_segment::localRefinement(input, preSeg);
		result = video_segment::localRefinement(input, preSeg);

	}



//	void DynamicSegment::solveMRF(const std::vector<double> &unary,
//								  const std::vector<double>& vCue,
//								  const std::vector<double>& hCue,
//								  const cv::Mat& img, const double weight_smooth,
//								  cv::Mat& result) const {
//		const int width = img.cols;
//		const int height = img.rows;
//
//		double MRF_smooth[] = {0,weight_smooth,weight_smooth,0};
//
//		DataCost *dataCost = new DataCost(const_cast<double*>(unary.data()));
//		SmoothnessCost *smoothnessCost = new SmoothnessCost(MRF_smooth, const_cast<double*>(hCue.data()), const_cast<double*>(vCue.data()));
//		EnergyFunction* energy_function = new EnergyFunction(dataCost, smoothnessCost);
//
//		Expansion mrf(width,height,2,energy_function);
//		mrf.initialize();
//		mrf.clearAnswer();
//		for(auto i=0; i<width*height; ++i)
//			mrf.setLabel(i,0);
//		double initDataE = mrf.dataEnergy();
//		double initSmoothE = mrf.smoothnessEnergy();
//		float mrf_time;
//		mrf.optimize(10, mrf_time);
//		printf("Inital energy: (%.3f,%.3f,%.3f), final energy: (%.3f,%.3f,%.3f), time:%.2fs\n", initDataE, initSmoothE, initDataE+initSmoothE,
//		       mrf.dataEnergy(), mrf.smoothnessEnergy(), mrf.dataEnergy()+mrf.smoothnessEnergy(), mrf_time);
//
//		result = Mat(height, width, CV_8UC1);
//		uchar* pResult = result.data;
//		for(auto i=0; i<width*height; ++i){
//			if(mrf.getLabel(i) > 0)
//				pResult[i] = (uchar)255;
//			else
//				pResult[i] = (uchar)0;
//		}
//		delete dataCost;
//		delete smoothnessCost;
//		delete energy_function;
//
//	}

}//namespace dynamic_stereo

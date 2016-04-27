//
// Created by yanhang on 2/24/16.
//

#include "dynamicstereo.h"
#include <gflags/gflags.h>
#include <stlplus3/file_system.hpp>
#include "dynamicwarpping.h"
#include "dynamicsegment.h"

using namespace std;
using namespace cv;
using namespace Eigen;
using namespace dynamic_stereo;

DEFINE_int32(testFrame, 60, "anchor frame");
DEFINE_int32(tWindow, 80, "tWindow");
DEFINE_int32(tWindowStereo, 30, "tWindowStereo");
DEFINE_int32(downsample, 2, "downsample ratio");
DEFINE_int32(resolution, 256, "disparity resolution");
DEFINE_int32(stereo_interval, 10, "interval for stereo");
DEFINE_double(weight_smooth, 0.1, "smoothness weight for stereo");
DEFINE_double(min_disp, -1, "minimum disparity");
DEFINE_double(max_disp, -1, "maximum disparity");

int main(int argc, char **argv) {
	if (argc < 2) {
		cerr << "Usage: DynamicStereo <path-to-data>" << endl;
		return 1;
	}

	google::InitGoogleLogging(argv[0]);
	google::ParseCommandLineFlags(&argc, &argv, true);

	FileIO file_io(argv[1]);
	CHECK_GT(file_io.getTotalNum(), 0);
	char buffer[1024] = {};

	if (!stlplus::folder_exists(file_io.getDirectory() + "/temp"))
		stlplus::folder_create(file_io.getDirectory() + "/temp");
	if (!stlplus::folder_exists(file_io.getDirectory() + "/midres"))
		stlplus::folder_create(file_io.getDirectory() + "/midres");

	vector<Depth> depths;
	vector<int> depthInd;
	vector<Mat> depthMask;

	Mat refImage = imread(file_io.getImage(FLAGS_testFrame));
	CHECK(refImage.data);
	const int width = refImage.cols;
	const int height = refImage.rows;

	//segnet mask for reference frame
	sprintf(buffer, "%s/segnet/seg%05d.png", file_io.getDirectory().c_str(), FLAGS_testFrame);
	Mat segMaskImg = imread(buffer);
	CHECK(segMaskImg.data) << buffer;
	cv::resize(segMaskImg, segMaskImg, cv::Size(width, height), 0,0, INTER_NEAREST);
	//vector<Vec3b> validColor{Vec3b(0,0,128), Vec3b(128,192,192), Vec3b(128,128,192), Vec3b(128,128,128), Vec3b(0,128,128)};
	vector<Vec3b> invalidColor{Vec3b(128,0,64), Vec3b(128,64,128), Vec3b(0,64,64), Vec3b(222,40,60)};
	Mat segMask(height, width, CV_8UC1, Scalar(255));
	for(auto y=0; y<height; ++y){
		for(auto x=0; x<width; ++x){
			Vec3b pix = segMaskImg.at<Vec3b>(y,x);
			if(std::find(invalidColor.begin(), invalidColor.end(), pix) != invalidColor.end())
				segMask.at<uchar>(y,x) = 0;
		}
	}

	int refId;
	//run stereo
	for (auto tf = FLAGS_testFrame - FLAGS_tWindow/2;
		 tf <= FLAGS_testFrame + FLAGS_tWindow/2; tf += FLAGS_stereo_interval) {
		if(tf == FLAGS_testFrame) {
			DynamicStereo stereo(file_io, tf, FLAGS_tWindow, FLAGS_tWindowStereo, FLAGS_downsample,
								 FLAGS_weight_smooth,
								 FLAGS_resolution, FLAGS_min_disp, FLAGS_max_disp);


			{
//			    //test SfM
//				const int tf1 = FLAGS_testFrame;
//				Mat imgRef = imread(file_io.getImage(tf1));
//			//In original scale
				Vector2d pt(742, 278);
				stereo.dbtx = pt[0];
				stereo.dbty = pt[1];
//			//Vector2d pt(794, 294);
//			//Vector2d pt(1077, 257);
//				sprintf(buffer, "%s/temp/epipolar%05d_ref.jpg", file_io.getDirectory().c_str(), tf1);
//				cv::circle(imgRef, cv::Point(pt[0], pt[1]), 2, cv::Scalar(0, 0, 255), 2);
//				imwrite(buffer, imgRef);
//				for (auto tf2 = stereo.getOffset(); tf2 < stereo.getOffset() + stereo.gettWindow(); ++tf2) {
//					Mat imgL, imgR;
//					utility::verifyEpipolarGeometry(file_io, stereo.getSfMModel(), tf1, tf2, pt, imgL, imgR);
//					sprintf(buffer, "%s/temp/epipolar%05dto%05d.jpg", file_io.getDirectory().c_str(), tf1, tf2);
//					imwrite(buffer, imgR);
//				}
			}

			Depth curdepth;
			Mat curDepthMask;
			printf("Running stereo for frame %d\n", tf);
			stereo.runStereo(curdepth, curDepthMask);
			depths.push_back(curdepth);
			depthInd.push_back(tf);
			depthMask.push_back(curDepthMask);

			refId = (int)depths.size() - 1;
		} else{
			depths.push_back(Depth());
			depthInd.push_back(tf);
			depthMask.push_back(Mat());
		}
	}
	//warpping
	Mat refDepthMask;
	cv::resize(depthMask[refId], refDepthMask, cv::Size(width, height), 0, 0, INTER_NEAREST);
	sprintf(buffer, "%s/temp/depthMask%05d.jpg", file_io.getDirectory().c_str(), FLAGS_testFrame);
	imwrite(buffer, refDepthMask);

	Mat warpMask = segMask.clone();
	CHECK_EQ(warpMask.cols, refDepthMask.cols);
	CHECK_EQ(warpMask.rows, refDepthMask.rows);

//	for(auto y=0; y<height; ++y){
//		for(auto x=0; x<width; ++x){
//			if(refDepthMask.at<uchar>(y,x) < 200)
//				warpMask.at<uchar>(y,x) = 0;
//		}
//	}

	shared_ptr<DynamicWarpping> warpping(new DynamicWarpping(file_io, FLAGS_testFrame, FLAGS_tWindow, FLAGS_downsample, FLAGS_resolution, depths, depthInd));
	const int warpping_offset = warpping->getOffset();
	vector<Mat> warpped;
	warpping->warpToAnchor(warpMask, warpped, false);

	vector<Mat> prewarp;
	warpping->preWarping(warpMask, prewarp);

	warpping.reset();

//	vector<Mat> warped_filtered;
//	utility::temporalMedianFilter(warpped, warped_filtered, 2);

	for(auto i=0; i<prewarp.size(); ++i){
//		sprintf(buffer, "%s/temp/warpedb%05d_%05d.jpg", file_io.getDirectory().c_str(), FLAGS_testFrame, i+warpping_offset);
//		imwrite(buffer, warpped[i]);
		sprintf(buffer, "%s/temp/prewarpb%05d_%05d.jpg", file_io.getDirectory().c_str(), FLAGS_testFrame, i+warpping_offset);
		imwrite(buffer, prewarp[i]);
	}

//
	//segmentation
	printf("Segmenting...\n");
	shared_ptr<DynamicSegment> segment(new DynamicSegment(file_io, FLAGS_testFrame, FLAGS_tWindow, FLAGS_downsample, depths, depthInd));
	Mat seg_result_small;
//	segment.getGeometryConfidence(geoConf);
//	sprintf(buffer, "%s/temp/geoConf%05d.jpg", file_io.getDirectory().c_str(), FLAGS_testFrame);
//	geoConf.saveImage(string(buffer), 5.0);
//	segment->segment(warpped, seg_result);
	segment->segment(prewarp, seg_result_small);
	segment.reset();
	Mat seg_result;
	cv::resize(seg_result_small, seg_result, cv::Size(width, height), 0, 0, INTER_NEAREST);
	Mat seg_overlay(height, width, CV_8UC3, Scalar(0,0,0));
	for(auto y=0; y<height; ++y){
		for(auto x=0; x<width; ++x){
			if(seg_result.at<uchar>(y,x) > 200)
				seg_overlay.at<Vec3b>(y,x) = refImage.at<Vec3b>(y,x) * 0.4 + Vec3b(255,0,0) * 0.6;
			else
				seg_overlay.at<Vec3b>(y,x) = refImage.at<Vec3b>(y,x) * 0.4 + Vec3b(0,0,255) * 0.6;
		}
	}
	sprintf(buffer, "%s/temp/segment%05d.jpg", file_io.getDirectory().c_str(), FLAGS_testFrame);
	imwrite(buffer, seg_overlay);

	for(auto i=0; i<warpped.size(); ++i){
		for(auto y=0; y<height; ++y){
			for(auto x=0; x<width; ++x){
				if(seg_result.at<uchar>(y,x) < 200){
					warpped[i].at<Vec3b>(y,x) = refImage.at<Vec3b>(y,x);
				}
			}
		}
	}

	for(auto i=0; i<warpped.size(); ++i){
		sprintf(buffer, "%s/temp/warpedb%05d_%05d.jpg", file_io.getDirectory().c_str(), FLAGS_testFrame, i+warpping_offset);
		imwrite(buffer, warpped[i]);
	}
	return 0;
}

//
// Created by yanhang on 4/29/16.
//

#include <unordered_set>
#include "dynamicsegment.h"
#include "../base/thread_guard.h"
#include "../VisualWord/visualword.h"

using namespace std;
using namespace cv;
using namespace Eigen;

namespace dynamic_stereo{

	void segmentDisplay(const FileIO& file_io, const int anchor,
	                    const std::vector<cv::Mat> &input, const cv::Mat& inputMask,
	                    const string& classifierPath, const string& codebookPath, cv::Mat& result){
		CHECK(!input.empty());
		char buffer[1024] = {};
		const int width = input[0].cols;
		const int height = input[0].rows;

		//display region
		sprintf(buffer, "%s/midres/classification%05d.png", file_io.getDirectory().c_str(), anchor);
		Mat preSeg = imread(buffer, false);

		if(!preSeg.data) {
            const vector<float> levelList{10.0, 20.0, 30.0};
            cv::Ptr<ml::StatModel> classifier;
            Mat codebook;
            VisualWord::VisualWordOption vw_option;
            cv::FileStorage codebookIn(codebookPath, FileStorage::READ);
            CHECK(codebookIn.isOpened()) << "Can not open code book: " << codebookPath;
            codebookIn["codebook"] >> codebook;

            int pixeldesc = (int)codebookIn["pixeldesc"];
            int classifiertype = (int)codebookIn["classifiertype"];
            printf("pixeldesc: %d, classifiertype: %d\n", pixeldesc, classifiertype);
            vw_option.pixDesc = (VisualWord::PixelDescriptor) pixeldesc;
            vw_option.classifierType = (VisualWord::ClassifierType) classifiertype;
            if(classifiertype == VisualWord::RANDOM_FOREST) {
                classifier = ml::RTrees::load<ml::RTrees>(classifierPath);
                CHECK(classifier.get());
                cout << "Tree depth: " << classifier.dynamicCast<ml::RTrees>()->getMaxDepth() << endl;
            }
            else if(classifiertype == VisualWord::BOOSTED_TREE)
                classifier = ml::Boost::load<ml::Boost>(classifierPath);
            else if(classifiertype == VisualWord::SVM)
                classifier = ml::SVM::load(classifierPath);
            CHECK(classifier.get()) << "Can not open classifier: " << classifierPath;


            //load or compute segmentation
            vector<Mat> segments;
            sprintf(buffer, "%s/midres/segment%05d.yml", file_io.getDirectory().c_str(), anchor);
            cv::FileStorage segmentIn(buffer, FileStorage::READ);
            bool run_segmentation = true;
            if(segmentIn.isOpened()){
                Mat levelMat;
                segmentIn["levelList"] >> levelMat;
                if(levelMat.rows!= levelList.size()){
                    run_segmentation = true;
                }else{
                    for(auto i=0; i<levelList.size(); ++i){
                        if(levelMat.at<float>(i,0) != levelList[i]){
                            run_segmentation = true;
                            break;
                        }
                    }
                }
                segments.resize(levelList.size());
                for(int i=0; i<levelList.size(); ++i){
                    segmentIn["level"+std::to_string(i)] >> segments[i];
                }
                run_segmentation = false;
            }
            if(run_segmentation) {
                VisualWord::detectVideo(input, classifier, codebook, levelList, preSeg, vw_option, cv::noArray(), segments);
                CHECK_EQ(segments.size(), levelList.size());
                cv::FileStorage segmentOut(buffer, FileStorage::WRITE);
                CHECK(segmentOut.isOpened());
                Mat levelMat(levelList.size(),1,CV_32FC1);
                for(auto i=0; i<levelList.size(); ++i)
                    levelMat.at<float>(i,0) = levelList[i];
                segmentOut << "levelList" << levelMat;
                for(int i=0; i<levelList.size(); ++i)
                    segmentOut << "level"+std::to_string(i) << segments[i];
            }else{
                VisualWord::detectVideo(input, classifier, codebook, levelList, preSeg, vw_option, segments, cv::noArray());
            }

            //dump out segmentation result
            for(auto i=0; i<segments.size(); ++i){
                Mat segVis = video_segment::visualizeSegmentation(segments[i]);
                sprintf(buffer, "%s/temp/videosegment%05d_%.1f.png", file_io.getDirectory().c_str(), anchor, levelList[i]);
                imwrite(buffer, segVis);
            }

            sprintf(buffer, "%s/midres/classification%05d.png", file_io.getDirectory().c_str(), anchor);
            imwrite(buffer, preSeg);
		}

        sprintf(buffer, "%s/temp/segment_display.jpg", file_io.getDirectory().c_str());
        imwrite(buffer, preSeg);
        result = video_segment::localRefinement(input, preSeg);
	}

	void groupPixel(const cv::Mat& labels, std::vector<std::vector<Eigen::Vector2d> >& segments){
		CHECK_NOTNULL(labels.data);
		CHECK_EQ(labels.type(), CV_32S);
		double minl, maxl;
		cv::minMaxLoc(labels, &minl, &maxl);
		CHECK_LT(minl, std::numeric_limits<double>::epsilon());
		const int nLabel = (int)maxl;
		segments.clear();
		segments.resize((size_t)nLabel);
		for(auto y=0; y<labels.rows; ++y){
			for(auto x=0; x<labels.cols; ++x){
				int l = labels.at<int>(y,x);
				if(l > 0)
					segments[l-1].push_back(Vector2d(x,y));
			}
		}
	}



}//namespace dynamic_stereo

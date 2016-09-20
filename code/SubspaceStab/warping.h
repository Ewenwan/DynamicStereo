//
// Created by yanhang on 4/21/16.
//

#ifndef SUBSPACESTAB_WARPPING_H
#define SUBSPACESTAB_WARPPING_H

#include <opencv2/opencv.hpp>
#include <Eigen/Eigen>
#include <iostream>
#include <glog/logging.h>

namespace substab {

	class GridWarpping {
	public:
		GridWarpping(const int w, const int h, const int gw = 64, const int gh = 36);


		void getGridIndAndWeight(const Eigen::Vector2d &pt, Eigen::Vector4i &ind, Eigen::Vector4d &w) const;

		void warpImageCloseForm(const cv::Mat& input, cv::Mat& output,
                                const std::vector<Eigen::Vector2d>& pts1, const std::vector<Eigen::Vector2d>& pts2,
                                const bool fixBoundary = false, const int id = 0);

		void computeSimilarityWeight(const cv::Mat& input, std::vector<double>& saliency) const;

		inline int gridInd(int x, int y)const{
			CHECK_LE(x, gridW);
			CHECK_LE(y, gridH);
			return y*(gridW+1)+x;
		}
		inline double getBlockW() const{
			return blockW;
		}
		inline double getBlockH() const{
			return blockH;
		}

		inline Eigen::Vector2d warpPoint(const Eigen::Vector2d& pt){
			Eigen::Vector4i ind;
			Eigen::Vector4d w;
			getGridIndAndWeight(pt, ind, w);
			Eigen::Vector2d res = gridLoc[ind[0]] * w[0] + gridLoc[ind[1]] * w[1] + gridLoc[ind[2]] * w[2] + gridLoc[ind[3]] * w[3];
			return res;
		}

		void visualizeGrid(const std::vector<Eigen::Vector2d>& grid, cv::Mat& img) const;
	private:
		std::vector<Eigen::Vector2d> gridLoc;
		std::vector<Eigen::Vector2d> warpedLoc;

		int width;
		int height;
		int gridW;
		int gridH;
		double blockW;
		double blockH;
	};

}//namespace substab

#endif //SUBSPACESTAB_WARPPING_H

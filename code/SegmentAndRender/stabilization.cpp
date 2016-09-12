//
// Created by yanhang on 9/11/16.
//

#include "stabilization.h"
#include "stab_energy.h"

using namespace std;
using namespace cv;
using namespace Eigen;

namespace dynamic_stereo {
    WarpGrid::WarpGrid(const int width, const int height, const int gridW_, const int gridH_)
            : gridW(gridW_), gridH(gridH_) {
        blockW = (double) width / (double) gridW;
        blockH = (double) height / (double) gridH;
        gridLoc.resize((size_t) (gridW + 1) * (gridH + 1));
        for (auto x = 0; x <= gridW; ++x) {
            for (auto y = 0; y <= gridH; ++y) {
                gridLoc[y * (gridW + 1) + x] = Vector2d(blockW * x, blockH * y);
                if (x == gridW)
                    gridLoc[y * (gridW + 1) + x][0] -= 1.1;
                if (y == gridH)
                    gridLoc[y * (gridW + 1) + x][1] -= 1.0;
            }
        }
    }

    void getGridIndAndWeight(const WarpGrid& grid, const Eigen::Vector2d& pt, Eigen::Vector4i& ind, Eigen::Vector4d& w){
        const double& blockW = grid.blockW;
        const double& blockH = grid.blockH;
        const int& gridW = grid.gridW;
        const int& gridH = grid.gridH;
        const vector<Vector2d>& gridLoc = grid.gridLoc;

        int x = (int) floor(pt[0] / blockW);
        int y = (int) floor(pt[1] / blockH);
        CHECK_LE(x, gridW);
        CHECK_LE(y, gridH);

        ind = Vector4i(y * (gridW + 1) + x, y * (gridW + 1) + x + 1, (y + 1) * (gridW + 1) + x + 1,
                       (y + 1) * (gridW + 1) + x);

        const double &xd = pt[0];
        const double &yd = pt[1];
        const double& xl = gridLoc[ind[0]][0];
        const double& xh = gridLoc[ind[2]][0];
        const double& yl = gridLoc[ind[0]][1];
        const double& yh = gridLoc[ind[2]][1];

        w[0] = (xh - xd) * (yh - yd);
        w[1] = (xd - xl) * (yh - yd);
        w[2] = (xd - xl) * (yd - yl);
        w[3] = (xh - xd) * (yd - yl);

        double s = w[0] + w[1] + w[2] + w[3];
        CHECK_GT(s, 0) << pt[0] << ' '<< pt[1];
        w = w / s;

        Vector2d pt2 =
                gridLoc[ind[0]] * w[0] + gridLoc[ind[1]] * w[1] + gridLoc[ind[2]] * w[2] + gridLoc[ind[3]] * w[3];
        double error = (pt2 - pt).norm();
        CHECK_LT(error, 0.0001) << pt[0] << ' ' << pt[1] << ' ' << pt2[0] << ' ' << pt2[1];
    }

    void warpbyGrid(const cv::Mat& input, cv::Mat& output, const WarpGrid& grid){
        output = input.clone();
#pragma omp parallel for
        for(auto y=0; y<output.rows; ++y){
            for(auto x=0; x<output.cols; ++y){
                Vector4d biW;
                Vector4i biInd;
                getGridIndAndWeight(grid, Vector2d(x,y), biInd, biW);
                Vector2d pt(0,0);
                for(auto i=0; i<4; ++i){
                    pt[0] += grid.gridLoc[biInd[i]][0] * biW[i];
                    pt[1] += grid.gridLoc[biInd[i]][1] * biW[i];
                }
                if(pt[0] < 0 || pt[1] < 0 || pt[0] > input.cols - 1 || pt[1] > input.rows - 1)
                    continue;
                Vector3d pixO = interpolation_util::bilinear<uchar,3>(input.data, input.cols, input.rows, pt);
                output.at<Vec3b>(y,x) = Vec3b((uchar)pixO[0], (uchar)pixO[1], (uchar)pixO[2]);
            }
        }
    }

    void gridStabilization(const std::vector<cv::Mat>& input, std::vector<cv::Mat>& output, const double ws, const int step) {
        CHECK(!input.empty());
        const int width = input[0].cols;
        const int height = input[0].rows;
        const int gridW = 64, gridH = 64;
        WarpGrid grid(width, height, gridW, gridH);

        //aggregated warping field
        vector<vector<Vector2d> > warpingField(input.size());
        for (auto &wf: warpingField)
            wf.resize(grid.gridLoc.size(), Vector2d(0,0));

        //apply near-hard boundary constraint
        const double weight_boundary = 100;
        //variables for optimization: offset from original grid.
        //Note, defination different from old gridWarpping
        //from frame v to frame v-1

        ceres::Solver::Options ceres_option;
        ceres_option.max_num_iterations = 1000;
        ceres_option.linear_solver_type = ceres::SPARSE_NORMAL_CHOLESKY;

        //Pay attention to the directioin of warping:
        //to match frame v against v-1, we need to compute warping
        //field from v-1 to v
        for (auto v = 1; v < input.size(); ++v) {
            printf("----------------\n");
            printf("Frame %d -> %d\n", v-1, v);
            vector<vector<double> > vars(grid.gridLoc.size());
            for (auto &v: vars)
                v.resize(2, 0.0);
            //create problem
            ceres::Problem problem;

            //appearance term
            for (auto y = 0; y < height; y += step) {
                for (auto x = 0; x < width; x += step) {
                    Vec3b pix = input[v].at<Vec3b>(y, x);
                    Vector3d tgtColor((double) pix[0], (double) pix[1], (double) pix[2]);
                    Vector4i biInd;
                    Vector4d biW;
                    getGridIndAndWeight(grid, Vector2d(x, y), biInd, biW);
                    ceres::CostFunction *cost_data =
                            new ceres::NumericDiffCostFunction<WarpFunctorDense, ceres::CENTRAL, 1, 2, 2, 2, 2>(
                                    new WarpFunctorDense(input[v-1], tgtColor, biInd, biW, grid.gridLoc, 1.0)
                            );
                    problem.AddResidualBlock(cost_data, NULL, vars[biInd[0]].data(), vars[biInd[1]].data(),
                                             vars[biInd[2]].data(), vars[biInd[3]].data());

                }
            }
            //boundary constraint
            ceres::CostFunction *cost_boundary = new ceres::AutoDiffCostFunction<WarpFunctorFix, 1, 2>(
                    new WarpFunctorFix(nullptr, weight_boundary)
            );
            for (auto x = 0; x <= gridW; ++x) {
                problem.AddResidualBlock(cost_boundary, NULL, vars[x].data());
                problem.AddResidualBlock(cost_boundary, NULL, vars[gridH * (gridW + 1) + x].data());
            }
            for (auto y = 0; y <= gridH; ++y) {
                problem.AddResidualBlock(cost_boundary, NULL, vars[y * (gridW + 1)].data());
                problem.AddResidualBlock(cost_boundary, NULL, vars[y * (gridW + 1) + gridW].data());
            }

            //similarity term
            for(auto y=1; y<=gridH; ++y){
                for(auto x=1; x<=gridW; ++x){
                    int gid1, gid2, gid3;
                    gid1 = y * (gridW + 1) + x;
                    gid2 = (y - 1) * (gridW + 1) + x;
                    gid3 = y * (gridW + 1) + x + 1;
                    problem.AddResidualBlock(
                            new ceres::AutoDiffCostFunction<WarpFunctorSimilarity, 1, 2, 2, 2>(
                                    new WarpFunctorSimilarity(grid.gridLoc[gid1], grid.gridLoc[gid2], grid.gridLoc[gid3], ws)),
                            NULL,
                            vars[gid1].data(), vars[gid2].data(), vars[gid3].data()
                    );
                    gid2 = (y - 1) * (gridW + 1) + x + 1;
                    problem.AddResidualBlock(
                            new ceres::AutoDiffCostFunction<WarpFunctorSimilarity, 1, 2, 2, 2>(
                                    new WarpFunctorSimilarity(grid.gridLoc[gid1], grid.gridLoc[gid2], grid.gridLoc[gid3], ws)),
                            NULL,
                            vars[gid1].data(), vars[gid2].data(), vars[gid3].data());
                }
            }

            //solve
            ceres::Solver::Summary summary;
            float start_t = cv::getTickCount();
            ceres::Solve(ceres_option, &problem, &summary);
            cout << summary.BriefReport() << endl;

            //aggregate result
            for(auto i=0; i<vars.size(); ++i){
                warpingField[v][i][0] = warpingField[v-1][i][0] + vars[i][0];
                warpingField[v][i][1] = warpingField[v-1][i][1] + vars[i][1];
            }

            printf("Warping...\n");
            WarpGrid warpGrid(width, height, gridW, gridH);
            CHECK_EQ(warpGrid.gridLoc.size(), warpingField.size());
            for(auto i=0; i<warpingField.size(); ++i){
                warpGrid.gridLoc[i] += warpingField[v][i];
            }
            warpbyGrid(input[v], output[v], warpGrid);
        }
    }


    void stablizeSegments(const std::vector<cv::Mat>& input, std::vector<cv::Mat>& output,
                          const std::vector<std::vector<Eigen::Vector2i> >& segments, const double ws){
        CHECK(!input.empty());
        output.resize(input.size());
        for(auto i=0; i<output.size(); ++i)
            output[i] = input[i].clone();

        const int width = input[0].cols;
        const int height = input[0].rows;

        const int margin = 5;
        for(const auto& segment: segments){
            cv::Point2i tl(width+1, height+1);
            cv::Point2i br(-1, -1);
            for(const auto& pt: segment){
                tl.x = std::min(pt[0], tl.x);
                tl.y = std::min(pt[1], tl.y);
                br.x = std::min(pt[0], br.x);
                br.y = std::min(pt[1], br.y);
            }
            tl.x = std::max(tl.x - margin, 0);
            tl.y = std::max(tl.y - margin, 0);
            br.x = std::min(br.x + margin, width-1);
            br.y = std::min(br.y + margin, height-1);
            cv::Rect roi(tl, br);
            vector<Mat> warp_input(input.size()), warp_output;
            for(auto v=0; v<input.size(); ++v)
                warp_input[v] = input[v](roi);
            gridStabilization(warp_input, warp_output, ws);
            for(auto v=0; v<output.size(); ++v)
                warp_output[v].copyTo(output[v](roi));
        }
    }
}//namespace dynamic_stereo
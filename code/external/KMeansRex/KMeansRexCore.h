/* KMeansRexCore.h
   Provides header file function declarations for Matlab MEX compilation.
*/

#include "Eigen/Dense"
#include <iostream>
using namespace Eigen;
using namespace std;

/*  DEFINE Custom Type Names to make code more readable
    ExtMat :  2-dim matrix/array externally defined (e.g. in Matlab or Python)
*/

namespace KMeansRex {
    typedef Map<ArrayXXd> ExtMat;
    typedef ArrayXXd Mat;
    typedef ArrayXd Vec;

    void set_seed(int seed);

    void select_without_replacement(int N, int K, Vec &chosenIDs);

    int discrete_rand(Vec &p);

    void init_Mu(ExtMat &X, ExtMat &Mu, char *initname);

    void run_lloyd(ExtMat &X, ExtMat &Mu, ExtMat &Z, int Niter);

    void RunKMeans(double *X_IN,  int N,  int D, int K, int Niter, \
	           int seed, char* initname, double *Mu_OUT, double *Z_OUT);
    void SampleRowsPlusPlus(double *X_IN,  int N,  int D, int K, int seed, double *Mu_OUT);


}//namespace KMeansRex
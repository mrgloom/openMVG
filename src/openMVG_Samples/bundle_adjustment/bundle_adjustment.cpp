
// Copyright (c) 2012, 2013 Pierre MOULON.

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.


// An example of a minimal, self-contained bundle adjuster using Ceres
// It refines Focal, Rotation and Translation of the cameras.

#include <cmath>
#include <cstdio>
#include <iostream>

#include "openMVG/multiview/test_data_sets.hpp"
#include "openMVG/multiview/projection.hpp"

using namespace openMVG;

#include "ceres/ceres.h"
#include "ceres/rotation.h"

// Build a Bundle Adjustment dataset.
class BAProblem {
 public:

  int num_observations()        const { return num_observations_; }
  const double* observations() const { return &observations_[0];  }
  double* mutable_cameras()          { return &parameters_[0];  }
  double* mutable_points()           { return &parameters_[0]  + 7 * num_cameras_; }

  double* mutable_camera_for_observation(int i) {
    return mutable_cameras() + camera_index_[i] * 7;
  }
  double* mutable_point_for_observation(int i) {
    return mutable_points() + point_index_[i] * 3;
  }
  int num_cameras_;
  int num_points_;
  int num_observations_;
  int num_parameters_;

  std::vector<int> point_index_;
  std::vector<int> camera_index_;
  std::vector<double> observations_; // 3D points
  std::vector<double> parameters_;   // Camera (pinhole, f,R,t)

};

// Templated pinhole camera model for used with Ceres.  The camera is
// parameterized using 7 parameters: 3 for rotation, 3 for translation, 1 for
// focal length. Principal point it is assumed be located at the image center.
struct PinholeReprojectionError {
  PinholeReprojectionError(double observed_x, double observed_y)
      : observed_x(observed_x), observed_y(observed_y) {}

  template <typename T>
  bool operator()(const T* const camera,
                  const T* const point,
                  T* residuals) const {

                    std::cout << "\n Camera: "
                    << camera[0] << " "
                    << camera[1] << " "
                    << camera[2] << " "
                    << camera[3] << " "
                    << camera[4] << " "
                    << camera[5] << " "
                    << camera[6] << "\n";
    // camera[0,1,2] are the angle-axis rotation.
    T p[3];
    ceres::AngleAxisRotatePoint(camera, point, p);

    // camera[3,4,5] are the translation.
    p[0] += camera[3];
    p[1] += camera[4];
    p[2] += camera[5];

    // Point from homogeneous to euclidean
    T xp = p[0] / p[2];
    T yp = p[1] / p[2];

    // Compute final projected point position.
    const T& focal = camera[6];
    T predicted_x = focal * xp;
    T predicted_y = focal * yp;

    std::cout << predicted_x << "\t" << observed_x << std::endl;
    std::cout << predicted_y << "\t" << observed_y << std::endl << std::endl;

    //unsigned char a = 2;
    //std::cin >> a;
    // The error is the difference between the predicted and observed position.
    residuals[0] = predicted_x - T(observed_x);
    residuals[1] = predicted_y - T(observed_y);

    return true;
  }

  double observed_x;
  double observed_y;
};

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);


  int nviews = 3;
  int npoints = 6;
  NViewDataSet d = NRealisticCamerasRing(nviews, npoints);

  // Setup a BA problem
  BAProblem ba_problem;

  // Configure the size of the problem
  ba_problem.num_cameras_ = nviews;
  ba_problem.num_points_ = npoints;
  ba_problem.num_observations_ = nviews * npoints;

  ba_problem.point_index_.reserve(ba_problem.num_observations_);
  ba_problem.camera_index_.reserve(ba_problem.num_observations_);
  ba_problem.observations_.reserve(2 * ba_problem.num_observations_);

  ba_problem.num_parameters_ = 7 * ba_problem.num_cameras_ + 3 * ba_problem.num_points_;
  ba_problem.parameters_.reserve(ba_problem.num_parameters_);

  double ppx = 500, ppy = 500;
  // Fill it with data (tracks and points coords)
  for (int i = 0; i < npoints; ++i) {
    // Collect the image of point i in each frame.
    for (int j = 0; j < nviews; ++j) {
      ba_problem.camera_index_.push_back(j);
      ba_problem.point_index_.push_back(i);
      Vec2 pt = d._x[j].col(i);
      ba_problem.observations_.push_back( pt(0) - ppx );
      ba_problem.observations_.push_back( pt(1) - ppy );
    }
  }

  // camera parameters, 3D points
  for (int j = 0; j < nviews; ++j) {
    // Rotation matrix to angle axis
    std::vector<double> angleAxis(3);
    ceres::RotationMatrixToAngleAxis((const double*)d._R[j].data(), &angleAxis[0]);
    // translation
    Vec3 t = d._t[j];
    double focal = d._K[j](0,0);
    ba_problem.parameters_.push_back(angleAxis[0]);
    ba_problem.parameters_.push_back(angleAxis[1]);
    ba_problem.parameters_.push_back(angleAxis[2]);
    ba_problem.parameters_.push_back(t[0]);
    ba_problem.parameters_.push_back(t[1]);
    ba_problem.parameters_.push_back(t[2]);
    ba_problem.parameters_.push_back(focal);
  }

  for (int i = 0; i < npoints; ++i) {
    Vec3 pt3D = d._X.col(i);
    double * ptr3D = ba_problem.mutable_points()+i*3;
    ba_problem.parameters_.push_back(pt3D[0]);
    ba_problem.parameters_.push_back(pt3D[1]);
    ba_problem.parameters_.push_back(pt3D[2]);
  }

  // Create residuals for each observation in the bundle adjustment problem. The
  // parameters for cameras and points are added automatically.
  ceres::Problem problem;
  for (int i = 0; i < ba_problem.num_observations(); ++i) {
    // Each Residual block takes a point and a camera as input and outputs a 2
    // dimensional residual. Internally, the cost function stores the observed
    // image location and compares the reprojection against the observation.
    ceres::CostFunction* cost_function =
        new ceres::AutoDiffCostFunction<PinholeReprojectionError, 2, 7, 3>(
            new PinholeReprojectionError(
                ba_problem.observations()[2 * i + 0],
                ba_problem.observations()[2 * i + 1]));

    problem.AddResidualBlock(cost_function,
                             NULL, // squared loss
                             ba_problem.mutable_camera_for_observation(i),
                             ba_problem.mutable_point_for_observation(i));
  }

  // Make Ceres automatically detect the bundle structure. Note that the
  // standard solver, SPARSE_NORMAL_CHOLESKY, also works fine but it is slower
  // for standard bundle adjustment problems.
  ceres::Solver::Options options;
  options.linear_solver_type = ceres::SPARSE_SCHUR;
  if (ceres::IsSparseLinearAlgebraLibraryTypeAvailable(ceres::SUITE_SPARSE))
    options.sparse_linear_algebra_library = ceres::SUITE_SPARSE;
  else
    if (ceres::IsSparseLinearAlgebraLibraryTypeAvailable(ceres::CX_SPARSE))
      options.sparse_linear_algebra_library = ceres::CX_SPARSE;
    else
    {
      // No sparse backend for Ceres.
      // Use dense solving
      options.linear_solver_type = ceres::DENSE_SCHUR;
    }
  options.minimizer_progress_to_stdout = true;

  ceres::Solver::Summary summary;
  ceres::Solve(options, &problem, &summary);
  std::cout << summary.FullReport() << "\n";

  return 0;
}

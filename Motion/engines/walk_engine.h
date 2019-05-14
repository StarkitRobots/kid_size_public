#pragma once

#include <map>
#include <Eigen/Dense>
#include <LegIK/LegIK.hpp>
#include "rhoban_utils/spline/poly_spline.h"
#include "robot_model/humanoid_model.h"
#include "Model/HumanoidFixedModel.hpp"

namespace rhoban
{
class WalkEngine
{
public:
  // Foot position and speed in 2.5D
  struct FootPose
  {
    FootPose();
    void operator=(const FootPose &other);

    double x, y, z;
    double yaw;
  };

  struct Foot
  {
    // Get the foot position, t is from 0 to 1, playing the footstep
    struct FootPose getPosition(double t);

    // Update splines for the foot step
    void clearSplines();
    
    // Splines
    rhoban_utils::PolySpline xSpline, ySpline, zSpline, yawSpline;

    double trunkYOffset;
  };

  // Ticks the walk engine
  WalkEngine();
  void initByModel(rhoban::HumanoidModel& model);

  // Re-compute splines
  void reset();
  void updateSplines();
  void newStep();

  // Updating feet position
  void assignModel(rhoban::HumanoidModel& model, double timeSinceLastStep);

  // Walk engine left and right feet position
  struct Foot left, right;

  // Support and flying foot
  bool isLeftSupport;
  struct Foot& supportFoot();
  struct Foot& flyingFoot();

  // Walk engine parameters
  double trunkXOffset;
  double trunkZOffset;
  double footYOffset;
  double riseGain;
  double riseDuration;
  double frequency;
  double swingGain;
  double swingPhase;
  double trunkHeight;
  double footDistance;
  double footYOffsetPerStepSizeY;
  double trunkPitch;

  // Dynamics orders
  double stepSizeX, stepSizeY, stepSizeYaw;

  // Duration of the current step
  double stepDuration;

  // This is stored for the current step to avoid having it changing during the step itself
  double _swingGain;
};
}  // namespace rhoban
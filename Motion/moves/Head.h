#pragma once

#include "Move.h"

#include "rhoban_unsorted/head_scan.h"

namespace rhoban
{
class HumanoidModel;
class CameraModel;
}  // namespace rhoban

/// PermaScan Move based on smooth trajectories
///
/// There is 2 different modes for head with the following priority
/// 1. Localize  (looking around near horizon to find features for localization)
/// 2. Scan/Track ball
class Head : public Move
{
public:
  Head();

  /// Implement Move
  virtual std::string getName() override;
  virtual void onStart() override;
  virtual void step(float elapsed) override;

  void setForceLocalize(bool value);
  void setDisabled(bool value);
  bool isDisabled();
  void setLocalizeMaxPan(double maxPan);

private:
  /// Synchronize local parameters with 'scanner'
  void updateScanners();

  void updateTimers(float elapsed);

  bool shouldTrackBall();

  /// Get the scan target in self referential according to the scanner
  Eigen::Vector3d getScanTarget(rhoban::HumanoidModel* model, const rhoban_unsorted::HeadScan& scannerUsed);

  /// Get the ball target in self referential
  Eigen::Vector3d getBallTarget(rhoban::HumanoidModel* model, const rhoban::CameraModel& camera_model);

  /// Apply the protection orders based on provided status
  void applyProtection();

  /// Default scanner, used to find the ball
  rhoban_unsorted::HeadScan scanner;
  /// Specific scanner for localization
  rhoban_unsorted::HeadScan localize_scanner;

  /// Min tilt [deg]
  double min_tilt;
  /// Max tilt [deg]
  double max_tilt;
  /// Max pan [deg]
  double max_pan;
  /// Minimal overlap between two adjacent control points [deg]
  double min_overlap;

  /// Min tilt wished for localization [deg]
  double localize_min_tilt;
  /// Max tilt wished for localization [deg]
  double localize_max_tilt;
  /// Max pan wished for localization [deg]
  double localize_max_pan;
  /// Minimal overlap between two adjacent control points in localization mode [deg]
  double localize_min_overlap;

  /// Maximal pan allowed for tracking [deg]
  double max_pan_track;
  /// Maximal tilt allowed for tracking [deg]
  double max_tilt_track;

  /// Maximal angular speed [deg/s]
  double max_speed;
  /// Maximal acceleration [deg/s^2]
  double max_acc;

  /// Duration of a default cycle in [s]
  double scan_period;
  /// Duration of a localize cycle in [s]
  double localize_scan_period;

  /// Pan sent to motors in [deg]
  double pan_deg;
  /// Tilt sent to motors in [deg]
  double tilt_deg;

  // Target point in X (robot referential)
  double target_x;
  // Target point in Y (robot referential)
  double target_y;

  // wished_pan [deg]
  double wished_pan;
  // wished_tilt [deg]
  double wished_tilt;
  // wished_dist [m]
  double wished_dist;

  /// Is the robot currently tracking the ball
  bool is_tracking;

  /// Time allowed to track before scanning
  double tracking_period;
  /// Time allowed to track before scanning when ball is near
  double near_tracking_period;
  /// Extra time allowed for the scan phase (reaching trajectory takes some time)
  double scan_extra_period;

  /// Time elapsed since tracking
  double tracking_time;
  /// Time elapsed since scanning
  double scanning_time;

  /// Look ball more frequently when it's nearby
  double near_track_dist;

  /// Do not loose ball when it's really close
  double force_track_dist;

  /// For tracking experiments, we always want to track the ball
  bool force_track;

  /// Avoid sudden changes
  double smoothing;

  /// Is the robot supposed to perform a pure localization
  bool force_localize;

  /// Is the robot supposed to keep searching for the ball
  bool force_scan_ball;

  /// When Head is disabled, it looks at a fixed position and uses only
  /// safety
  bool disabled;

  /// Head_pitch used for backward falls
  double fall_backward_pitch;

  /// Head_pitch used for forward falls
  double fall_forward_pitch;

  /// Time offset (in the future) for ball prediction [ms]
  double predicted_ball_time_offset;
};

#include "Binding/LocalisationBinding.hpp"
#include "Binding/Robocup.hpp"

#include "CameraState/CameraState.hpp"

#include "Localisation/Ball/BallStackFilter.hpp"
#include "Localisation/Field/CompassObservation.hpp"
#include "Localisation/Field/FieldObservation.hpp"
#include "Localisation/Field/GoalObservation.hpp"
#include "Localisation/Field/RobotController.hpp"
#include "Localisation/Field/TagsObservation.hpp"

#include "Utils/Drawing.hpp"
#include "Utils/Interface.h"
#include "Utils/OpencvUtils.h"

#include "scheduler/MoveScheduler.h"
#include "services/DecisionService.h"
#include "services/LocalisationService.h"
#include "services/ModelService.h"
#include "services/RefereeService.h"

#include "unistd.h"

#include <rhoban_utils/logging/logger.h>
#include <utility>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <cstdlib>

using namespace rhoban_utils;
using namespace Vision::Localisation;
using Vision::Utils::CameraState;

static rhoban_utils::Logger fieldLogger("RobocupFieldPF");

namespace Vision
{

LocalisationBinding::LocalisationBinding(MoveScheduler * scheduler_,
                                         Robocup * vision_binding_)
  : vision_binding(vision_binding_),
    scheduler(scheduler_),
    nb_particles_ff(3000),
    toGoalQ(-1), robotQ(-1),
    enableFieldFilter(true),
    isGoalKeeper(false),
    consistencyEnabled(true),
    consistencyScore(1),
    consistencyStepCost(0.005),
    consistencyBadObsCost(0.05),
    consistencyGoodObsGain(0.08),
    consistencyResetInterval(90),
    consistencyMaxNoise(3.0),
    cs(new CameraState(scheduler_)),
    period(3.0),
    maxNoiseBoost(10.0),
    noiseBoostDuration(20),
    isUsingVisualCompass(false),
    nbVCObs(0),
    minVCObs(1),
    isForbidden(false),
    _runThread(nullptr)
{
  scheduler->getServices()->localisation->setLocBinding(this);
  field_filter = new Localisation::FieldPF();

  init();

  currTS = getNowTS();
  lastTS = currTS;
  lastFieldReset = currTS;
  lastUniformReset = currTS;

  _runThread = new std::thread(std::bind(&LocalisationBinding::run, this));
}

LocalisationBinding::~LocalisationBinding()
{
}

void LocalisationBinding::run()
{
  while(true) {
    step();
    if (scheduler->isFakeMode()) {
      while(true) {
        // Here, we check if there is a premature exit or if enough time ha
        // been elapsed according to vision TimeStamps
        double elapsed = diffSec(currTS, getNowTS());
        bool referee_allow_playing = refereeAllowsToPlay();
        bool premature_exit = field_filter->isResetPending() && referee_allow_playing;
        if (elapsed > period || premature_exit) break;
        // Sleep for 10 ms
        usleep(10 * 1000);
      }
    }
    else {
      double elapsed = diffSec(currTS, getNowTS());
      fieldLogger.log("Step time: %lf", elapsed);
      if (elapsed < period) {
        int sleep_us = (int)((period - elapsed) * 1000 * 1000);
        // Sleep a total time of sleep_us by small intervals and interrupt if 
        // there is a reset pending
        int count = sleep_us/10000;
        for (int k=0; k<count; k++) {
          bool referee_allow_playing = refereeAllowsToPlay();
          bool premature_exit = field_filter->isResetPending() && referee_allow_playing;
          if  (premature_exit) {
            fieldLogger.log("Premature exit from sleep (reset pending)");
            break;
          }
          usleep(10000);
        }
      }
    }
  }
}

void LocalisationBinding::init()
{
  initRhIO();
  importFromRhIO();
  field_filter->initializeAtUniformRandom(nb_particles_ff);
}

//TODO: eventually build Image handlers

void LocalisationBinding::initRhIO()
{
  // Only bind once
  if (RhIO::Root.commandExist("Localisation/resetFilters")) {
    return;
  }
  // Init interface with RhIO
  RhIO::Root.newCommand("Localisation/resetFilters",
                        "Reset all particle filters to an uniform distribution",
                        [this](const std::vector<std::string> &args)
                            -> std::string {
                              lastFieldReset = getNowTS();
                              lastUniformReset = lastFieldReset;
                              vision_binding->ballStackFilter->clear();
                              vcCounterMutex.lock();
                              nbVCObs = 0;
                              consistencyScore = 0;
                              field_filter->askForReset();
                              vcCounterMutex.unlock();
                              return "Field have been reset";
                            });
  RhIO::Root.newCommand(
      "Localisation/bordersReset",
      "Reset on the borders",
      [this](const std::vector<std::string> &args) -> std::string {
        fieldReset(FieldPF::ResetType::Borders);
        return "Field have been reset";
      });
  RhIO::Root.newCommand(
      "Localisation/fallReset", "Apply a fall event on field particle filter",
      [this](const std::vector<std::string> &args) -> std::string {
        fieldReset(FieldPF::ResetType::Fall);
        return "Field have been reset";
      });
  RhIO::Root.newCommand(
      "Localisation/customReset", "Reset the field particle filter at the "
                                  "custom position with custom noise",
      [this](const std::vector<std::string> &args) -> std::string {
        unsigned int k = 0;

        auto rhioNode = &(RhIO::Root.child("/Localisation/Field/FieldPF"));
        for (string item : {"customX", "customY", "customTheta", "customNoise", "customThetaNoise"}) {
          if (args.size() > k) {
            rhioNode->setFloat(item, atof(args[k].c_str()));
          }
          k++;
        }
        lastFieldReset = getNowTS();
        vcCounterMutex.lock();
        nbVCObs = minVCObs;
        consistencyScore = 1;
        field_filter->askForReset(FieldPF::ResetType::Custom);
        vcCounterMutex.unlock();
        return "Field have been reset";
      });
  // Number of particles in the field filter
  RhIO::Root.newInt("/Localisation/Field/nbParticles")
      ->defaultValue(nb_particles_ff)
      ->comment("Number of particles in the localisation filter")
      ->persisted(true);
  // Consistency
  RhIO::Root.newBool("/Localisation/Consistency/enabled")
      ->defaultValue(consistencyEnabled)
      ->comment("Is consistency check enabled? (If disable, consistencyScore is not updated)")
      ->persisted(true);
  RhIO::Root.newFloat("/Localisation/Consistency/elapsedSinceReset")
      ->defaultValue(0)
      ->comment("Elapsed time since last reset (from any source) [s]");
  RhIO::Root.newFloat("/Localisation/Consistency/elapsedSinceUniformReset")
      ->defaultValue(0)
      ->comment("Elapsed time since last uniform reset (from any source) [s]");
  RhIO::Root.newFloat("/Localisation/Consistency/score")
      ->defaultValue(consistencyScore)
      ->maximum(1.0)
      ->minimum(0.0)
      ->comment("Current consistency quality");
  RhIO::Root.newFloat("/Localisation/Consistency/stepCost")
      ->defaultValue(consistencyStepCost)
      ->comment("The reduction of consistencyScore at each step")
      ->persisted(true);
  RhIO::Root.newFloat("/Localisation/Consistency/badObsCost")
      ->defaultValue(consistencyBadObsCost)
      ->comment("The reduction of consistencyScore for each bad observation")
      ->persisted(true);
  RhIO::Root.newFloat("/Localisation/Consistency/goodObsGain")
      ->defaultValue(consistencyGoodObsGain)
      ->comment("The increase of consistencyScore for each 'good' observation")
      ->persisted(true);
  RhIO::Root.newFloat("/Localisation/Consistency/resetInterval")
      ->defaultValue(consistencyResetInterval)
      ->comment("The minimal time to wait between two consistency resets [s]")
      ->persisted(true);
  RhIO::Root.newFloat("/Localisation/Consistency/maxNoise")
      ->defaultValue(consistencyMaxNoise)
      ->comment("Noise factor at 0 consistencyScore")
      ->persisted(true);
  RhIO::Root.newFloat("/Localisation/period")
      ->defaultValue(period)
      ->maximum(30.0)
      ->minimum(0.0)
      ->comment("Period between two ticks from the particle filter")
      ->persisted(true);
  RhIO::Root.newFloat("/Localisation/Consistency/elapsedSinceConvergence")
      ->defaultValue(0)
      ->comment("Elapsed time since last convergence or reset [s]");
  RhIO::Root.newFloat("/Localisation/Field/maxNoiseBoost")
      ->defaultValue(maxNoiseBoost)
      ->maximum(30.0)
      ->minimum(1.0)
      ->comment("Maximal multiplier for exploration in boost mode")
      ->persisted(true);
  RhIO::Root.newFloat("/Localisation/Field/noiseBoostDuration")
      ->defaultValue(noiseBoostDuration)
      ->maximum(30.0)
      ->minimum(0.0)
      ->comment("Duration of the noise boost after global reset [s]")
      ->persisted(true);
  RhIO::Root.newInt("/Localisation/Field/nbVCObs")
      ->defaultValue(nbVCObs)
      ->comment("Number of compass observations used since last uniform reset");
  RhIO::Root.newInt("/Localisation/Field/minVCObs")
      ->defaultValue(minVCObs)
      ->comment("Number of compass observations necessary to disable VC")
      ->persisted(true);
  RhIO::Root.newBool("/Localisation/Field/isUsingVisualCompass")
      ->defaultValue(isUsingVisualCompass)
      ->comment("Is the localisation currently using the visual compass");

  RhIO::Root.newFrame("Localisation/TopView", "", RhIO::FrameFormat::BGR);

  // Binding Localisation items
  RobotController::bindWithRhIO();
  GoalObservation::bindWithRhIO();
  TagsObservation::bindWithRhIO();
  CompassObservation::bindWithRhIO();

}

void LocalisationBinding::importFromRhIO() {
  RobotController::importFromRhIO();
  GoalObservation::importFromRhIO();
  TagsObservation::importFromRhIO();
  CompassObservation::importFromRhIO();
  field_filter->importFromRhIO();
  nb_particles_ff =
      RhIO::Root.getValueInt("/Localisation/Field/nbParticles").value;
  // Noise boost
  maxNoiseBoost =
      RhIO::Root.getValueFloat("/Localisation/Field/maxNoiseBoost").value;
  noiseBoostDuration =
      RhIO::Root.getValueFloat("/Localisation/Field/noiseBoostDuration").value;
  // Visual compass
  minVCObs = RhIO::Root.getValueInt("/Localisation/Field/minVCObs").value;
  // Consistency import
  consistencyEnabled =
      RhIO::Root.getValueBool("/Localisation/Consistency/enabled").value;
  consistencyStepCost =
      RhIO::Root.getValueFloat("/Localisation/Consistency/stepCost").value;
  consistencyBadObsCost =
      RhIO::Root.getValueFloat("/Localisation/Consistency/badObsCost").value;
  consistencyGoodObsGain =
      RhIO::Root.getValueFloat("/Localisation/Consistency/goodObsGain").value;
  consistencyResetInterval =
      RhIO::Root.getValueFloat("/Localisation/Consistency/resetInterval").value;
  consistencyMaxNoise =
      RhIO::Root.getValueFloat("/Localisation/Consistency/maxNoise").value;
  period = RhIO::Root.getValueFloat("/Localisation/period").value;
}

void LocalisationBinding::publishToRhIO() {
  RhIO::Root.setFloat("/Localisation/Consistency/elapsedSinceReset",
                      elapsedSinceReset);
  RhIO::Root.setFloat("/Localisation/Consistency/elapsedSinceUniformReset",
                      elapsedSinceUniformReset);
  RhIO::Root.setFloat("/Localisation/Consistency/score", consistencyScore);
  RhIO::Root.setBool("/Localisation/Field/isUsingVisualCompass",
                     isUsingVisualCompass);
  RhIO::Root.setInt("/Localisation/Field/nbVCObs", nbVCObs);

  field_filter->publishToRhIO();

  bool isStreaming = RhIO::Root.frameIsStreaming("/Localisation/TopView");
  if (isStreaming) {
    int width = 1040;
    int height = 740;
    cv::Mat topView = getTopView(width, height);
    RhIO::Root.framePush("/Localisation/TopView", width, height, topView.data,
                         width * height * 3);
  }
}

void LocalisationBinding::step()
{
  importFromRhIO();

  currTS = getNowTS();
  cs->updateInternalModel(currTS.getTimeMS() / 1000);


  elapsedSinceReset = diffSec(lastFieldReset, currTS);
  elapsedSinceUniformReset = diffSec(lastUniformReset, currTS);

  // Always steal informations from vision
  stealFromVision();

  // Get information from the referee
  bool refereeAllowTicks = refereeAllowsToPlay();
  
  // When the robot is penalized do not update anything, but increase reactivity
  if (!refereeAllowTicks) {
    lastForbidden = currTS;
    isForbidden = true;
    fieldLogger.log("Referee forbid ticks");
    // Avoid having a uniform reset pending when robot is penalized or in initial phase
    field_filter->cancelPendingReset(FieldPF::ResetType::Uniform);
    // Since we don't want uniform reset, make sur that nbVCObs is equal to minVCObs
    vcCounterMutex.lock();
    nbVCObs = minVCObs;
    vcCounterMutex.unlock();
    // Ensure that visual compass is not used while referee does not allow to play
    setVisualCompassStatus(false);

    publishToRhIO();
    return;
  }

  // Determining if the robot is fallen
  DecisionService * decisionService = scheduler->getServices()->decision;
  if (decisionService->isFallen) {
    fieldLogger.log("Robot is fallen, forbidding ticks");
    publishToRhIO();
    return;
  }

  FieldPF::ResetType pending_reset = field_filter->getPendingReset();
  double elapsed_since_forbidden = diffSec(lastForbidden, currTS);
  double start_without_reset_delay = 10;//[s]: to free the robot if it is not allowed to play
  // Wait a proper reset for some time
  // (avoid starting a tick before receiving informations from 'robocup' move)
  if (isForbidden && elapsed_since_forbidden < start_without_reset_delay &&
      (pending_reset == FieldPF::ResetType::None || pending_reset == FieldPF::ResetType::Uniform)) {
    std::ostringstream msg;
    msg << "Delaying restart of filter: "
        << "elapsed since forbidden:" << elapsed_since_forbidden << " "
        << "Pending reset: '" << FieldPF::getName(pending_reset) << "'";
    fieldLogger.log(msg.str().c_str());

    publishToRhIO();
    return;
  }

  isForbidden = false;

  // Determine if a visualCompass is required
  fieldLogger.log("consistency: %d | nbVCObs: %d | minVCObs: %d",
                  consistencyEnabled, nbVCObs, minVCObs);
  setVisualCompassStatus(consistencyEnabled && nbVCObs < minVCObs);

  // Compute observations if there is no reset pending
  ObservationVector observations;
  if (!field_filter->isResetPending()) {
    observations = extractObservations();
  }

  // Update consistency
  if (consistencyEnabled) {
    applyWatcher(observations);
  }
  else {
    consistencyScore = 0;
  }

  // Update filter with the provided observations
  updateFilter(observations);

  // Avoid memory leaks
  for (size_t id = 0; id < observations.size(); id++) {
    delete(observations[id]);
  }

  importFiltersResults();

  publishToLoc();
  publishToRhIO();
}

TimeStamp LocalisationBinding::getNowTS() {
  if (scheduler->isFakeMode()) {
    return vision_binding->sourceTS;
  }
  return TimeStamp::now();
}

void LocalisationBinding::setVisualCompassStatus(bool active)
{
  // When status changes, update filters and localisationService
  if (active != isUsingVisualCompass) {
    fieldLogger.log("Setting compass status to %d, previously %d", active, isUsingVisualCompass);
    isUsingVisualCompass = active;
    try {
      std::string visualCompassPath = "Vision/visualCompass";
      RhIO::IONode & visualCompassNode = RhIO::Root.child(visualCompassPath);
      int enabled = active ? 1 : 0;
      visualCompassNode.setInt("enabled", enabled);
      scheduler->getServices()->localisation->setVisualCompassStatus(active);
    } catch (const std::logic_error & error) {
      fieldLogger.error("Unable to enable/disable visualCompass: (%s)",
                        error.what());
    }
  }
}

std::vector<GoalObservation *> LocalisationBinding::extractGoalObservations()
{
  std::vector<GoalObservation *> goalObservations;
  // Goal Observations
  for (size_t i = 0; i < goalsLocations.size(); i++) {
    cv::Point2f pos_in_self = cs->getPosInSelf(goalsLocations[i]);
    double robotHeight = cs->getHeight() / 100;

    std::pair<Angle, Angle> panTiltToGoal;
    panTiltToGoal = cs->panTiltFromXY(pos_in_self, robotHeight);
    Angle panToGoal = panTiltToGoal.first;
    Angle tiltToGoal = panTiltToGoal.second;
    GoalObservation * newObs = new GoalObservation(panToGoal, tiltToGoal, cs->getHeight());
    // Adding new observation or merging based on similarity
    bool has_similar = false;
    for (GoalObservation * goalObs : goalObservations) {
      if (GoalObservation::isSimilar(*newObs, *goalObs)) {
        has_similar = true;
        goalObs->merge(*newObs);
      }
    }
    if (has_similar) {
      delete(newObs);
    }
    else {
      goalObservations.push_back(newObs);
    }
  }

  return goalObservations;
}

std::vector<ArenaCornerObservation *>
LocalisationBinding::extractArenaCornerObservations() {
  // Introduction des observations des coins du terrain 
  std::vector<ArenaCornerObservation *> arenaCornerObservations;
  printf("retrieving arenaCornerObs : %d\n", (int)clipping_data.size());
  for (size_t i = 0; i < clipping_data.size(); i++) {
    if (clipping_data[i].is_obs_valid()) {
      cv::Point2f pos_in_self = cs->getPosInSelf(clipping_data[i].getCornerInWorldFrame());
      double robotHeight = cs->getHeight() / 100;
      std::pair<Angle, Angle> panTiltToGoal;
      panTiltToGoal = cs->panTiltFromXY(pos_in_self, robotHeight);
      Angle panToGoal = panTiltToGoal.first;
      Angle tiltToGoal = panTiltToGoal.second;
      try {
        ArenaCornerObservation * newObs =
          new ArenaCornerObservation(clipping_data[i], panToGoal, tiltToGoal, cs->getHeight());
        arenaCornerObservations.push_back(newObs);
      } catch (const std::string msg) {
        printf("ArenaCornerObservation unconsistancy; error at construction : %s", msg.c_str());
      }
    }
  }
  return arenaCornerObservations;
}
  
std::vector<TagsObservation *> LocalisationBinding::extractTagsObservations()
{
  std::vector<TagsObservation *> tagsObservations;
  std::map<int,std::vector<Eigen::Vector3d>> tagsInSelf;
  for (size_t markerId = 0; markerId < markerIndices.size(); markerId++) {
    Eigen::Vector3d pos_in_world = markerPositions[markerId];
    Eigen::Vector3d pos_in_self =
      cs->_model->frameInSelf("origin", pos_in_world);
    tagsInSelf[markerIndices[markerId]].push_back(pos_in_self);
  }
  for (const std::pair<int, std::vector<Eigen::Vector3d>> & entry : tagsInSelf) {
    int nb_obs = entry.second.size();
    // Compute mean
    Eigen::Vector3d mean = Eigen::Vector3d::Zero();
    for (const Eigen::Vector3d & pos : entry.second) {
      mean += pos;
    }
    mean /= nb_obs;
    // Compute stddev
    Eigen::Vector3d err2 = Eigen::Vector3d::Zero();
    for (const Eigen::Vector3d & pos : entry.second) {
      Eigen::Vector3d diff = pos - mean;
      err2 += diff.cwiseProduct(diff);
    }
    Eigen::Vector3d dev = (err2 / nb_obs).cwiseSqrt();
    // Absurd conversion to cm
    mean *= 100;
    dev *= 100;
    cv::Point3f cv_pos = Utils::eigenToCV(mean);
    cv::Point3f cv_dev = Utils::eigenToCV(dev);
    tagsObservations.push_back(new TagsObservation(entry.first, cv_pos, cv_dev,
                                                   cs->getHeight(),
                                                   entry.second.size()));
  }
  return tagsObservations;
}

std::vector<CompassObservation *> LocalisationBinding::extractCompassObservations()
{
  std::vector<CompassObservation *> compassObservations;
  for(size_t it=0; it<compassOrientations.size();it++)
  {
    //FIXME Hard ignore the dispersion <0.5
    // (currently ignore all results where homography failed)
    if(compassQuality[it]>0.5)
    {
      Angle trunkYaw=cs->getTrunkYawInWorld();
      double compassInTrunk = compassOrientations[it]-trunkYaw.getSignedValue();
      compassObservations.push_back(new CompassObservation(compassInTrunk));
    }
  }
  return compassObservations;
}

void LocalisationBinding::stealFromVision()
{
  // Declaring local unused variables to fit signature
  std::vector<std::pair<float, float> > markerCenters;
  std::vector<std::pair<float, float> > markerCentersUndistorded;
  // Stealing data
  double tagTimestamp = 0;// Unused
  goalsLocations = vision_binding->stealGoals();
  vision_binding->stealTags(markerIndices, markerPositions,
                            markerCenters, markerCentersUndistorded, &tagTimestamp);
  vision_binding->stealCompasses(compassOrientations,compassQuality);
  clipping_data = vision_binding->stealClipping();
  fieldLogger.log("Nb observations stolen: (%d goals, %d markers, %d compass)",
                  goalsLocations.size(), markerPositions.size(), compassOrientations.size());
}

LocalisationBinding::ObservationVector LocalisationBinding::extractObservations()
{
  // Declaration of the vectors used
  ObservationVector fieldObservations;
  std::vector<GoalObservation *> goalObservations;
  std::vector<TagsObservation *> tagsObservations;
  std::vector<CompassObservation *> compassObservations;

  if (isUsingVisualCompass) {
    for(CompassObservation *obs: extractCompassObservations()) {
      fieldObservations.push_back(obs);
      vcCounterMutex.lock();
      nbVCObs++;
      vcCounterMutex.unlock();
    }
  }
  else {
    int obsId = 0;
    for (GoalObservation * obs : extractGoalObservations()) {
      fieldObservations.push_back(obs);
      fieldLogger.log("Goal %d -> pan: %lf, tilt: %lf, weight: %1lf",
                      obsId, obs->pan.getSignedValue(),
                      obs->tilt.getSignedValue(), obs->weight);
      obsId++;
    }
    for (ArenaCornerObservation * obs : extractArenaCornerObservations()) {
      fieldObservations.push_back(obs);
      fieldLogger.log("Arena Corner %d -> pan: %lf, tilt: %lf, weight: %1lf, dist: %lf",
		      obsId, obs->getPan().getSignedValue(),
		      obs->getTilt().getSignedValue(),
		      obs->getWeight(),
		      obs->getBrutData().getRobotCornerDist());
      obsId++;
    }

    for (TagsObservation * obs : extractTagsObservations()) {
      fieldObservations.push_back(obs);
      fieldLogger.log("Tags %d -> id: %d, pos: (%.3lf, %.3lf, %.3lf), "
                      "dev: (%.3lf, %.3lf, %.3lf), height: %lf  weight: %lf",
                      obsId, obs->id,
                      obs->seenPos.x, obs->seenPos.y, obs->seenPos.z,
                      obs->stdDev.x, obs->stdDev.y, obs->stdDev.z,
                      obs->robotHeight, obs->weight);
      obsId++;
    }
  }

  // Add field observation, but only if we have some other observations
  if (fieldObservations.size()>0) {
    fieldObservations.push_back(new FieldObservation(isGoalKeeper));
  }

  return fieldObservations;
}

void LocalisationBinding::updateFilter(
  const std::vector<rhoban_unsorted::Observation<Localisation::FieldPosition> *> & obs)
{
  ModelService * model_service = scheduler->getServices()->model;

  // Check if base has been updated since last tick:
  bool isWalkEnabled = model_service->wasReadBaseUpdate();

  if (scheduler->isFakeMode()) {
    isWalkEnabled = true;
  }

  //ComputedOdometry
  double odom_start = lastTS.getTimeMS() / 1000.0;
  double odom_end = currTS.getTimeMS() / 1000.0;
  FieldPF::ResetType pending_reset = field_filter->getPendingReset();
  // If a reset has been asked, use odometry since reset.
  // Specific case for fall_reset, we still want to use the odometry prior to the reset
  if (pending_reset != FieldPF::ResetType::None &&
      pending_reset != FieldPF::ResetType::Fall) {
    // Specific case for resets, we don't want to integrate motion before reset
    odom_start = lastFieldReset.getTimeMS() / 1000.0;
  }
  Eigen::Vector3d odo = model_service->odometryDiff(odom_start, odom_end);
  cv::Point2f robotMove;
  robotMove.x = odo(0) * 100;
  robotMove.y = odo(1) * 100;
  double orientationChange = rad2deg(odo(2));
  if (std::fabs(orientationChange) > 90) {
    fieldLogger.warning("unlikely orientation change received from odometry: %f deg",
                        orientationChange);
  }
  // Use a boost of noise after an uniformReset
  double noiseGain = 1;
  if (elapsedSinceUniformReset < noiseBoostDuration) {
    double ratio = elapsedSinceUniformReset / noiseBoostDuration;
    noiseGain = maxNoiseBoost * (1 - ratio) + ratio;
    fieldLogger.log("Using noise boost gain: %lf (%lf[s] elapsed)",
                    noiseGain, elapsedSinceUniformReset);
  } else if (consistencyEnabled) {
    noiseGain = 1 + (1-consistencyScore) * (consistencyMaxNoise - 1);
    fieldLogger.log("Using consistency boost gain: %lf (score: %lf)",
                    noiseGain, consistencyScore);
  }

  RobotController rc(cv2rg(robotMove), orientationChange, noiseGain);

  double elapsed = odom_end - odom_start;

  // Update the filter if one of the following conditions are met:
  // - An observation has been found
  // - Walk was enabled at least once since the last tick
  // - A reset has been required
  bool isResetPending = field_filter->isResetPending();
  if (enableFieldFilter && (obs.size() > 0 || isWalkEnabled || isResetPending)) {
    filterMutex.lock();
    field_filter->resize(nb_particles_ff);
    field_filter->step(rc, obs, elapsed);
    filterMutex.unlock();
  }
  else {
    std::stringstream oss;
    oss << "skipping step: ";
    if (!enableFieldFilter) oss << "field disabled, ";
    if (!isWalkEnabled) oss << "walk disabled, ";
    if (!isResetPending) oss << "no reset planned, ";
    oss << "nbObs: " << obs.size();
    fieldLogger.log(oss.str().c_str());
  }

  // If we updated the filter, it is important to update lastTS for next odometry.
  // If we skipped the step, it means that there is no point in using odometry from
  // lastTS to currTS, therefore, we can safely update lastTS
  lastTS = currTS;
}

void LocalisationBinding::publishToLoc()
{
  LocalisationService *loc = scheduler->getServices()->localisation;

  // update the loc service
  cv::Point2d lg = field_filter->getLeftGoalPositionInSelf();
  cv::Point2d rg = field_filter->getRightGoalPositionInSelf();
  cv::Point2d c = field_filter->getCenterPositionInSelf();
  Angle o = field_filter->getOrientation();

  loc->setPosSelf(Eigen::Vector3d(lg.x, lg.y, 0),
                  Eigen::Vector3d(rg.x, rg.y, 0), Eigen::Vector3d(c.x, c.y, 0),
                  deg2rad(o.getValue()),
                  robotQ, consistencyScore);
}

void LocalisationBinding::applyWatcher(
  const std::vector<rhoban_unsorted::Observation<Localisation::FieldPosition> *> & obs)
{
  // Apply HighLevel PF
  double stepDeltaScore = -consistencyStepCost;
  const auto &particle = field_filter->getRepresentativeParticle();
  std::vector<rhoban_unsorted::BoundedScoreObservation<FieldPosition> *> castedObservations;
  int obsId = 0;
  for (rhoban_unsorted::Observation<FieldPosition> *o : obs) {
    GoalObservation * goalObs = dynamic_cast<GoalObservation *>(o);
    // Ignore non goal observations for quality check
    if (goalObs == nullptr) {
      continue;
    }
    // Checking Score of the particle
    double score = goalObs->potential(particle, true);
    double minScore = goalObs->getMinScore();
    // Debug
    fieldLogger.log("Observation %d: %s -> score: %f , minScore: %f",
                    obsId, goalObs->toStr().c_str(), score, minScore);
    obsId++;
    // If score <= minScore, then observation is so different from expected result
    // that there is only two possibilities:
    // 1. Vision provided a false positive
    // 2. Representative particle location is really wrong
    if (score > minScore) {
      stepDeltaScore += consistencyGoodObsGain;
    } else {
      stepDeltaScore -= consistencyBadObsCost;
    }      
  }

  /// Update consistency score
  consistencyScore += stepDeltaScore;
  consistencyScore = std::min(1.0,std::max(0.0, consistencyScore));
  fieldLogger.log("Updating consistency: deltaStep: %f | new consistency: %f",
                  stepDeltaScore, consistencyScore);

  /// Reset of the particle filter requires several conditions
  /// - We have not reseted the filter for  long time
  /// - ConsistencyScore has reached 0
  /// - There is no reset pending on the robot
  bool resetAllowed = elapsedSinceUniformReset > consistencyResetInterval;
  bool lowConsistency = consistencyScore <= 0;
  fieldLogger.error("resetAllowed: %d, consistency: %f (elapsed since UR: %f)",
                    resetAllowed, consistencyScore, elapsedSinceUniformReset);
  if (resetAllowed && lowConsistency && !field_filter->isResetPending()) {
    lastFieldReset = getNowTS();
    lastUniformReset = lastFieldReset;
    vcCounterMutex.lock();
    nbVCObs = 0;
    // consistencyScore starts at 0
    consistencyScore = 0;
    field_filter->askForReset();
    vcCounterMutex.unlock();
    std::ostringstream msg;
    msg << "Asking for a full reset: " << std::endl;
    msg << "consistencyScore: " << consistencyScore
        << " robotQ: " << robotQ;
    fieldLogger.log(msg.str().c_str());
  }
}

void LocalisationBinding::importFiltersResults() {
  filterMutex.lock();
  // Goal
  toGoal = field_filter->getAngleToGoal();
  toGoalQ = field_filter->angleToGoalQuality();
  // Robot
  robot = field_filter->getRepresentativeParticle();
  robotQ = field_filter->getRepresentativeQuality();

  filterMutex.unlock();
}

cv::Mat LocalisationBinding::getTopView(int width, int height) {
  if (cs == NULL)
    throw std::runtime_error("TopView is not ready");
  filterMutex.lock();
  cv::Mat img(height, width, CV_8UC3);
  field_filter->draw(img, cs);

  filterMutex.unlock();
  return img;
}

void LocalisationBinding::fieldReset(Localisation::FieldPF::ResetType type, float x,
                                     float y, float noise, float theta, float thetaNoise) {
  lastFieldReset = getNowTS();

  if (type == Localisation::FieldPF::ResetType::Custom) {
    auto rhioNode = &(RhIO::Root.child("/Localisation/Field/FieldPF"));
    rhioNode->setFloat("customX", x);
    rhioNode->setFloat("customY", y);
    rhioNode->setFloat("customNoise", noise);
    rhioNode->setFloat("customTheta", theta);
    rhioNode->setFloat("customThetaNoise", thetaNoise);
  }

  vcCounterMutex.lock();
  if (type == Localisation::FieldPF::Uniform) {
    lastUniformReset = lastFieldReset;
    nbVCObs = 0;
    consistencyScore = 0;
  } else if (type != Localisation::FieldPF::ResetType::Fall) {
    // If there is an other reset than the uniform or fall one, we don't need visual compass
    nbVCObs = minVCObs;
    consistencyScore = 1;
  }
  field_filter->askForReset(type);
  vcCounterMutex.unlock();
}


bool LocalisationBinding::refereeAllowsToPlay()
{
  // On fake mode, always allow robot to play
  if (scheduler->isFakeMode()) return true;

  RefereeService * referee = scheduler->getServices()->referee;
  bool allowedPhase = referee->isPlacingPhase() || referee->isFreezePhase();
  bool penalized = referee->isPenalized();
  return referee->isPlaying() || (allowedPhase && !penalized);
}

}

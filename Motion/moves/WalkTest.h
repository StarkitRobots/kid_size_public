#pragma once

#include "Move.h"
#include "engines/walk_engine.h"

class WalkTest : public Move
{
public:
  WalkTest();
  std::string getName();

  void onStart();
  void step(float elapsed);

protected:
  double timeSinceLastStep;

  bool walkEnable;
  double trunkPitch;
  rhoban::WalkEngine engine;

  double armsRoll, elbowOffset;
  int stepCount = 0;

  double swingGainStart;
};

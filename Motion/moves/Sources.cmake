set (SOURCES
    Approach.cpp
    ApproachPotential.cpp
    ApproachMove.cpp
    Penalty.cpp
    Walk.cpp
    StandUp.cpp
    Move.cpp
    Moves.cpp
    Search.cpp
    StaticLearner.cpp
    STM.cpp
    Replayer.cpp
    Head.cpp
    Robocup.cpp
    Playing.cpp
    TestHeadSinus.cpp
    IMUTest.cpp
    TrajectoriesPlayer.cpp
    CameraCalibration.cpp
    OdometryCalibration.cpp
    ModelCalibration.cpp
    GoalKeeper.cpp
    Placer.cpp
    Kick.cpp
    LateralStep.cpp
    ArucoCalibration.cpp
    
    LogMachine.cpp
    KickPhilipp.cpp

    GoalKick.cpp
)

if (csa_mdp_experiments_FOUND)
  set(SOURCES
    ${SOURCES}
    KickController.cpp
    LearnedApproach.cpp
    MDPKickController.cpp
    QKickController.cpp
    ClearingKickController.cpp
    PenaltyKickController.cpp
    )
endif(csa_mdp_experiments_FOUND)


set_source_files_properties(Placer.cpp PROPERTIES COMPILE_FLAGS "-g -O0")

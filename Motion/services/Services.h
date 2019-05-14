#pragma once

#include <map>
#include <vector>
#include <string>

class MoveScheduler;
class Service;

class ModelService;
class RobotModelService;
class OdometryService;
class RefereeService;
class LocalisationService;
class TeamPlayService;
class DecisionService;
class StrategyService;
class CaptainService;
class ViveService;

/**
 * Services
 *
 * Services container
 */
class Services
{
public:
  /**
   * Initialization with main
   * ModelService instance
   */
  Services(MoveScheduler* scheduler);

  /**
   * Deallocation of all
   * contained services
   */
  ~Services();

  /**
   * Services access
   */
  bool hasService(const std::string& name) const;
  Service* getService(const std::string& name);
  const std::vector<std::pair<std::string, Service*>>& getAllServices();

  /**
   * Direct access to services instance
   */
  ModelService* model;
  RobotModelService *robotModel;
  OdometryService* odometry;
  RefereeService* referee;
  LocalisationService* localisation;
  TeamPlayService* teamPlay;
  DecisionService* decision;
  StrategyService* strategy;
  CaptainService* captain;
  ViveService* vive;

private:
  /**
   * Main MoveScheduler pointer
   */
  MoveScheduler* _scheduler;

  /**
   * Services container and map
   * (For direct access and well defined iteration order).
   */
  std::vector<std::pair<std::string, Service*>> _container;
  std::map<std::string, Service*> _map;

  /**
   * Insert a new service inside the container
   */
  void add(const std::string& name, Service* service);
};

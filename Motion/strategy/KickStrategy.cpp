#include <cmath>
#include <iostream>
#include <robocup_referee/constants.h>
#include <rhoban_utils/util.h>

#include "KickStrategy.hpp"

using namespace robocup_referee;
        
KickStrategy::KickStrategy(double accuracy)
    : accuracy(accuracy)
{
}

KickStrategy::Action KickStrategy::actionFor(double x, double y)
{
    x += Constants::fieldLength/200.0;
    y += Constants::fieldWidth/200.0;

    // Bounding x and y
    if (x < 0) x = 0;
    if (x > Constants::fieldLength/100.0) x = Constants::fieldLength/100.0;
    if (y < 0) y = 0;
    if (y > Constants::fieldWidth/100.0) y = Constants::fieldWidth/100.0;

    int X = round(x/accuracy);
    int Y = round(y/accuracy);

    if (actions.count(X) && actions[X].count(Y)) {
        return actions[X][Y];
    } else {
        KickStrategy::Action action;;
        action.kick = "";
        return action;
    }
}

double KickStrategy::scoreFor(double x, double y)
{
    return actionFor(x, y).score;
}

void KickStrategy::setAction(double x, double y, Action action)
{
    int X = round(x/accuracy);
    int Y = round(y/accuracy);

    actions[X][Y] = action;
}

bool KickStrategy::fromJson(std::string filename)
{
    actions.clear();
    auto data = rhoban_utils::file_get_contents(filename);

    if (data != "") {
        Json::Reader reader;
        Json::Value json;

        if (reader.parse(data, json)) {
            accuracy = json["accuracy"].asDouble();
            for (Json::ArrayIndex k=0; k<json["actions"].size(); k++) {
                auto entry = json["actions"][k];
                int x = entry[0].asInt();
                int y = entry[1].asInt();
                Action action;
                action.kick = entry[2].asString();
                action.orientation = entry[3].asDouble();
                action.tolerance = entry[4].asDouble();
                action.score = entry[5].asDouble();
                actions[x][y] = action;
            }

            return true;
        }
    }

    return false;
}

Json::Value KickStrategy::toJson()
{
    Json::Value json(Json::objectValue);

    json["accuracy"] = accuracy;
    json["actions"] = Json::Value(Json::arrayValue);

    for (auto &xEntry : actions) {
        int x = xEntry.first;
        for (auto &yEntry : xEntry.second) {
            int y = yEntry.first;
            Json::Value action(Json::arrayValue);

            action[0] = x;
            action[1] = y;
            action[2] = yEntry.second.kick;
            action[3] = yEntry.second.orientation;
            action[4] = yEntry.second.tolerance;
            action[5] = yEntry.second.score;

            json["actions"].append(action);
        }
    }

    return json;
}

void KickStrategy::gnuplot()
{
    for (auto &xEntry : actions) {
        int x = xEntry.first;
        for (auto &yEntry : xEntry.second) {
            int y = yEntry.first;
            double X = x*accuracy;
            double Y = y*accuracy;
            double kickX = cos(yEntry.second.orientation)*accuracy*0.5;
            double kickY = sin(yEntry.second.orientation)*accuracy*0.5;
            double kick = 0;
            if (yEntry.second.kick == "lateral") kick = 2;
            if (yEntry.second.kick == "classic") kick = 3;
            if (yEntry.second.kick == "opportunist") kick = 1;

            std::cout << X << " " << Y << " " << kick << std::endl;
            std::cout << X+kickX << " " << Y+kickY << " " << kick << std::endl;
            std::cout << std::endl;
            std::cout << std::endl;
        }
    }
}

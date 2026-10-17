// Old path of the gameplay transforms, for octtest.cpp only: delete once it includes the games/di1 headers.
#pragma once
#include "games/di1/activities.hpp"
#include "games/di1/challenges.hpp"
#include "games/di1/curves.hpp"
#include "games/di1/surfaces.hpp"
#include "games/di1/toolset.hpp"
#include "games/di1/zones.hpp"

namespace gameplay {

using di1::currencies10;
using di1::Playset10;
using di1::playsets10;
using di1::activities::activityGivers;
using di1::activities::activityGraph30;
using di1::activities::activityTasks30;
using di1::activities::apGivers30;
using di1::activities::dnaGadgets30;
using di1::challenges::beacons30;
using di1::challenges::challengeTypePath;
using di1::challenges::challengeTypes30;
using di1::challenges::zonelist30;
using di1::curves::curves30;
using di1::surfaces::surfaces30;
using di1::toolset::levelAllows;
using di1::toolset::toolLevels10;

}  // namespace gameplay

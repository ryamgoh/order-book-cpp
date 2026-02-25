#pragma once

#include "Usings.hpp"
#include <vector>

// When we think of OrderBooks, we think of Levels!
// This is used in our public APIs to get information about the state of the
// orderbook
// Essentially, instead of exposing individual orders, we use LevelInfo as a way
// to aggregate orders at the same price. i.e If we have 3 Orders of 20 units @
// $50, they get aggregated to 60 units @ $50, at the $50 range
struct LevelInfo {
  Price price_;
  Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;


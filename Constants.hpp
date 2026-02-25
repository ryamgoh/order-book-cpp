#pragma once

#include <limits>

#include "Usings.hpp"

struct Constants {
  static const Price InvalidPrice = std::numeric_limits<Price>::quiet_NaN();
};

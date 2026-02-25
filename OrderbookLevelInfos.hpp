#pragma once

#include "LevelInfos.hpp"
class OrderbookLevelInfos {
public:
  OrderbookLevelInfos(const LevelInfos &bids, const LevelInfos &asks)
      : bids_{bids}, asks_{asks} {};

  // Return Type == `const LevelInfos &`
  // LevelInfos = The Type of Data being Returned
  // & = Returns a reference not a copy
  // const = The returned reference is read-only
  //
  // Function Name == `GetBids`
  //
  // const at the end ==
  // promises this function will not modify any member variables of the class
  const LevelInfos &GetBids() const { return bids_; }
  const LevelInfos &GetAsks() const { return asks_; }

private:
  LevelInfos bids_;
  LevelInfos asks_;
};


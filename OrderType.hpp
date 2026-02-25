#pragma once

enum class OrderType {
  // Instructs the broker to buy and sell immediately at the BEST available price.
  // Guarantees execution but not a specific price.
  Market,
  // Limit order that expires at the end of a trading session (i.e. closing time of stock exchange)
  // Advantage: avoids unexpected, unwanted trades for future time periods
  GoodForDay, 
  // For long term traders, swing traders, by setting "limit" orders for specific price targets (i.e. buy the dip, if falls to $X)
  // Advantage: don't need to monitor the market
  GoodTillCancel,
  // Referred to as Immediate-Or-Cancel
  // The order is executed immediately with whatever quantity is available at that moment
  // Any portion of the order that isn't filled immediately is cancelled altogether. (i.e. in a BID, but there's not enough ASKS to cover BID, then it is closed)
  // Advantage: Used when a trader wants to get as much of a position as possible right away without waiting for liquidity.
  FillAndKill,
  // The entire order MUST be filled immediately at the specific price
  // If the full amount is not immediately available, the entire order is cancelled.
  // Advantage: Ideal for LARGE trades to avoid partial execution.
  // FOK allows NO partial fills (It's ALL OR NOTHING) compared to FAK
  // FOK prioritizes total quantity; FAK prioritizes speed for whatever amount is available
  FillOrKill,
};

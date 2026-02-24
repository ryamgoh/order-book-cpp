#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <numeric>
#include <ostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>
enum class OrderType {
  GoodTillCancel,
  FillAndKill,
};

enum class Side {
  Buy,
  Sell,
};

// Aliases!
using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;

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

class Order {
  // Note: For any order, we want to keep track of 3 types of quantities
  // 1. Initial quanity
  // 2. Remaining quantity
  // 3. Quantity filled
public:
  Order(OrderType orderType, OrderId orderId, Side side, Price price,
        Quantity quantity)
      : orderType_{orderType}, orderId_{orderId}, side_{side}, price_{price},
        initialQuantity_{quantity}, remainingQuantity_{quantity} {};
  OrderType GetOrderType() const { return orderType_; }
  OrderId GetOrderId() const { return orderId_; }
  Side GetSide() const { return side_; }
  Price GetPrice() const { return price_; }
  Quantity GetInitialQuantity() const { return initialQuantity_; }
  Quantity GetRemainingQuantity() const { return remainingQuantity_; }
  Quantity GetFilledQuantity() const {
    return GetInitialQuantity() - GetRemainingQuantity();
  }
  bool IsFilled() const { return GetRemainingQuantity() == 0; }
  void Fill(Quantity quantity) {
    if (quantity > GetRemainingQuantity()) {
      throw std::logic_error(std::format(
          "Order ({}) cannot be filled for more than its remaining quanity.",
          GetOrderId()));

      remainingQuantity_ -= quantity;
    }
  }

private:
  OrderType orderType_;
  OrderId orderId_;
  Side side_;
  Price price_;
  Quantity initialQuantity_;
  Quantity remainingQuantity_;
};

using OrderPointer = std::shared_ptr<Order>;
using OrderPointers = std::list<OrderPointer>;

class OrderModify {
  // This is just a DTO for making changes to orders
public:
  OrderModify(OrderId orderId, Side side, Price price, Quantity quantity)
      : orderId_{orderId}, price_{price}, side_{side}, quantity_{quantity} {}

  OrderId GetOrderId() const { return orderId_; }
  Side GetSide() const { return side_; }
  Price GetPrice() const { return price_; }
  Quantity GetQuantity() const { return quantity_; }

  OrderPointer ToOrderPointer(OrderType type) const {
    return std::make_shared<Order>(type, GetOrderId(), GetSide(), GetPrice(),
                                   GetQuantity());
  }

private:
  OrderId orderId_;
  Side side_;
  Price price_;
  Quantity quantity_;
};

struct TradeInfo {
  OrderId orderId_;
  Price price_;
  Quantity quantity;
};

class Trade {
public:
  Trade(const TradeInfo &bidTrade, const TradeInfo &askTrade)
      : bidTrade_{bidTrade}, askTrade_{askTrade} {}

  const TradeInfo &GetBidTrade() const { return bidTrade_; }
  const TradeInfo &GetAskTrade() const { return askTrade_; }

private:
  TradeInfo bidTrade_;
  TradeInfo askTrade_;
};

using Trades = std::vector<Trade>;


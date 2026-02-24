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

class Orderbook {
private:
  struct OrderEntry {
    OrderPointer order_{nullptr};
    OrderPointers::iterator location_;
  };

  // When you iterate over a std::map, it always yields std::pair<const Key,
  // Value> elements.
  std::map<Price, OrderPointers, std::greater<Price>>
      bids_; // descending -> highest to lowest
  std::map<Price, OrderPointers, std::less<Price>>
      asks_; // ascending -> lowest to highest
  std::unordered_map<OrderId, OrderEntry> orders_;

  bool CanMatch(Side side, Price price) const {
    if (side == Side::Buy) {
      if (asks_.empty())
        return false;
      // The . (member access) and -> (member access through pointer) operators
      // have higher precedence than * (dereference) What You Thought
      // *asks_.begin()  →  (*asks_).begin()  // You thought this
      // What It Actually Is
      // *asks_.begin()  →  *(asks_.begin())  // Dereference the iterator
      const auto &[bestAsk, _] = *asks_.begin();
      // We are essentially asking for the lowest ask since Sellers are look to
      // make their prices higher, so the lowest price by the sellers is the
      // best price for the Buyers.
      return price >= bestAsk;
    } else {
      if (bids_.empty())
        return false;
      const auto &[bestBid, _] = *bids_.begin();
      return price <= bestBid;
    }
  };

  Trades MatchOrders() {
    Trades trades;
    trades.reserve(orders_.size()); // This prevents too much reallocation

    while (true) {
      if (bids_.empty() || asks_.empty())
        break;

      auto &[bidPrice, bids] = *bids_.begin();
      auto &[askPrice, asks] = *asks_.begin();

      if (bidPrice < askPrice)
        break;

      while (bids.size() && asks.size()) {
        auto &bid = bids.front();
        auto &ask = asks.front();

        Quantity quantity =
            std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());

        bid->Fill(quantity);
        ask->Fill(quantity);

        if (bid->IsFilled()) {
          bids.pop_front();
          orders_.erase(bid->GetOrderId());
        }
        if (ask->IsFilled()) {
          asks.pop_front();
          orders_.erase(ask->GetOrderId());
        }

        if (bids.empty()) {
          bids_.erase(bidPrice);
        }
        if (asks.empty()) {
          asks_.erase(askPrice);
        }

        trades.push_back(
            Trade{TradeInfo{bid->GetOrderId(), bid->GetPrice(), quantity},
                  TradeInfo{ask->GetOrderId(), ask->GetPrice(), quantity}});
      }
    }

    if (!bids_.empty()) {
      auto &[_, bids] = *bids_.begin();
      auto &order = bids.front();
      if (order->GetOrderType() == OrderType::FillAndKill)
        CancelOrder(order->GetOrderId());
    }

    if (!asks_.empty()) {
      auto &[_, asks] = *asks_.begin();
      auto &order = asks.front();
      if (order->GetOrderType() == OrderType::FillAndKill)
        CancelOrder(order->GetOrderId());
    }

    return trades;
  }

public:
  Trades AddOrder(OrderPointer order) {
    if (orders_.contains(order->GetOrderId())) {
      return {};
    }

    if (order->GetOrderType() == OrderType::FillAndKill &&
        !CanMatch(order->GetSide(), order->GetPrice())) {
      return {};
    }

    OrderPointers::iterator iterator;

    if (order->GetSide() == Side::Buy) {
      auto &orders = bids_[order->GetPrice()];
      orders.push_back(order);
      iterator = std::next(orders.begin(), orders.size() - 1);
    } else {
      auto &orders = asks_[order->GetPrice()];
      orders.push_back(order);
      iterator = std::next(orders.begin(), orders.size() - 1);
    }

    orders_.insert({order->GetOrderId(), OrderEntry{order, iterator}});

    return MatchOrders();
  }

  void CancelOrder(OrderId orderId) {
    if (!orders_.contains(orderId)) {
      return;
    }

    const auto &[order, iterator] = orders_.at(orderId);
    orders_.erase(orderId);

    if (order->GetSide() == Side::Sell) {
      // Remove it from the sell side
      auto price = order->GetPrice();
      auto &orders = asks_.at(price);
      orders.erase(iterator);
      if (orders.empty())
        asks_.erase(price);
    } else {
      // Remove it from the buy side
      auto price = order->GetPrice();
      auto &orders = bids_.at(price);
      orders.erase(iterator);
      if (orders.empty())
        bids_.erase(price);
    }
  }

  Trades MatchOrder(OrderModify order) {
    if (!orders_.contains(order.GetOrderId()))
      return {};

    const auto &[existingOrder, _] = orders_.at(order.GetOrderId());
    CancelOrder(order.GetOrderId());
    return AddOrder(order.ToOrderPointer(existingOrder->GetOrderType()));
  }

  std::size_t Size() const { return orders_.size(); }

  /**
   * The GetOrderInfos() method iterates through all price levels in both
   * the bids and asks sides, accumulating the total quantity at each price.
   */
  OrderbookLevelInfos GetOrderInfos() const {
    LevelInfos bidInfos, askInfos;
    bidInfos.reserve(orders_.size());
    askInfos.reserve(orders_.size());

    auto CreateLevelInfos = [](Price price, const OrderPointers &orders) {
      return LevelInfo{
          price,
          std::accumulate(orders.begin(), orders.end(), (Quantity)0,
                          [](Quantity runningSum, const OrderPointer &order) {
                            return runningSum + order->GetRemainingQuantity();
                          })};
    };

    // for (const std::pair<const Price, OrderPointers>& pair : bids_) {
    //     const Price& price = pair.first;
    //     OrderPointers& orders = pair.second;
    //     // ...
    // }
    for (const auto &[price, orders] : bids_)
      bidInfos.push_back(CreateLevelInfos(price, orders));

    for (const auto &[price, orders] : asks_)
      askInfos.push_back(CreateLevelInfos(price, orders));

    return OrderbookLevelInfos{bidInfos, askInfos};
  }
};

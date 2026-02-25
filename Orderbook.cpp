#include "LevelInfos.hpp"
#include "Order.hpp"
#include "OrderModify.hpp"
#include "OrderType.hpp"
#include "OrderbookLevelInfos.hpp"
#include "Trade.hpp"
#include "Usings.hpp"
#include <cstddef>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <numeric>
#include <ostream>
#include <unordered_map>

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

int main() {
  Orderbook orderbook;
  const OrderId orderId = 1;
  orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, orderId,
                                             Side::Buy, 100, 10));
  std::cout << orderbook.Size() << std::endl; // 1
  orderbook.CancelOrder(orderId);
  std::cout << orderbook.Size() << std::endl; // 0
  return 0;
}


#include "Order.hpp"
#include "OrderModify.hpp"
#include "OrderbookLevelInfos.hpp"
#include "Trade.hpp"
#include "Usings.hpp"
#include <map>
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

  bool CanMatch(Side side, Price price) const;
  Trades MatchOrders();

public:
  Orderbook();

  Trades AddOrder(OrderPointer order);
  void CancelOrder(OrderId orderId);
  Trades ModifyOrder(OrderModify order);

  std::size_t Size() const;
  OrderbookLevelInfos GetOrderInfos() const;
};

#include "Order.hpp"
#include "OrderModify.hpp"
#include "OrderbookLevelInfos.hpp"
#include "Trade.hpp"
#include "Usings.hpp"
#include <condition_variable>
#include <map>
#include <mutex>
#include <unordered_map>
class Orderbook {
private:
  struct OrderEntry {
    OrderPointer order_{nullptr};
    OrderPointers::iterator location_;
  };

  struct LevelData {
    Quantity quantity_{};
    Quantity count_{};

    enum class Action {
      Add,
      Remove,
      Match,
    };
  };

  std::unordered_map<Price, LevelData> data_;
  // When you iterate over a std::map, it always yields std::pair<const Key,
  // Value> elements.
  std::map<Price, OrderPointers, std::greater<Price>>
      bids_; // descending -> highest to lowest
  std::map<Price, OrderPointers, std::less<Price>>
      asks_; // ascending -> lowest to highest
  std::unordered_map<OrderId, OrderEntry> orders_;
  mutable std::mutex ordersMutex_;
  std::thread ordersPruneThread_;
  std::condition_variable shutdownConditionVariable_;
  std::atomic<bool> shutdown_{false};

  void PruneGoodForDayOrders();

  void CancelOrders(OrderIds orderIds);
  void CancelOrderInternal(OrderId orderId);

  void OnOrderCancelled(OrderPointer order);
  void OnOrderAdded(OrderPointer order);
  void OnOrderMatched(Price price, Quantity quantity, bool isFullyFilled);
  void UpdateLevelData(Price price, Quantity quantity,
                       LevelData::Action action);

  bool CanFullyFill(Side side, Price price, Quantity) const;
  bool CanMatch(Side side, Price price) const;
  Trades MatchOrders();

public:
  Orderbook();
  Orderbook(const Orderbook &) = delete;
  void operator=(const Orderbook &) = delete;
  Orderbook(Orderbook &&) = delete;
  void operator=(Orderbook &&) = delete;
  ~Orderbook();

  Trades AddOrder(OrderPointer order);
  void CancelOrder(OrderId orderId);
  Trades ModifyOrder(OrderModify order);

  std::size_t Size() const;
  OrderbookLevelInfos GetOrderInfos() const;
};

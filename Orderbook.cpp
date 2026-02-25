#include "Orderbook.hpp"
#include "OrderType.hpp"
#include "Usings.hpp"
#include <chrono>
#include <numeric>

// Private methods
/**
 * we want to remove the good for day orders at the end of the trading session
 * say 4pm
 */
void Orderbook::PruneGoodForDayOrders() {

  // This brings the std::chrono namespace into scope so you can use its
  // contents without typing std::chrono:: every time. Without it:
  // std::chrono::hours(16)
  // std::chrono::system_clock::now()
  // With it:
  // hours(16)
  // system_clock::now()
  using namespace std::chrono;
  const auto end = hours(16);

  while (true) {
    const auto now = system_clock::now();
    const auto now_c = system_clock::to_time_t(now);
    std::tm now_parts;
    localtime_r(&now_c, &now_parts);

    if (now_parts.tm_hour >= end.count())
      now_parts.tm_mday += 1;

    now_parts.tm_hour = end.count();
    now_parts.tm_min = 0;
    now_parts.tm_sec = 0;

    auto next = system_clock::from_time_t(mktime(&now_parts));
    auto till = next - now + milliseconds(100);

    {
      // In this step, we acquire the lock (other threads cannot use it)
      std::unique_lock ordersLock{ordersMutex_};

      // Then, we check if there's a shutdown flag called, OR that it is time.
      // If it isn't time yet, we essentially do a "wait_for" command for that
      // during until the time of closing (say 1h30m) After this, it "releases"
      // the lock and other threads can continue using this Then it is now 4pm,
      // this "reaquires" the lock, preventing others threads from using Orders.
      // Then it just goes on to prune all GoodForDay orders
      //
      // Caveat: wait_for is not the same as while(true) sleep(10). Its not
      // greedy on the CPU for this.
      if (shutdown_.load(std::memory_order_acquire) ||
          shutdownConditionVariable_.wait_for(ordersLock, till) ==
              std::cv_status::no_timeout)
        return;
    }

    OrderIds orderIds;

    {
      std::scoped_lock ordersLock{ordersMutex_};

      for (const auto &[_, entry] : orders_) {
        const auto &[order, _] = entry;

        if (order->GetOrderType() != OrderType::GoodForDay)
          continue;

        orderIds.push_back(order->GetOrderId());
      }
    }

    CancelOrders(orderIds);
  }
}

void Orderbook::CancelOrders(OrderIds orderIds) {
  std::scoped_lock ordersLock{ordersMutex_};

  for (const auto &orderId : orderIds) {
    CancelOrderInternal(orderId);
  }
}

/**
 * We created this CancelOrderInteral so we don't have to constantly acquire the
 * mutex, which can be pretty consuming on the CPU So this makes it much more
 * performant, when we only just acquire one for the duration of the batch
 */
void Orderbook::CancelOrderInternal(OrderId orderId) {
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

  OnOrderCancelled(order);
}

void Orderbook::OnOrderCancelled(OrderPointer order) {
  UpdateLevelData(order->GetPrice(), order->GetRemainingQuantity(),
                  LevelData::Action::Remove);
}

void Orderbook::OnOrderAdded(OrderPointer order) {
  UpdateLevelData(order->GetPrice(), order->GetInitialQuantity(),
                  LevelData::Action::Add);
}

void Orderbook::OnOrderMatched(Price price, Quantity quantity,
                               bool isFullyFilled) {
  UpdateLevelData(price, quantity,
                  isFullyFilled ? LevelData::Action::Remove
                                : LevelData::Action::Match);
}

/**
 * Update the book-keeping state
 */
void Orderbook::UpdateLevelData(Price price, Quantity quantity,
                                LevelData::Action action) {
  auto &data = data_[price];

  data.count_ += action == LevelData::Action::Remove ? -1
                 : action == LevelData::Action::Add  ? 1
                                                     : 0;
  if (action == LevelData::Action::Remove ||
      action == LevelData::Action::Match) {
    data.quantity_ -= quantity;
  } else {
    data.quantity_ += quantity;
  }

  if (data.count_ == 0)
    data_.erase(price);
}

bool Orderbook::CanFullyFill(Side side, Price price, Quantity quantity) const {
  if (!CanMatch(side, price))
    return false;

  std::optional<Price> threshold;

  if (side == Side::Buy) {
    const auto [askPrice, _] = *asks_.begin();
    threshold = askPrice;
  } else {
    const auto [bidPrice, _] = *bids_.begin();
    threshold = bidPrice;
  }

  for (const auto &[levelPrice, levelData] : data_) {
    if (threshold.has_value() &&
            (side == Side::Buy && threshold.value() > levelPrice) ||
        (side == Side::Sell && threshold.value() < levelPrice))
      continue;

    if ((side == Side::Buy && levelPrice > price) ||
        (side == Side::Sell && levelPrice < price))
      continue;

    if (quantity <= levelData.quantity_)
      return true;

    quantity -= levelData.quantity_;
  }

  return false;
}

bool Orderbook::CanMatch(Side side, Price price) const {
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
}

Trades Orderbook::MatchOrders() {
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

      OnOrderMatched(bid->GetPrice(), quantity, bid->IsFilled());
      OnOrderMatched(ask->GetPrice(), quantity, ask->IsFilled());
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

// Public Methods
Orderbook::Orderbook()
    : ordersPruneThread_{
          // [this] { PruneGoodForDayOrders(); } is an anonymous function
          [this] { PruneGoodForDayOrders(); }} {}

/**
 * This is a destructor function
 */
Orderbook::~Orderbook() {
  // sets a flag to tell the prune thread to stop 
  shutdown_.store(true, std::memory_order_release);
  // Wakes up the thread if its sleeping in "wait_for" 
  shutdownConditionVariable_.notify_one();
  ordersPruneThread_.join();
}

Trades Orderbook::AddOrder(OrderPointer order) {
  std::scoped_lock ordersLock{ordersMutex_};

  // If the orders already contains this specific order, we ignore
  if (orders_.contains(order->GetOrderId())) {
    return {};
  }

  if (order->GetOrderType() == OrderType::Market) {
    if (order->GetSide() == Side::Buy && !asks_.empty()) {
      const auto &[worstAsk, _] = *asks_.rbegin();
      order->ToGoodTillCancel(worstAsk);
    } else if (order->GetSide() == Side::Sell && !bids_.empty()) {
      const auto &[worstBid, _] = *bids_.rbegin();
      order->ToGoodTillCancel(worstBid);
    } else
      return {};
  }

  if (order->GetOrderType() == OrderType::FillAndKill &&
      !CanMatch(order->GetSide(), order->GetPrice()))
    return {};

  if (order->GetOrderType() == OrderType::FillOrKill &&
      !CanFullyFill(order->GetSide(), order->GetPrice(),
                    order->GetInitialQuantity()))
    return {};

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

  OnOrderAdded(order);

  return MatchOrders();
}

void Orderbook::CancelOrder(OrderId orderId) {
  std::scoped_lock ordersLock{ordersMutex_};

  CancelOrderInternal(orderId);
}

Trades Orderbook::ModifyOrder(OrderModify order) {
  OrderType orderType;

  // We acquire the mutex in this scope so that we don't reaquire the mutex in
  // CancelOrder and AddOrder. Both of them use the same mutex This could
  // potentially lead to a DEADLOCK.
  {
    std::scoped_lock ordersLock{ordersMutex_};

    if (!orders_.contains(order.GetOrderId()))
      return {};

    const auto &[existingOrder, _] = orders_.at(order.GetOrderId());
    orderType = existingOrder->GetOrderType();
  }

  CancelOrder(order.GetOrderId());
  return AddOrder(order.ToOrderPointer(orderType));
}

std::size_t Orderbook::Size() const {
  std::scoped_lock ordersLock{ordersMutex_};
  return orders_.size();
}

/**
 * The GetOrderInfos() method iterates through all price levels in both
 * the bids and asks sides, accumulating the total quantity at each price.
 */
OrderbookLevelInfos Orderbook::GetOrderInfos() const {
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

# Order Book

A high-performance C++ limit order book implementation for trading systems.

## What is an Order Book?

Imagine you're at a flea market. Some people want to **buy** items, some want to **sell**.

- **Buyers** (bids): They want to buy at the lowest price possible. They'll say "I'll pay $10" or "I'll pay $9" etc.
- **Sellers** (asks): They want to sell at the highest price possible. They'll say "I'll sell for $12" or "I'll sell for $11" etc.

An **order book** is simply a list that keeps track of all these buy and sell orders. When a buyer's price meets or exceeds a seller's price, a **trade** happens - the order gets matched!

### Key Concepts

#### Buy Orders (Bids)
A buy order says "I'm willing to buy at this price or lower." 
- Highest bids are matched first (best price for sellers)
- Stored in descending order: $100, $99, $98, ...

#### Sell Orders (Asks)
A sell order says "I'm willing to sell at this price or higher."
- Lowest asks are matched first (best price for buyers)
- Stored in ascending order: $100, $101, $102, ...

#### Matching
A trade occurs when:
```
highest_bid >= lowest_ask
```

For example:
- Best bid: $105 (someone willing to pay $105)
- Best ask: $100 (someone selling for $100)
- Match! Trade executes at $100-$105 (usually the maker's price)

## Core Data Types

### Side (Side.hpp)
The simplest type - just indicates whether an order is buying or selling:

```cpp
enum class Side {
  Buy,
  Sell,
};
```

### Price, Quantity, OrderId (Usings.hpp)
Type aliases for clarity and easy modification:

```cpp
using Price    = std::int32_t;   // Prices can be negative (not in this impl)
using Quantity = std::uint32_t;  // Always positive
using OrderId  = std::uint64_t;  // Unique identifier for each order
```

### OrderType (OrderType.hpp)
Five order types are supported:

```cpp
enum class OrderType {
  Market,        // Execute immediately at best available price
  GoodForDay,    // Valid until 4pm, then automatically cancelled
  GoodTillCancel,// Stays until explicitly cancelled
  FillAndKill,   // Get what's available NOW, cancel rest
  FillOrKill,    // Fill ENTIRE order or nothing
};
```

### Order (Order.hpp)
Represents a single order in the system:

```cpp
class Order {
  // Quantities tracked:
  // - initialQuantity_: Original order size
  // - remainingQuantity_: How much is left to fill
  
  Order(OrderType, OrderId, Side, Price, Quantity);
  
  OrderType GetOrderType() const;
  OrderId   GetOrderId() const;
  Side      GetSide() const;
  Price     GetPrice() const;
  Quantity  GetInitialQuantity() const;
  Quantity  GetRemainingQuantity() const;
  Quantity  GetFilledQuantity() const;    // initial - remaining
  
  bool IsFilled() const;                  // remaining == 0
  void Fill(Quantity);                    // Reduce remaining quantity
  void ToGoodTillCancel(Price);           // Convert market to GTC
};
```

### OrderModify (OrderModify.hpp)
A **Data Transfer Object (DTO)** for modifying existing orders. Instead of passing individual parameters, we bundle them:

```cpp
class OrderModify {
  OrderModify(OrderId orderId, Side side, Price price, Quantity quantity);
  
  OrderId   GetOrderId() const;
  Side      GetSide() const;
  Price     GetPrice() const;
  Quantity  GetQuantity() const;
  
  // Convert to a new Order with the same OrderId but new parameters
  OrderPointer ToOrderPointer(OrderType type) const;
};
```

## Order Types Explained

### Market Order
```
"I want to buy NOW at whatever price!"
```
- Executes immediately against the best available price
- Gets converted internally to a GoodTillCancel with the market price
- Always guarantees execution, but price is uncertain

### Good Till Cancel (GTC)
```
"I'll wait for a match at this price until I cancel"
```
- Stays in the book indefinitely
- Gets matched when a counterparty's order crosses your price
- Most common order type

### Fill and Kill (FAK)
```
"Fill what you can NOW, cancel the rest"
```
- Tries to match immediately against existing orders
- Any unfilled portion is cancelled right away
- Useful for getting immediate liquidity

### Fill or Kill (FOK)
```
"Fill my ENTIRE order NOW, or cancel everything"
```
- Must fill ALL quantity immediately
- Either full execution or nothing
- Used for large orders where partial fills are undesirable

### Good for Day (GFD)
```
"This order is only valid until 4pm today"
```
- Automatically cancelled at end of trading day
- Background thread (`ordersPruneThread_`) handles this

## Data Structures

### LevelInfo (LevelInfos.hpp)
**Aggregated** view of orders at each price level:

```cpp
struct LevelInfo {
  Price price_;      // The price level (e.g., $100)
  Quantity quantity_; // Total quantity at this price
};
```

Why aggregate? Instead of showing 50 individual orders at $100, we show one level: "$100: 500 shares".

Example:
```
3 orders: 20 @ $50, 30 @ $50, 10 @ $50
Aggregated: LevelInfo{price: 50, quantity: 60}
```

### LevelInfos
A vector of LevelInfo - represents all price levels:

```cpp
using LevelInfos = std::vector<LevelInfo>;
```

### OrderbookLevelInfos (OrderbookLevelInfos.hpp)
Public-facing snapshot of the entire order book:

```cpp
class OrderbookLevelInfos {
  const LevelInfos& GetBids() const;  // All buy levels
  const LevelInfos& GetAsks() const;  // All sell levels
};
```

### TradeInfo (TradeInfo.hpp)
Details of one side of a trade:

```cpp
struct TradeInfo {
  OrderId orderId_;   // Which order was filled
  Price price_;       // Execution price
  Quantity quantity; // How much was filled
};
```

### Trade (Trade.hpp)
A **matched pair** - every trade has a buyer and seller:

```cpp
class Trade {
  const TradeInfo& GetBidTrade() const;  // Buyer's side
  const TradeInfo& GetAskTrade() const;  // Seller's side
};

using Trades = std::vector<Trade>;  // Multiple trades from one order
```

## Internal Architecture

### Core Data Structures

```
┌─────────────────────────────────────────────────────────────────┐
│                      Orderbook                                   │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  BIDS (Buyers)              │  ASKS (Sellers)                  │
│  ┌──────────────────┐       │  ┌──────────────────┐           │
│  │ $105: [order1]   │       │  │ $100: [order2]   │           │
│  │ $104: [order3]   │       │  │ $101: [order4]   │           │
│  │ $103: [order5]   │       │  │ $102: [order5]   │           │
│  └──────────────────┘       │  └──────────────────┘           │
│         (descending)        │         (ascending)               │
│                                                                  │
├─────────────────────────────────────────────────────────────────┤
│  orders_ (by ID)                                                │
│  ┌──────────────┬────────────────────────────────────────┐     │
│  │ OrderID      │ Order + Iterator into bids/asks        │     │
│  │ 1            │ order1 + iterator to $105 in bids_     │     │
│  │ 2            │ order2 + iterator to $100 in asks_    │     │
│  └──────────────┴────────────────────────────────────────┘     │
│                                                                  │
├─────────────────────────────────────────────────────────────────┤
│  data_ (price level summary)                                    │
│  ┌──────────────┬─────────────────────────────┐                │
│  │ Price       │ LevelData {quantity, count} │                │
│  │ $100        │ {quantity: 100, count: 5}   │                │
│  │ $101        │ {quantity: 50, count: 2}    │                │
│  └──────────────┴─────────────────────────────┘                │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Maps vs Hash Maps

- **`bids_` / `asks_`**: `std::map` 
  - Sorted by price
  - O(log n) insertion/lookup
  - Need to find best price quickly
  
- **`orders_`**: `std::unordered_map`
  - By OrderId
  - O(1) lookup
  - Need to find specific order quickly

### Thread Safety

```
┌─────────────────────────────────────────────────────────────────┐
│                     Thread Safety                                │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ordersMutex_ ───────────────────────────────────────────────    │
│      │                                                          │
│      ├── AddOrder() ──────────────────────────────────────      │
│      │    1. Check if order exists                              │
│      │    2. Validate order type                                │
│      │    3. Insert into bids_/asks_                            │
│      │    4. Insert into orders_                                │
│      │    5. Match orders                                       │
│      │                                                          │
│      ├── CancelOrder() ────────────────────────────────────      │
│      │    1. Find order in orders_                              │
│      │    2. Remove from bids_/asks_                            │
│      │    3. Erase from orders_                                 │
│      │                                                          │
│      ├── ModifyOrder() ─────────────────────────────────────     │
│      │    1. Get order details (with lock)                      │
│      │    2. Cancel (with lock)                                 │
│      │    3. Add new order (with lock)                          │
│      │                                                          │
│  Background Thread ──────────────────────────────────────────    │
│      │                                                          │
│      └── PruneGoodForDayOrders()                               │
│           - Waits until 4pm                                     │
│           - Cancels all GFD orders                              │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Matching Algorithm

```cpp
while (bids not empty AND asks not empty) {
    best_bid = highest bid price
    best_ask = lowest ask price
    
    if (best_bid < best_ask) 
        break;  // No more matches possible
    
    // Match at the best ask price (maker's price)
    while (both sides have orders) {
        trade_quantity = min(bid.remaining, ask.remaining)
        
        bid.Fill(trade_quantity)
        ask.Fill(trade_quantity)
        
        if (bid.IsFilled()) remove from book
        if (ask.IsFilled()) remove from book
        
        record Trade
    }
}
```

## Public API

```cpp
class Orderbook {
  // Add new order - returns trades if matched
  Trades AddOrder(OrderPointer order);
  
  // Remove order from book
  void CancelOrder(OrderId orderId);
  
  // Cancel and replace with new parameters
  Trades ModifyOrder(OrderModify order);
  
  // Query current state
  std::size_t Size() const;              // Total orders
  OrderbookLevelInfos GetOrderInfos() const; // Bids + Asks
};
```

## Project Structure

```
order-book-cpp/
├── Orderbook.hpp/.cpp      # Main order book implementation
├── Order.hpp                # Order data structure
├── OrderModify.hpp          # Order modification DTO
├── OrderType.hpp            # Order type enum (Market, GTC, etc.)
├── Side.hpp                 # Buy/Sell enum
├── Trade.hpp                # Trade (matched pair)
├── TradeInfo.hpp            # One side of a trade
├── LevelInfos.hpp           # Aggregated price levels
├── OrderbookLevelInfos.hpp  # Full book snapshot
├── Usings.hpp              # Type aliases (Price, Quantity, OrderId)
├── Constants.hpp           # Constants (invalid price)
├── main.cpp                # Example usage
├── tests/
│   ├── _test.cpp           # Google Test unit tests
│   └── TestFiles/          # Test data files
└── CMakeLists.txt          # Build configuration
```

## Building and Testing

```bash
mkdir build && cd build
cmake ..
cmake --build .
ctest  # Run all tests
```

Or run tests directly:
```bash
./my_tests
```

## Running Individual Tests

```bash
./my_tests --gtest_filter="OrderbookTests.Match_GoodTillCancel"
```

## Test File Format

Test files in `TestFiles/` define order sequences:

```
A B GoodTillCancel 100 10 1    # Add: Buy, GTC, $100, 10 qty, ID 1
A S GoodTillCancel 100 10 2    # Add: Sell, GTC, $100, 10 qty, ID 2
R 0 0 0                        # Result: 0 bids, 0 asks (fully matched)
```

Format:
- `A <Side> <OrderType> <Price> <Quantity> <OrderId>` - Add order
- `M <OrderId> <Side> <Price> <Quantity>` - Modify order
- `C <OrderId>` - Cancel order
- `R <total_orders> <bids_count> <asks_count>` - Expected result

Example test files:
- `Match_GoodTillCancel.txt` - Basic matching
- `Match_FillAndKill.txt` - Immediate or cancel
- `Match_FillOrKill_Hit.txt` - Full fill
- `Match_FillOrKill_Miss.txt` - No fill possible
- `Cancel_Success.txt` - Cancelling orders
- `Modify_Side.txt` - Modifying orders
- `Match_Market.txt` - Market orders

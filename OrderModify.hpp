#pragma once

#include "Order.hpp"

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


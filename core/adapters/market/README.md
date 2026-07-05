# Finite limit-order execution

This adapter uses share subunits as the core payload and integer cents as an
adapter-owned price attribute.

An adapter-owned smart order router sweeps two ask levels for a 6,000-share
buy with a limit price of 102 cents:

| Level | Available | Filled | Price | Cash |
|---|---:|---:|---:|---:|
| 1 | 3,000 | 3,000 | 100 cents | 300,000 cents |
| 2 | 4,000 | 3,000 | 102 cents | 306,000 cents |

The total fill is 6,000 shares for 606,000 cents, producing an exact integer
VWAP of 101 cents. The adapter retains the unsubmitted remainder between two
fresh closed transactions. The core does not discover or sweep price levels.

A true fill-or-kill order requests 5,000 shares with a 101-cent limit. Only
the 3,000-share first level is eligible, so the adapter cancels the whole
order before core execution.

A failed-matcher transaction blindly submits 6,000 shares to the 3,000-share
first level. The core accepts 3,000 and records 3,000 in external exhaust.
Those units are labelled `cancelled_invalid_request`; they are not capital and
do not remain as a live order.

This adapter is not a matching engine, exchange, price-discovery system, or
market simulator. It does not implement order identity, price-time priority,
fees, settlement, concurrency, or floating-point prices.

Run the adapter and renderer:

```bash
./build/tensorless_limit_order_validator \
  > core/artifacts/limit_order_trace.jsonl
python3 core/adapters/market/render_trace.py
```

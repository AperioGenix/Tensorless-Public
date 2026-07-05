# Switch fabric adapter

Two adapter-owned FIFO queues receive deterministic bursts. Every even tick
adds five tokens to the x queue and three to the y queue. A fresh Tensorless
transaction arbitrates at most five tokens per tick through a shared sink.
Peak arrival is eight tokens, while mean arrival is four tokens per tick.

Separate momentum channels report each queue's accepted and rejected share.
The adapter checks those shares against an independent largest-remainder
oracle, preserves FIFO order, and runs the workload twice to check replay.

Queue length is observed after arrival and before service. A token served on
its arrival tick has a one-tick system time. Over 20 ticks:

```text
arrivals = departures = 80
sum(queue length) = 110
sum(system time) = 110

L = 110 / 20
lambda = 80 / 20
W = 110 / 80
L = lambda * W
```

The FIFO queues, timestamps, and five-token service policy belong to the
adapter. Tensorless performs each capacity-limited arbitration and records
rejected payload. The core does not provide packet identity, queue storage, or
a network bandwidth parameter.

The executable writes one queue and arbitration frame per tick followed by a
summary. Render the captured workload from the repository root:

```bash
python3 core/adapters/network/render_trace.py
```

The default input is `core/artifacts/switch_trace.jsonl`.

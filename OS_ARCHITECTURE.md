# OS/runtime architecture choices

## 1. Worker threads

A fixed `boost::asio::thread_pool` processes accepted connections. This avoids the cost and scheduling overhead of creating a fresh thread for every judge request.

Default:

```text
32 worker threads
```

Increase only after measuring the selected LLM provider. More threads do not magically make one model call faster.

## 2. Semaphore for model concurrency

The worker pool can handle more requests than the model provider should receive simultaneously. A C++20 counting semaphore caps concurrent LLM calls, defaulting to four.

This protects:

- API rate limits
- process memory
- model latency under bursts
- provider connection pressure

The model call is the slowest external operation, so limiting it separately is more useful than blindly increasing worker count.

## 3. Bounded backpressure

A second counting semaphore limits accepted-but-not-yet-completed work. When the service is saturated, the listener returns HTTP 503 instead of accumulating an unlimited queue and eventually taking the entire process down.

## 4. LRU cache

The interpretation cache is keyed by the notes plus the battery capacity and base reserve information that can change the meaning of percentage language. Only validated LLM output is allowed to survive as reusable interpretation state.

This helps when the judge repeats the same scenario or when clients retry the same request after a transient network failure.

## 5. Keep-alive

HTTP/1.1 keep-alive avoids reconnecting for every request. That matters when the judge sends many cases to one public endpoint.

## 6. Scheduling/optimization

The actual scheduling intelligence is a linear-programming model over 24 hours. The service does not use FIFO/priority scheduling for energy decisions because those are OS scheduling algorithms and do not solve the energy-cost minimization problem.

The LP uses:

- grid import variables
- solar usage variables
- charge/discharge variables
- battery state variables
- equality constraints for energy/state transitions
- hard bounds for every operator directive
- an objective equal to total grid electricity cost

This separation keeps OS-level scheduling of requests independent from energy-level scheduling of the campus.
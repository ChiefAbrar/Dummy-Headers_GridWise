# GridWise Preliminary C++20 API Solution

A production-style C++20 implementation for the BUP CSE Fest 2026 GridWise LLM-assisted preliminary.

The required product is an HTTP API, not a website. The service exposes exactly the judging endpoints:

- `GET /health`
- `POST /optimize-energy`

The pipeline is:

```text
HTTP request
    |
    v
Strict request validation
    |
    v
LLM interprets every operator note
    |
    v
Deterministic guardrails
    |
    v
24-hour Linear Program (simplex)
    |
    v
Independent replay validator
    |
    v
Machine-checkable JSON response
```

## Why this design

The challenge separates language understanding from mathematical correctness. The LLM is used only where natural language is required. Its structured result is treated as untrusted data, validated deterministically, and only then turned into optimization constraints.

The optimizer is a continuous LP because the challenge's battery equations depend on charge and discharge only through their difference. If a feasible LP solution has both positive charge and discharge in the same hour, subtracting `min(charge, discharge)` from both leaves every equation and every battery state unchanged. Therefore an optimal LP solution can always be normalized into the challenge's required single-action representation without changing cost.

The service deliberately does not use a heavyweight generic scheduling framework. It has a tiny 24-hour model, so a dense simplex implementation is faster to initialize, simpler to ship, and removes a runtime solver dependency.

## OS/runtime engineering

The service uses ordinary operating-system techniques where they actually help:

- Fixed C++ worker thread pool instead of one process/thread per request.
- A `std::counting_semaphore` limits concurrent LLM calls so a burst of judge requests cannot exhaust model quotas or memory.
- A bounded pending-connection limiter applies backpressure instead of allowing an unbounded request queue.
- A mutex-protected LRU cache reuses previously validated LLM interpretations for identical semantic inputs.
- HTTP connections support keep-alive.
- The solver works on contiguous small arrays/vectors and performs no filesystem or database I/O during a request.
- Forking is intentionally avoided. In a containerized single-service deployment it adds process-management complexity without improving this workload.

## Project layout

```text
include/             Public/internal C++ headers
src/                 HTTP server, LLM client, validation, optimizer, replay
scripts/              Local test helpers
config/               Example configuration
 tests/               Core tests and public sample fixtures
Dockerfile             Reproducible deployment image
CMakeLists.txt         Build definition
VIDEO_SCRIPT.md        3-minute submission video script
```

## Install on Linux / WSL2

Ubuntu 24.04 or another recent Debian-family Linux is the simplest development environment.

```bash
sudo apt update
sudo apt install -y build-essential cmake libboost-all-dev libcurl4-openssl-dev ca-certificates
```

Then:

```bash
git clone <your-private-repo-url>
cd gridwise_cpp_solution
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The build produces:

```text
build/gridwise_server
build/gridwise_tests
```

For Windows, WSL2 + Ubuntu is strongly recommended for the event because the deployment environment is Linux/Docker anyway. Native MSVC/vcpkg builds are possible but add dependency-management noise during a four-hour competition, which is exactly the sort of thing competitions enjoy doing to humans.

## Environment variables

Copy the example configuration:

```bash
cp config/example.env .env
```

Then export the values. The program reads environment variables directly, so do not commit `.env`.

Required:

```text
LLM_MODEL                 Model identifier.
```

```text
LLM_BASE_URL=___
LLM_CHAT_PATH=___
LLM_API_KEY=___
LLM_MODEL=____
```

Tuning variables:

```text
PORT=8080
GRIDWISE_HOST=0.0.0.0
GRIDWISE_WORKERS=32
GRIDWISE_MAX_PENDING=64
GRIDWISE_LLM_CONCURRENCY=4
GRIDWISE_LLM_CACHE_ENTRIES=256
LLM_TIMEOUT_MS=4500
LLM_CONNECT_TIMEOUT_MS=1200
LLM_RETRIES=2
```

The low default LLM retry count is intentional because the rubric gives the best latency credit at p95 <= 5 seconds. Raise retries only after benchmarking the selected provider.

## Run locally

```bash
export $(grep -v '^#' .env | xargs)
build/gridwise_server
```

Expected startup log:

```text
GridWise C++ listening on 0.0.0.0:8080 with 32 workers
```

Health check:

```bash
curl -sS http://127.0.0.1:8080/health
```

Expected:

```json
{"status":"ok"}
```

## Test the optimizer against all public cases

This test bypasses the LLM deliberately and feeds the organizer-published reference interpretations into the deterministic optimizer. It checks every public scenario's optimal cost and independently replays the resulting plan.

```bash
./build/gridwise_tests tests/fixtures_public_samples.json
```

Expected result from this solution:

```text
PASS SAMPLE-01 cost=38365
PASS SAMPLE-02 cost=42885
PASS SAMPLE-03 cost=35480
PASS SAMPLE-04 cost=40495
PASS SAMPLE-05 cost=33950
PASS SAMPLE-06 cost=34090
PASS SAMPLE-07 cost=38550
PASS SAMPLE-08 cost=37665
PASS SAMPLE-09 cost=34873
PASS SAMPLE-10 cost=41620
ALL PUBLIC CASES PASS: 10/10
```

These are public examples only. The hidden judge must still see a real generative-model interpretation path.

## Full local end-to-end test without an external model

For development, `scripts/fake_llm.py` is a fake OpenAI-compatible server. It exists only to exercise the real production HTTP/LLM/guardrail/optimizer/replay pipeline locally.

Terminal 1:

```bash
python3 scripts/fake_llm.py
```

Terminal 2:

```bash
export LLM_BASE_URL=http://127.0.0.1:19090
export LLM_CHAT_PATH=/chat/completions
export LLM_MODEL=test-model
export PORT=8080
build/gridwise_server
```

Terminal 3:

```bash
curl -sS \
  -X POST http://127.0.0.1:8080/optimize-energy \
  -H 'Content-Type: application/json' \
  --data-binary @tests/sample_request.json | tee /tmp/gridwise-response.json
```

The end-to-end test should report SAMPLE-01 with `total_cost_bdt` equal to `38365` within the challenge tolerance.

The fake LLM is never used by the deployed service and is not part of the judging path.

## How to check whether a returned schedule is actually correct

Do not judge correctness by `total_cost_bdt` alone. The challenge explicitly scores the whole pipeline.

For every response, replay the 24 rows in order and check:

```text
grid_kwh + solar_used_kwh + battery_discharge_kwh
    = demand_kwh + battery_charge_kwh
```

Then verify:

1. Solar never exceeds effective solar after `solar_reduction`.
2. Battery stays between its active minimum reserve and capacity.
3. Charge/discharge rates are respected.
4. `no_charge_window` and `no_discharge_window` are obeyed.
5. `max_grid_window` caps are obeyed.
6. The final battery energy exactly returns to the initial energy within tolerance.
7. `total_grid_kwh`, `total_cost_bdt`, and `peak_grid_kwh` recalculate from the 24 rows.
8. Every operator note has exactly one interpretation entry in order.
9. `no_op` is the only interpretation with `applies=false`.

The service performs these checks itself after optimization. A solver result is rejected internally if replay validation fails.

## Real submission test

After configuring the real LLM:

```bash
curl -f http://YOUR_PUBLIC_BASE_URL/health
```

Then:

```bash
curl -f \
  -X POST https://YOUR_PUBLIC_BASE_URL/optimize-energy \
  -H 'Content-Type: application/json' \
  --data-binary @tests/sample_request.json \
  -o response.json
```

Inspect:

```bash
cat response.json
```

Run the service repeatedly. The event requires repeated hidden LLM-backed requests to remain reachable and the `/optimize-energy` path to finish within the judging timeout.

## Docker

Build:

```bash
docker build -t gridwise-cpp:1.0.0 .
```

Run:

```bash
docker run --rm -p 8080:8080 \
  -e LLM_BASE_URL='https://api.openai.com/v1' \
  -e LLM_CHAT_PATH='/chat/completions' \
  -e LLM_API_KEY="$LLM_API_KEY" \
  -e LLM_MODEL="$LLM_MODEL" \
  gridwise-cpp:1.0.0
```

Then:

```bash
curl http://127.0.0.1:8080/health
```

Do not place credentials in the Dockerfile or image layers.

## Deployment recommendations

Use any public platform that gives the judge a direct HTTP URL. The container listens on `0.0.0.0` and honors `PORT`.

Recommended architecture:

```text
Public URL
    |
    v
Container / GridWise C++
    |
    +--> bounded worker threads
    |
    +--> LLM provider
    |
    +--> deterministic directives
    |
    +--> LP simplex
    |
    +--> replay validator
```

No database is necessary.

## LLM prompt design

The system prompt tells the model the complete allowed directive vocabulary and the key semantic rules:

- 1–3 notes
- one interpretation per note
- start-inclusive/end-exclusive hours
- factor means usable fraction remaining
- percentage battery reserves use the supplied battery capacity
- irrelevant notes become `no_op`
- no invented demand/tariff/battery values

The model is asked for JSON only. The C++ service still treats that result as untrusted and validates it before optimization.

## Deterministic guardrails

The guardrail layer checks:

- exactly one mapping per note
- note indices 0..N-1 in order
- allowed directive types only
- correct `applies` semantics
- integer, unique, sorted hours 0..23
- solar factor in [0,1]
- reserve within battery capacity
- non-negative grid caps
- required structured-adjustment shape

Bad model output does not become an optimization constraint silently.

## Optimization model

Variables for each hour `h`:

```text
g[h]  grid import
s[h]  solar used
c[h]  battery charge
d[h]  battery discharge
e[h]  battery energy after the hour
```

Objective:

```text
minimize sum_h g[h] * tariff[h]
```

Constraints include:

```text
g + s + d = demand + c

e[h] = e[h-1] + c[h] - d[h]
0 <= s <= effective_solar
0 <= c <= max_charge
0 <= d <= max_discharge
active_minimum <= e <= capacity
final e[23] = initial_energy
```

Directive constraints are added directly before solving.

## Complexity

The optimization horizon is fixed at 24 hours. The model has 120 continuous variables and a few hundred linear constraints, so the local solve is tiny relative to the LLM network call.

That is why the solution prioritizes deterministic optimization and spends engineering effort on LLM latency, caching, concurrency control, and replay validation.

## Security / secrets

Never commit:

- `LLM_API_KEY`
- `.env`
- provider tokens
- passwords
- raw secret-bearing prompts

The service deliberately returns generic internal errors rather than model-provider responses or exception traces.
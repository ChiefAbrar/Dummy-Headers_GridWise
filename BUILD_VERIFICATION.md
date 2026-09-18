# Build verification performed for this package

Environment used for verification:

- GNU C++ 14.2
- CMake 4.x
- Boost 1.83 system headers
- system shared libcurl
- Linux x86-64

Checks completed:

1. Release C++20 build succeeded with LTO enabled.
2. `gridwise_tests` built successfully.
3. All 10 public cases passed independently with their published optimal total cost:
   - SAMPLE-01: 38365 BDT
   - SAMPLE-02: 42885 BDT
   - SAMPLE-03: 35480 BDT
   - SAMPLE-04: 40495 BDT
   - SAMPLE-05: 33950 BDT
   - SAMPLE-06: 34090 BDT
   - SAMPLE-07: 38550 BDT
   - SAMPLE-08: 37665 BDT
   - SAMPLE-09: 34873 BDT
   - SAMPLE-10: 41620 BDT
4. `/health` was exercised over the real HTTP server and returned the required response.
5. A full end-to-end HTTP test was run using the included fake OpenAI-compatible LLM server. SAMPLE-01 passed through HTTP parsing, LLM response parsing, deterministic guardrails, optimizer, replay validation, and response serialization, with total cost 38365 BDT.
6. A source scan found no obvious API-key/private-key patterns in the submitted source material.

A local AddressSanitizer/UBSan build was attempted, but compilation through the system Boost.JSON headers exceeded the execution window because of very large compiler diagnostics. This is a verification limitation of the build environment, not a successful sanitizer run, and should not be represented as one.
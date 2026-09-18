# GridWise submission checklist

## API

- [ ] `GET /health` is public and returns HTTP 200 with `{"status":"ok"}`.
- [ ] `POST /optimize-energy` is public and accepts the exact challenge JSON.
- [ ] No login, VPN, manual approval, or dashboard is required.
- [ ] The public URL works from a machine outside the development network.

## LLM

- [ ] `LLM_MODEL` is configured with the actual model used for judging.
- [ ] The provider is reachable for the entire judging window.
- [ ] The model supports JSON structured output or equivalent JSON-mode behavior.
- [ ] The LLM directly interprets `operator_notes`.
- [ ] No hard-coded phrase matcher is used as the production interpreter.

## Guardrails

- [ ] Exactly one interpretation per note.
- [ ] Note indices are ordered `0..N-1`.
- [ ] `no_op` uses `applies=false` and `structured_adjustment=null`.
- [ ] Non-`no_op` directives use `applies=true`.
- [ ] Hours are unique, integer, sorted, and in `0..23`.
- [ ] Numeric ranges are validated before optimization.

## Optimization/replay

- [ ] Every public case passes `./build/gridwise_tests tests/fixtures_public_samples.json`.
- [ ] Final plans satisfy energy balance.
- [ ] Battery bounds/rates and directives are replay-checked.
- [ ] Final battery energy equals initial energy.
- [ ] Reported totals are recalculated from the returned plan.

## Performance

- [ ] Measure real-provider p95 latency with the exact deployed model.
- [ ] Keep p95 at or below 5 seconds when possible.
- [ ] Keep per-request completion well below 30 seconds.
- [ ] Verify repeated requests do not cause 5xx failures.

## Docker/repository

- [ ] Docker image is pullable under the exact submitted tag/digest.
- [ ] The image contains no API keys.
- [ ] Container listens on `0.0.0.0`.
- [ ] `PORT` is documented.
- [ ] README has a clean-machine quickstart.
- [ ] README names the model/provider, environment variables, optimizer, and guardrails.
- [ ] Repository follows the event's private-during-round/public-after-deadline rule.

## Video

- [ ] Maximum 3:00.
- [ ] Explains LLM -> guardrails -> optimizer -> replay.
- [ ] Explains how judges can run the service.
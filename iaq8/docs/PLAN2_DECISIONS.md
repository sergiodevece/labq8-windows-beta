# IAQ8 Plan 2: decisiones y pendientes

Registro de la ejecución del Plan 2 (docs/superpowers/plans/2026-11-25-iaq8-plan-2-servidor.md): servidor en /Users/serx/Documents/VITAPLUG/vitaplug-assist-server (rama main) y, en este repo, iaq8_render y la prueba E2E. **Nota:** la Ruling (I2) sustituye las líneas del plan que pedían 422 por más de 200 mensajes o textos de más de 4000 caracteres: el servidor recorta la transcripción y el plugin limita lo que envía.

## Decisiones (en orden)

- Ruling R1: T2 creates `src/usage.ts` with `export type Usage = { input: number; output: number; cacheRead: number; cacheWrite: number }` and defines `TurnRecord` (fields exactly as in T4's Interfaces) in `src/store.ts`; T3 imports Usage from usage.ts; T4 consumes TurnRecord — cost if wrong: trivial move.
- Ruling R2: T4 adds `lastProposalBands (sessionId: string): Promise<Band[] | null>` to Store, MemoryStore and D1Store (reads the latest kind='proposal' reply_json of the session) — cost if wrong: one extra query.
- Ruling R3: eval/run.ts takes `--render <path to iaq8_render>` (default `../LABQ8/build/iaq8-debug/iaq8_render_artefacts/Debug/iaq8_render` if it exists) and `--analyze <path>`; audio steps are skipped with a note when absent — cost if wrong: none.
- Ruling R4: T7's opt-in e2e test (IAQ8_E2E_URL) passes on a valid Question/Proposal OR on ErrorKind::unavailable when the server has no API key; it records which happened — proves transport + contract path without a key — cost if wrong: weaker e2e until the author deploys.
- Ruling (author request 2026-11-25): eval cases from Apple Loops (/Library/Audio/Apple Loops, .caf) until the author is in the studio — T6 adds eval/make-cases.ts: pick a loop, loop/trim to ~15 s WAV in eval/audio (gitignored, never committed/redistributed), optionally apply a known 'damage' EQ via iaq8_render so the case has ground truth (expected candidate + correction direction); undamaged loops test 'don't invent strong changes'. Voice is scarce in the installed library — suggest Logic Sound Library downloads or a home voice recording — cost if wrong: less realistic cases than beginner recordings
- Ruling (carry to T4): every recorded turn counts toward quota, including kind=error (503) turns — they may have consumed tokens and a failing path must not be hammered for free; 401/429 rejections are never recorded. D1Store must implement hasSessionSince — cost if wrong: testers lose a few turns on server faults
- Ruling: T5/T6 eval criterion `targets_candidates` exempts low_rumble from the 1/6-oct rule (low_cut placed above the peak near trend.fit_from_hz) — cost if wrong: none
- Ruling: fold I1 into Task 4 (touches model.ts failure variant: optional parts) — cost if wrong: none
- Ruling: server re-injects the session's last stored analysis (same code+session, from request_json) when a request has analysis null — the plugin's "analysis only after each listening" saves bytes but the model must keep seeing it; fold into Task 6's fix round — cost if wrong: slightly larger prompts per turn
- Ruling (I2): relax the plan's 422-on-limits for transcript length — server keeps the 256 KB byte guard but trims the transcript for the model (last 190 messages; per-message text truncated to 4000 chars) instead of rejecting; plugin also caps input/source-note to 4000 chars and sends only the last 190 messages — spec's reasonable-user expectation outranks the plan's mandated 422 — cost if wrong: very old turns not seen by the model
- Ruling (I4): cap serialized analysis at 16 KB (else 422) and add a per-code monthly USD cap (default 5 USD, column max_usd_month, new migration 0002) returning 429
- Ruling: one fix wave = I1–I5 + M1, M2, M3, M4, M5, M6, M7/M8 docs, M9 log, M10 engines, appliedPrevious tolerance at the plugin's quantisation
- Parked — 256 KB guard sticky in extreme sessions; no omission marker (plugin pre-trims); USD cap soft overshoot (~$2) and timeouts at $0; null-model iterations priced dearest may reach the cap sooner; battery echoes unsanitised bands; plan lines 94–95 superseded by Ruling (I2) — Ruling: acceptable for the beta; revisit after first deploy (compare cost_usd with the console) — cost if wrong: small cap/accuracy drift

## Pendientes menores aplazados

- Task 1: minor (deferred): depth ≤32 not enforced (shape fixed); lone surrogates; trim whitespace set vs JUCE; `as TurnRequest` cast (use z.infer); redundant finite refine; plugin_id unchecked (registry in T3/T4)
- Task 2: minor (deferred): concurrent overshoot of per-minute cap (document); extra count queries per request; shell-expansion caveat in issue-code hint; no exact 60 000 ms / Dec→Jan tests; migration never executed (T4 runs it via wrangler local); unused _h
- Task 3: minor (deferred): question converted from empty proposal keeps proposal-style quick replies; opus-5/4.8 prices + cache multipliers "verify"
- Task 4: minor (deferred): appliedPrevious positional + padded bands may under-report if plugin quantisation alters values >1e-3 (note for eval); non-atomic quota; placeholder database_id (T7)
- Task 4: minor (deferred): access-failure test doesn't assert the log; serializeReply-throw path not deterministic; tests need Node ≥22.5 (node:sqlite)
- Task 5: minor (deferred): partial output on second write failure; usage text on every error; extra channels counted in loudness (tool passes ≤2)
- Task 5: minor (deferred): +6 dB clamp test input at -40 dB near silence gate margin; silenceMeanSquare literal not tied to silenceDb
- Task 6: minor (deferred): contract_ok nearly circular; wav fmt size validation; loop seam click
- Task 6: fix round 1 DONE 994f7b0. Product finding (Plan 1 analyzer): a broad +6 dB low-shelf boom at 120 Hz on bass yields no candidate — note for analyzer tuning with real material
- Task 6: minor (deferred): array analysis accepted by MemoryStore vs D1 json_type; "chillona" maps to 2–6 kHz (sibilance at 7 kHz → n/a on real cases); turn-1 rejection loses analysis for the session
- Parked — 256 KB guard sticky in extreme sessions; no omission marker (plugin pre-trims); USD cap soft overshoot (~$2) and timeouts at $0; null-model iterations priced dearest may reach the cap sooner; battery echoes unsanitised bands; plan lines 94–95 superseded by Ruling (I2) — Ruling: acceptable for the beta; revisit after first deploy (compare cost_usd with the console) — cost if wrong: small cap/accuracy drift

## Pendiente del autor

- Desplegar siguiendo docs/DEPLOY.md del servidor (Cloudflare, D1, secreto, códigos).
- Primera pasada real de la batería con Sonnet (coste estimado en céntimos) y, después, con los tres modelos.
- Grabar casos reales (voces) cuando vuelva al estudio.

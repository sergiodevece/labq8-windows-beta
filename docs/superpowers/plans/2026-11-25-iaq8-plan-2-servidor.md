# IAQ8 · Plan 2: servidor, prompt y batería de casos — plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Servidor VitaPlug Assist que cumple el contrato del Plan 1 y llama a
Claude con un prompt de EQ para principiantes. Incluye códigos de acceso,
límites de uso, registro del coste por sesión y una batería de casos para
elegir modelo escuchando las propuestas.

**Architecture:** Repositorio propio `../vitaplug-assist-server`, en
TypeScript. El núcleo es independiente de la plataforma: la función
`handleTurn`, con dependencias inyectadas (almacén, cliente del modelo y
reloj), probada con vitest en Node. Un adaptador delgado lo ejecuta en
Cloudflare Workers, con D1 (SQLite) como almacén.

Cada turno es sin estado: un prompt de sistema por plugin (en caché) más un
único mensaje de usuario con el contexto (fuente, análisis, ajustes y
propuesta actual) y la transcripción. La respuesta llega por salida
estructurada (`output_config.format`).

La batería de casos reutiliza el mismo núcleo y genera un informe HTML. Para
escucharlo, una herramienta C++ nueva del repositorio LABQ8 (`iaq8_render`)
aplica la propuesta a cada WAV con el motor de LABQ8 e iguala el volumen.

**Tech Stack:**
- Servidor: Node ≥ 20 (hay 24), TypeScript estricto, `@anthropic-ai/sdk`,
  `zod`, `vitest`, `wrangler` como dependencia de desarrollo (se usa con
  `npx`), Cloudflare Workers + D1.
- `iaq8_render`: C++20/JUCE, en el proyecto `iaq8/` del Plan 1.

**Spec:** `docs/superpowers/specs/2026-11-24-iaq8-design.md` (§6.3 contrato, §6.4
servidor, §6.5 límites, §8 calidad y coste). Contrato exacto del plugin:
`iaq8/src/assist/Contract.h` y `iaq8/tests/fixtures/contract/*.json`.
Decisiones y pendientes del Plan 1: `iaq8/docs/PLAN1_DECISIONS.md`.

## Global Constraints

- **El contrato es el del Plan 1, sin cambios.**
  - `POST /v1/turn` con `Authorization: Bearer <código>`.
  - Petición y respuestas exactamente como en `Contract.h` y los fixtures.
  - Errores: 401 `{"error":"no_access"}`, 429 `{"error":"limit_reached"}`,
    422 `{"error":"invalid_request"}` y 503 `{"error":"unavailable"}`.
  - Respuesta 200: JSON RFC 8259 estricto, profundidad ≤ 32, enteros de 15
    dígitos como mucho y exponentes de 3 dígitos como mucho; el plugin
    rechaza cualquier otra cosa.
- **Plazo:** el plugin corta a los 30 s, así que el servidor responde en
  ≤ 25 s. El cliente del modelo usa `timeout: 22000` y `maxRetries: 0`.
- **Modelo por defecto:** `claude-sonnet-5-5`. Variables de entorno: `MODEL`,
  con los modelos de la batería permitidos (`claude-haiku-5-5`,
  `claude-sonnet-5-5`, `claude-opus-5-5`), y `EFFORT` (por defecto `low`).
  No se envía `thinking`.
- **Rechazos del modelo:** `fallbacks: "default"` con la beta
  `server-side-fallback-2026-07-01` en Sonnet y Opus (en Haiku no existe). Con `stop_reason: "refusal"` o
  `"max_tokens"`, el servidor responde 503 `unavailable`.
- **La clave de Anthropic** solo existe como secreto del Worker
  (`ANTHROPIC_API_KEY`). Nunca va en el código, los logs ni los informes.
- **Códigos de acceso:** se guarda solo su SHA-256, nunca el código. Nunca
  se registran ni el código ni la cabecera `Authorization`.
- **No hay audio en ninguna parte del servidor.** Se guardan la
  conversación, el análisis y las respuestas de cada turno: es la decisión
  del prototipo (spec §6.4).
- **Límites por código** (configurables por código, con estos valores por
  defecto): 30 sesiones y 400 turnos por mes natural, y 10 turnos por
  minuto.
- **Límites que recorta el servidor** como defensa adicional, porque el
  límite de verdad lo pone el plugin:
  - como mucho 5 bandas activas;
  - ganancia de −12 a +6 dB;
  - Q de campana de 0,5 a 8;
  - Q de shelf de 0,5 a 1,0;
  - Q de corte de 0,5 a 0,707;
  - Q de notch de 0,5 a 18;
  - frecuencia de 20 a 20 000 Hz.

  La combinación de bandas y las reglas por fuente las sigue aplicando
  `sanitize` en el plugin.
- **Respuestas rápidas:** 8 como mucho, de 18 caracteres como mucho. Los
  textos del modelo están en el idioma del usuario (español por defecto).
- **LABQ8 no se toca.** En el repositorio LABQ8 solo cambian `iaq8/` y
  `docs/superpowers/`.

## Review Focus

1. **Petición repetida tras un corte:** el plugin reenvía la misma
   conversación, a veces con dos mensajes de usuario seguidos (nota de la
   Tarea 10 del Plan 1). El servidor responde con normalidad, sin 422, y
   cada intento cuenta como un turno. Prueba en la Tarea 4.
2. **El modelo devuelve algo fuera del contrato:**
   - más de 8 respuestas rápidas o textos más largos → se recortan;
   - números no finitos o bandas imposibles → se recortan o se descartan;
   - salida truncada o un rechazo → 503.

   El plugin nunca recibe un 200 que vaya a rechazar. Pruebas en la Tarea 3.
3. **Petición hostil o enorme:** cuerpo de más de 256 KB, más de 200
   mensajes o textos de más de 4000 caracteres → 422 sin llamar al modelo.
   Prueba en la Tarea 1.
4. **El límite de un código se alcanza a mitad de sesión:** 429 con
   `limit_reached`, sin llamar al modelo y sin registrar coste. Prueba en la
   Tarea 2.
5. **El coste que se registra coincide con la factura:** incluye la lectura
   y la escritura de caché que devuelve `usage`, por modelo. Prueba en la
   Tarea 3.

---

## File Structure

```
../vitaplug-assist-server/            (repositorio nuevo, git init)
  package.json, tsconfig.json, vitest.config.ts, wrangler.toml, .gitignore, README.md
  src/contract.ts          esquemas zod de petición/respuesta (fuente: Contract.h), serialización estricta
  src/limits.ts            recortes por banda del servidor
  src/store.ts             interfaz Store + MemoryStore (tests) ; src/d1Store.ts (D1)
  src/access.ts            hash de códigos, comprobación de límites
  src/prompt/iaq8.md       prompt de sistema de la EQ (versionado)
  src/prompt/registry.ts   plugin_id → { prompt, schema, mapping }
  src/model.ts             interfaz ModelClient + AnthropicModelClient (SDK) + precios
  src/turn.ts              handleTurn(request, deps) → { status, body }
  src/worker.ts            adaptador Cloudflare Workers (fetch handler)
  migrations/0001_init.sql esquema D1
  scripts/issue-code.ts    emite códigos de acceso (imprime el código una vez)
  eval/cases/<id>/case.json + analysis.json (+ referencia al WAV fuera del repo)
  eval/run.ts              ejecuta la batería contra varios modelos
  eval/report.ts           informe HTML
  test/*.test.ts           vitest
  test/fixtures/contract/  copia de iaq8/tests/fixtures/contract (no editar a mano)
  docs/DEPLOY.md           pasos para el autor
LABQ8/iaq8/
  tools/RenderMain.cpp     iaq8_render
  tests/RenderTests.cpp
```

Comandos del servidor: `npm test`, `npx tsc --noEmit` y `npx wrangler dev`.
Comandos del plugin: los del Plan 1.

---

### Task 1: Repositorio, contrato y validación de la petición

**Files:**
- Create: `../vitaplug-assist-server/{package.json,tsconfig.json,vitest.config.ts,.gitignore,README.md}`, `src/contract.ts`, `test/contract.test.ts`, `test/fixtures/contract/*` (copiados de `iaq8/tests/fixtures/contract/`)

**Interfaces:**
- Produces (`src/contract.ts`):
  ```ts
  export const SOURCES = ["voice","acoustic_guitar","electric_guitar","bass","drums","mix","other"] as const;
  export const BAND_TYPES = ["bell","low_shelf","high_shelf","low_cut","high_cut","notch"] as const;
  export type Band = { type: BandType; freq_hz: number; gain_db: number; q: number; bypass: boolean; candidate_id: string | null; reason: string | null };
  export type TurnRequest = { plugin_id: string; plugin_version: string; session_id: string; source: Source; source_note: string;
    messages: { role: "assistant" | "user"; text: string }[]; analysis: unknown | null;
    applied: { bands: Band[] }; proposal: { bands: Band[] } | null };
  export type QuestionReply = { kind: "question"; text: string; quick_replies: string[] };
  export type ProposalReply = { kind: "proposal"; explanation: string; bands: Band[]; quick_replies: string[] };
  export type Reply = QuestionReply | ProposalReply;
  export function parseTurnRequest (bodyText: string): { ok: true; value: TurnRequest } | { ok: false };
  export function serializeReply (r: Reply): string;   // strict JSON: finite numbers rounded to ≤ 4 decimals, depth ≤ 32
  export const LIMITS = { maxBodyBytes: 262144, maxMessages: 200, maxTextChars: 4000, maxQuickReplies: 8, maxQuickReplyChars: 18 };
  ```

- [ ] **Step 1:** Crear el repositorio y el commit inicial:
  `mkdir ../vitaplug-assist-server && cd ../vitaplug-assist-server && git init -b main`.
  Instalar las dependencias:
  `npm i @anthropic-ai/sdk zod` y `npm i -D typescript vitest tsx wrangler @cloudflare/workers-types`.
  En `tsconfig.json`, `"strict": true` y `"noUncheckedIndexedAccess": true`.
- [ ] **Step 2: Pruebas que fallan** (`test/contract.test.ts`):
  - `request_first_turn.json` se lee bien.
  - Se rechazan: fuente desconocida, banda con `type` desconocido, número
    no finito, `applied.bands` con un número de bandas distinto de 8, cuerpo
    de más de 256 KB, más de 200 mensajes y un texto de más de 4000
    caracteres (Review Focus 3).
  - Se aceptan dos mensajes `user` seguidos (Review Focus 1).
  - `serializeReply` sobre `reply_question.json` y `reply_proposal.json`
    devuelve objetos equivalentes, sin números de más de 4 decimales ni
    exponentes, y redondea `NaN` a un valor finito o lo rechaza (decidir y
    probar: se rechaza con una excepción).
- [ ] **Step 3:** Ejecutar `npm test`. Esperado: FAIL.
- [ ] **Step 4:** Implementar con esquemas `zod` en modo `.strict()`.
  `serializeReply` construye el JSON a mano o redondea antes de usar
  `JSON.stringify`, que nunca produce exponentes con valores entre 1e-4 y
  1e15.
- [ ] **Step 5:** Ejecutar `npm test` y `npx tsc --noEmit`. Esperado: PASS.
- [ ] **Step 6: Commit** `"Server P2.1: contract types and request validation"`.

### Task 2: Códigos de acceso, límites de uso y almacén

**Files:**
- Create: `src/store.ts`, `src/access.ts`, `migrations/0001_init.sql`, `scripts/issue-code.ts`, `test/access.test.ts`

**Interfaces:**
- Produces:
  ```ts
  export interface Store {
    findCode (codeHash: string): Promise<{ id: string; label: string; active: boolean; maxSessionsMonth: number; maxTurnsMonth: number; maxTurnsMinute: number } | null>;
    countUsage (codeId: string, sinceMs: number): Promise<{ sessions: number; turns: number }>;
    countTurnsSince (codeId: string, sinceMs: number): Promise<number>;
    recordTurn (t: TurnRecord): Promise<void>;   // Task 4 fills TurnRecord
    insertCode (c: { id: string; codeHash: string; label: string }): Promise<void>;
  }
  export class MemoryStore implements Store { /* tests */ }
  export async function hashCode (code: string): Promise<string>;  // SHA-256 hex via crypto.subtle
  export type AccessResult = { ok: true; codeId: string; codeLabel: string } | { ok: false; status: 401 | 429 };
  export async function checkAccess (authHeader: string | null, sessionId: string, store: Store, nowMs: number): Promise<AccessResult>;
  ```
  Esquema D1 (`0001_init.sql`):
  - `access_codes(id TEXT PK, code_hash TEXT UNIQUE, label TEXT, active INTEGER, max_sessions_month INTEGER DEFAULT 30, max_turns_month INTEGER DEFAULT 400, max_turns_minute INTEGER DEFAULT 10, created_ms INTEGER)`.
  - `turns(id TEXT PK, code_id TEXT, session_id TEXT, ts_ms INTEGER, plugin_id TEXT, model TEXT, kind TEXT, input_tokens INTEGER, output_tokens INTEGER, cache_read_tokens INTEGER, cache_write_tokens INTEGER, cost_usd REAL, latency_ms INTEGER, status INTEGER, applied_previous INTEGER, request_json TEXT, reply_json TEXT)`.
  - Índices por `(code_id, ts_ms)` y por `session_id`.

  `scripts/issue-code.ts <label>`:
  - genera 20 caracteres aleatorios legibles (sin 0/O/1/I);
  - escribe el SQL de inserción con el hash para `npx wrangler d1 execute`;
  - imprime el código **una sola vez** por la salida estándar.

- [ ] **Step 1: Pruebas que fallan:**
  - Sin cabecera, con un código desconocido o con un código inactivo → 401.
  - Pasar los turnos del mes, las sesiones del mes (una sesión nueva cuenta
    al primer turno de un `session_id` nuevo) o los turnos por minuto →
    429 (Review Focus 4).
  - Un código válido devuelve su `codeId`.
  - `hashCode` es estable.
- [ ] **Step 2:** Ejecutar `npm test`. Esperado: FAIL.
- [ ] **Step 3:** Implementar. El mes natural se cuenta en UTC.
- [ ] **Step 4:** Ejecutar `npm test`. Esperado: PASS.
- [ ] **Step 5: Commit** `"Server P2.2: access codes, usage limits and store"`.

### Task 3: Prompt, salida estructurada y cliente del modelo

**Files:**
- Create: `src/prompt/iaq8.md`, `src/prompt/registry.ts`, `src/model.ts`, `src/limits.ts`, `test/model.test.ts`, `test/limits.test.ts`

**Interfaces:**
- Produces:
  ```ts
  // Structured output the model must produce (one flat object; strict-schema friendly: all keys required)
  export type ModelOutput = { kind: "question" | "proposal"; text: string; quick_replies: string[];
    bands: { type: BandType; freq_hz: number; gain_db: number; q: number; candidate_id: string | null; reason: string }[] };
  export const MODEL_OUTPUT_SCHEMA: object;            // JSON Schema for output_config.format (type json_schema), additionalProperties:false everywhere
  export function buildUserMessage (req: TurnRequest): string;   // context + transcript, see below
  export function toReply (out: ModelOutput): { reply: Reply; adjustments: string[] };  // maps + clamps (limits.ts)
  export type Usage = { input: number; output: number; cacheRead: number; cacheWrite: number };
  export interface ModelClient { complete (system: string, user: string, model: string, effort: string): Promise<{ ok: true; output: ModelOutput; usage: Usage; model: string } | { ok: false; reason: "refusal" | "max_tokens" | "invalid" | "error" }> }
  export class AnthropicModelClient implements ModelClient { constructor (apiKey: string) }
  export const PRICES_USD_PER_MTOK: Record<string, { input: number; output: number; cacheRead: number; cacheWrite: number }>;
  export function costUsd (model: string, u: Usage): number;
  export const pluginRegistry: Record<string, { system: string }>; // "iaq8" → contents of iaq8.md
  ```
- **`buildUserMessage`.** Un único mensaje de usuario con estas secciones
  (la API exige que el primer mensaje sea del usuario y la conversación
  empieza con preguntas del plugin, así que la transcripción va como texto):
  - `FUENTE:`
  - `ANÁLISIS (JSON):`, o "sin análisis en este turno"
  - `AJUSTES APLICADOS (JSON)`
  - `PROPUESTA ACTUAL (JSON)`, o "ninguna"
  - `CONVERSACIÓN:`, con líneas `Asistente: …` y `Usuario: …`

  El texto del usuario se marca como dato: va entre delimitadores y no se
  sigue como instrucción.
- **`AnthropicModelClient.complete`.** Llama a `client.beta.messages.create`
  con:
  - `model`, `max_tokens: 4000`;
  - `system: [{ type: "text", text: system, cache_control: { type: "ephemeral" } }]`;
  - `messages: [{ role: "user", content: user }]`;
  - `output_config: { effort, format: { type: "json_schema", schema: MODEL_OUTPUT_SCHEMA } }`;
  - `betas: ["server-side-fallback-2026-07-01"]` y `fallbacks: "default"`,
    solo para `claude-sonnet-5-5` y `claude-opus-5-5` (Haiku 5.5 no tiene
    reintento en otro modelo desde el servidor: sin `fallbacks` ni beta);
  - en el cliente, `timeout: 22000` y `maxRetries: 0`.

  Primero comprueba `stop_reason` (`refusal` o `max_tokens` → `ok: false`).
  Luego hace `JSON.parse` del primer bloque de texto y lo valida con zod
  (si falla → `invalid`). Captura los errores tipados del SDK
  (`Anthropic.APIError` → `error`).
- **Precios** (USD por millón de tokens, consultados el 2026-11-03; la
  escritura de caché se apunta como 1,25 × la entrada, a verificar en la
  página de precios antes de cobrar a nadie):

  | Modelo | Entrada | Salida | Lectura de caché |
  |---|---|---|---|
  | claude-haiku-5-5 | 0,10 | 0,50 | 0,01 (a verificar) |
  | claude-sonnet-5-5 | 2 | 10 | 0,10 |
  | claude-opus-5-5 | 4 | 20 | 0,20 |

- **Contenido mínimo del prompt `iaq8.md`** (en español, ≥ 600 palabras para
  que la caché funcione):
  - Rol: asistente de ecualización para personas sin conocimientos técnicos.
  - Hablar con palabras normales; mencionar Hz solo de forma aproximada y
    explicada.
  - Como mucho 2–3 preguntas antes de proponer. Si el objetivo es vago y hay
    análisis, preguntar con ejemplos concretos sacados de los candidatos
    ("¿suena como dentro de una caja?").
  - **Usar los candidatos medidos:** cada banda que corrige un problema
    medido lleva su `candidate_id` y una frecuencia dentro de 1/6 de octava
    del candidato. Las bandas sin candidato son ajustes de carácter
    (brillo, cuerpo), suaves y anchas.
  - Respetar los límites: 5 bandas como mucho; realce de +6 dB como mucho y
    preferiblemente ≤ +3; recortes de −12 dB como mucho y preferiblemente
    ≤ −6; Q por tipo como en Global Constraints. Si el usuario pide algo
    extremo, explicarlo y proponer lo máximo razonable.
  - Para correcciones, partir de la PROPUESTA ACTUAL y cambiar solo lo que
    se pide.
  - `text`: en una pregunta, la pregunta; en una propuesta, una explicación
    general de como mucho 2 frases. Una frase corta por banda en `reason`.
  - `quick_replies`: 3–6 opciones de ≤ 18 caracteres, útiles para el
    siguiente paso ("Menos brillo", "Más cuerpo").
  - Conocimiento por fuente, en tablas cortas:
    - voz: barro 200–400 Hz, caja/nasal 500–1 kHz, presencia 2–5 kHz,
      sibilancia 5–9 kHz, aire ≥ 10 kHz, ruido por debajo de ~80 Hz;
    - guitarras acústica y eléctrica, bajo, batería y mezcla: equivalentes;
    - los objetivos de los 12 presets INTENTION de
      `docs/specs/LABQ8_INTENTION_PHASE_MS_v01.md` §2 como guía de intención.
  - Idioma: el del último mensaje del usuario.
- **`toReply`.** Recorta con `limits.ts` según los límites del servidor de
  Global Constraints y añade una línea en `adjustments` por cada recorte.
  Rellena hasta 8 bandas con bandas desactivadas (`bypass: true`, campana,
  0 dB, Q 1, frecuencias por defecto de LABQ8 por banda, de
  `src/parameters/ParameterIDs.h` `defaultFrequencyHz`: 1000, 40, 100, 250,
  2500, 5000, 10000 y 16000 Hz). Las bandas que
  devuelve el modelo van con `bypass: false`. Recorta las respuestas
  rápidas a 8 × 18 caracteres. Una pregunta lleva `bands: []`.

- [ ] **Step 1: Pruebas que fallan**, con un `ModelClient` falso para la
  lógica y unitarias para `limits`, `buildUserMessage`, `toReply` y
  `costUsd`:
  - `buildUserMessage` incluye el análisis solo cuando lo hay, y marca el
    texto del usuario como dato.
  - `toReply`: 9 respuestas rápidas o de 25 caracteres se recortan; una
    banda de +9 dB pasa a +6 con su línea de ajuste; un Q de corte de 2 pasa
    a 0,707; un número no finito descarta la banda; siempre hay 8 bandas en
    una propuesta (Review Focus 2).
  - `costUsd` con `usage` real de ejemplo, incluidas la lectura y la
    escritura de caché, para los tres modelos (Review Focus 5).
  - `MODEL_OUTPUT_SCHEMA` lleva `additionalProperties: false` y `required`
    con todas las claves en cada objeto.
  - Una prueba opcional con red real: `RUN_LIVE=1` y `ANTHROPIC_API_KEY`
    presentes, si no se salta. Llama a `complete` con el caso
    `request_first_turn.json` y comprueba que devuelve `ok`.
- [ ] **Step 2:** Ejecutar `npm test`. Esperado: FAIL.
- [ ] **Step 3:** Implementar. El prompt se carga como texto (en Workers,
  importación de texto con regla `[[rules]] type = "Text"` en
  `wrangler.toml`; en Node/vitest, `fs.readFileSync` mediante un pequeño
  cargador inyectado en `registry.ts`).
- [ ] **Step 4:** Ejecutar `npm test` y `npx tsc --noEmit`. Esperado: PASS.
- [ ] **Step 5: Commit** `"Server P2.3: EQ prompt, structured output and model client"`.

### Task 4: `handleTurn`, registro y adaptador de Workers

**Files:**
- Create: `src/turn.ts`, `src/worker.ts`, `src/d1Store.ts`, `wrangler.toml`, `test/turn.test.ts`

**Interfaces:**
- Consumes: Tasks 1–3.
- Produces:
  ```ts
  export type TurnRecord = { id: string; codeId: string; sessionId: string; tsMs: number; pluginId: string; model: string; kind: "question" | "proposal" | "error";
    usage: Usage; costUsd: number; latencyMs: number; status: number; appliedPrevious: boolean | null; requestJson: string; replyJson: string };
  export type Deps = { store: Store; model: ModelClient; modelName: string; effort: string; now: () => number; newId: () => string };
  export async function handleTurn (method: string, authHeader: string | null, bodyText: string, deps: Deps): Promise<{ status: number; body: string }>;
  ```
  Orden:
  1. Método o ruta distintos de `POST /v1/turn` → 404 (en el adaptador).
  2. Cuerpo no válido → 422, sin llamar al modelo.
  3. `checkAccess`: 401 o 429.
  4. `plugin_id` sin prompt registrado → 422.
  5. Llamada al modelo; si falla, 503.
  6. `toReply`, luego 200 con `serializeReply`.
  7. Registro del turno (también en error 503: kind `error`, con el coste
     si hubo `usage`). `appliedPrevious` es verdadero si `applied.bands`
     coincide (tolerancia 1e-3) con la última propuesta registrada de esa
     sesión; con esto se mide si las propuestas se aplican (spec §6.4).
  8. **Nunca se registran** la cabecera `Authorization` ni el código.

  `worker.ts`:
  - Lee `env.ANTHROPIC_API_KEY`, `env.MODEL`, `env.EFFORT` y `env.DB`.
  - Responde `Content-Type: application/json`.
  - Si el registro falla, la respuesta al plugin no cambia: se registra con
    `ctx.waitUntil` y los errores se tragan, sin escribir el cuerpo en
    consola.

- [ ] **Step 1: Pruebas que fallan**, con `MemoryStore` y un `ModelClient`
  falso:
  - Un recorrido 200 con pregunta y otro con propuesta, con el cuerpo
    comparado contra los fixtures del contrato.
  - 401 sin código.
  - 429 al pasar el límite, sin llamar al modelo (lo cuenta el falso).
  - 422 con un cuerpo malo.
  - Un rechazo del modelo → 503 con `{"error":"unavailable"}`.
  - Petición repetida con dos mensajes de usuario seguidos → 200, y cuenta
    dos turnos (Review Focus 1).
  - `appliedPrevious` vale verdadero cuando `applied` coincide con la
    propuesta anterior de la sesión.
  - Ningún campo registrado contiene el código.
- [ ] **Step 2:** Ejecutar `npm test`. Esperado: FAIL.
- [ ] **Step 3:** Implementar `turn.ts`, `d1Store.ts` (consultas
  preparadas) y `worker.ts`. En `wrangler.toml`: `compatibility_flags =
  ["nodejs_compat"]`, el binding D1 `DB`, `[vars] MODEL="claude-sonnet-5-5"`
  y `EFFORT="low"`.
- [ ] **Step 4:** Ejecutar `npm test` y `npx tsc --noEmit`. Comprobación
  manual con `npx wrangler dev` y D1 local (`npx wrangler d1 migrations
  apply DB --local`), un código emitido en local y un
  `curl -X POST localhost:8787/v1/turn` con `request_first_turn.json` y la
  clave en `.dev.vars` (ignorado por git). Si no hay clave, se espera un 503
  correcto.
- [ ] **Step 5: Commit** `"Server P2.4: turn handler, logging and Workers adapter"`.

### Task 5: `iaq8_render` (repositorio LABQ8)

**Files:**
- Create: `iaq8/tools/RenderMain.cpp`, `iaq8/tests/RenderTests.cpp`
- Modify: `iaq8/CMakeLists.txt` (`juce_add_console_app(iaq8_render ...)` con `juce_audio_formats`)

**Interfaces:**
- Produces:
  - Uso: `iaq8_render <entrada.wav> <respuesta.json> <salida_prefijo>`. Lee
    una respuesta `proposal` del contrato (también acepta la petición
    completa y usa su `proposal`).
  - Pasa las bandas por `assist::sanitize`, con la fuente de un argumento
    opcional `--source`, por defecto `other`.
  - Escribe `<prefijo>_antes.wav` (original) y `<prefijo>_despues.wav`
    (procesado con `labq8::dsp::EqCore`, igualado en sonoridad ponderada K
    con el original en todo el archivo y limitado a ±6 dB, como en el
    plugin).
  - Imprime en la salida estándar las líneas de ajuste y la compensación
    aplicada.
  - Termina con 2 si los argumentos o los archivos no son válidos.
- Reutiliza `LoudnessMatcher`, o su ponderación K, y `EqCore` sin
  duplicarlos.

- [ ] **Step 1: Pruebas que fallan** (`RenderTests.cpp`, sobre la función
  de la herramienta extraída a `iaq8/src/analysis/OfflineRender.{h,cpp}` con
  `RenderResult render (channels, sr, BandSet)`):
  - Ruido rosa más una campana de −6 dB a 1 kHz → el espectro del "después"
    baja ~6 dB en 1 kHz.
  - La sonoridad K del "después" queda a ±0,5 dB de la del "antes".
  - Un `BandSet` vacío (todo en bypass) da una salida igual a la entrada.
- [ ] **Step 2:** Ejecutar el filtro `iaq8_tests "render"`. Esperado: FAIL.
- [ ] **Step 3:** Implementar.
- [ ] **Step 4:** Ejecutar la suite completa. Ejecución manual sobre un WAV
  de prueba con `reply_proposal.json`.
- [ ] **Step 5: Commit** (en LABQ8, rama `dev/iaq8-prototype`) `"IAQ8 P2.5: iaq8_render offline before/after renderer"`.

### Task 6: Batería de casos e informe

**Files:**
- Create: `eval/cases/README.md`, `eval/cases/synthetic-*/case.json` + `analysis.json`, `eval/run.ts`, `eval/report.ts`, `eval/checks.ts`, `test/eval.test.ts`
- Modify: `.gitignore` (`eval/out/`, `eval/audio/`)

**Interfaces:**
- **Formato de un caso** (`case.json`):

  ```json
  { "id": "voz-caja-01",
    "source": "voice",
    "source_note": "",
    "wav": "eval/audio/voz-caja-01.wav",
    "turns": ["Que se entienda", "menos agudos"],
    "expect": "notes for the reviewer" }
  ```

  `analysis.json` es la salida de `iaq8_analyze <wav> <source>`. Los WAV
  viven en `eval/audio/`, que git ignora (pueden ser grabaciones privadas).
- **`run.ts --models claude-haiku-5-5,claude-sonnet-5-5,claude-opus-5-5 [--effort low] [--cases glob]`.**
  Por cada caso y cada modelo:
  - Simula la conversación con las mismas preguntas locales que el plugin
    (Session.cpp), seguidas de los `turns` del caso.
  - Llama a `handleTurn` con un `MemoryStore` y el `AnthropicModelClient`
    real. Si el modelo pregunta en vez de proponer, contesta con el
    siguiente `turn`, o con "Hazlo como te parezca" cuando no quedan.
  - Guarda en `eval/out/<fecha>/<caso>/<modelo>.json` las respuestas, el
    coste, la latencia y los ajustes.
  - Si existen el WAV e `iaq8_render`, genera el audio de antes y después
    de la propuesta final.
- **`checks.ts`**, comprobaciones automáticas por resultado:
  - `contract_ok`: el plugin lo aceptaría.
  - `within_limits`: `toReply` no tuvo que recortar nada.
  - `targets_candidates`: si el análisis tiene candidatos de tipo
    excess/resonance/sibilance en la zona de lo que se pidió, alguna banda
    lleva `candidate_id` y frecuencia dentro de 1/6 de octava.
  - `questions_before_proposal` ≤ 3.
  - `max_boost_db` y `max_cut_db`.
- **`report.ts`** genera `eval/out/<fecha>/index.html`, autónomo, con una
  tabla por caso de los tres modelos:
  - bandas y curva de respuesta en SVG;
  - explicación y respuestas rápidas;
  - comprobaciones;
  - coste y latencia;
  - reproductores `<audio>` de antes y después.

  Al final, la media de coste por sesión y la latencia p50/p90 por modelo.
  Hay una columna para la nota del autor (1–5); se guarda en
  `localStorage`, con un botón "Exportar notas" que descarga un JSON.
- **Casos sintéticos.** 6 casos que generan señales con
  `iaq8_analyze`/`iaq8_render`, para probar el arnés antes de tener
  grabaciones reales:
  - voz con barro;
  - voz con sibilancia;
  - guitarra áspera;
  - bajo con retumbe;
  - mezcla apagada;
  - voz sin problemas, que comprueba que no se inventa cambios fuertes.

- [ ] **Step 1: Pruebas que fallan** (`test/eval.test.ts`, con un
  `ModelClient` falso):
  - `checks` sobre resultados preparados (cada comprobación en verdadero y
    en falso).
  - `run` sobre los casos sintéticos con el modelo falso escribe la
    estructura de salida.
  - `report` genera HTML con una fila por caso y modelo.
- [ ] **Step 2:** Ejecutar `npm test`. Esperado: FAIL.
- [ ] **Step 3:** Implementar. `eval/cases/README.md` explica cómo añadir un
  caso real (ver la Tarea 7).
- [ ] **Step 4:** Ejecutar `npm test`. Ejecución manual con la red real solo
  si `ANTHROPIC_API_KEY` está presente: `npx tsx eval/run.ts --models
  claude-sonnet-5-5 --cases synthetic-*`, mostrando el coste total antes de
  empezar y pidiendo confirmación si supera 1 USD.
- [ ] **Step 5: Commit** `"Server P2.6: evaluation battery and HTML report"`.

### Task 7: Despliegue, códigos y conexión del plugin

**Files:**
- Create: `../vitaplug-assist-server/docs/DEPLOY.md`
- Modify (LABQ8): `iaq8/CMakeLists.txt` (valor por defecto de `IAQ8_DEFAULT_SERVER_URL` solo si el autor da la URL; si no, documentar el `-D…`), `iaq8/docs/NETWORK_SPIKE.md` o un nuevo `iaq8/docs/E2E.md`

**Interfaces:**
- `DEPLOY.md` recoge los pasos que hace **el autor**, porque requieren
  cuentas y secretos suyos. Claude no los ejecuta:
  1. Crear una cuenta de Cloudflare (gratuita) y ejecutar
     `npx wrangler login`.
  2. `npx wrangler d1 create iaq8-assist`, copiar el `database_id` en
     `wrangler.toml` y ejecutar
     `npx wrangler d1 migrations apply DB --remote`.
  3. En la consola de Anthropic: crear una clave **solo para este
     servidor** y poner un **tope de gasto mensual** (por ejemplo, 30 USD).
  4. `npx wrangler secret put ANTHROPIC_API_KEY`: el autor pega la clave
     cuando se la pide.
  5. `npx wrangler deploy`, y anotar la URL `https://….workers.dev`.
  6. Emitir códigos con `npx tsx scripts/issue-code.ts "probador-1"` y
     ejecutar el SQL que imprime con `--remote`. Dar cada código a su
     probador.
  7. Compilar IAQ8 con
     `-DIAQ8_DEFAULT_SERVER_URL=https://….workers.dev`, instalarlo, guardar
     un código en el editor y hacer el recorrido completo en Logic.
  8. Consultar el coste por sesión con
     `npx wrangler d1 execute DB --remote --command "SELECT session_id, SUM(cost_usd), COUNT(*) FROM turns GROUP BY session_id ORDER BY MIN(ts_ms) DESC LIMIT 20"`.
- [ ] **Step 1:** Escribir `DEPLOY.md` con los pasos anteriores y la forma
  de revocar un código (`UPDATE access_codes SET active=0 …`).
- [ ] **Step 2:** Comprobar el flujo de principio a fin en local, sin
  cuenta de Cloudflare:
  - `npx wrangler dev`;
  - IAQ8 compilado con `-DIAQ8_DEFAULT_SERVER_URL=http://localhost:8787`
    (el cliente admite http solo para localhost);
  - la suite de IAQ8 con una prueba nueva, opcional y condicionada a
    `IAQ8_E2E_URL`, que envía `request_first_turn.json` por `AssistClient`
    y espera una `Question` o una `Proposal` válidas.
- [ ] **Step 3:** Anotar el resultado en `iaq8/docs/E2E.md`.
- [ ] **Step 4: Commit** (servidor) `"Server P2.7: deploy runbook"` y (LABQ8) `"IAQ8 P2.7: local end-to-end check"`.

---

## Al terminar el Plan 2

- Servidor: `npm test` y `npx tsc --noEmit` en verde.
- IAQ8: suite completa en verde.
- Batería: se ejecuta con los casos sintéticos y el modelo falso, y con
  Sonnet 5.5 real si hay clave.
- **Pendiente del autor:**
  - el despliegue (`DEPLOY.md`);
  - grabar 20–30 casos reales (WAV de 10–20 s y lo que pediría un
    principiante);
  - ejecutar la batería con los tres modelos y puntuarla en el informe.

  Con esas notas se elige el modelo por defecto (variable `MODEL`), sin
  tocar el plugin.

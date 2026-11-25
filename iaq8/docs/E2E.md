# IAQ8 P2.7: comprobación de extremo a extremo (local)

Comprueba que el plugin (`AssistClient` + `makeHttpsTransport`) habla con el
servidor VitaPlug Assist sin cuenta de Cloudflare y sin clave de Anthropic.

## Prueba opt-in

`contract: end to end against a server (opt-in, IAQ8_E2E_URL)` en
`iaq8/tests/ContractTests.cpp`. Envía el mismo `TurnRequest` que
`request_first_turn.json` (la prueba de contrato ya garantiza que son
idénticos) y acepta una `Question` o `Proposal` válidas, o `ErrorKind::unavailable`
(servidor sin clave del modelo). Informa de cuál ocurrió con una nota.

- `IAQ8_E2E_URL`: URL base del servidor. Sin ella la prueba se omite.
- `IAQ8_E2E_CODE`: código de acceso, leído del entorno (nunca está en el código).
- http solo se admite en localhost / 127.0.0.1 / [::1].

## Resultado (2026-11-25, macOS, build Debug)

Servidor, en `vitaplug-assist-server`:

```
npx wrangler d1 migrations apply DB --local          # ya aplicadas
npx tsx scripts/issue-code.ts "e2e-local"            # código: <redactado>
npx wrangler d1 execute DB --local --file <INSERT impreso>
npx wrangler dev --port 8787                          # sin .dev.vars: sin clave
```

Plugin:

```
cmake --build build/iaq8-debug --target iaq8_tests
IAQ8_E2E_URL=http://localhost:8787 IAQ8_E2E_CODE=<redactado> \
  build/iaq8-debug/iaq8_tests_artefacts/Debug/iaq8_tests
```

Salida relevante:

```
[PASS] contract: end to end against a server (opt-in, IAQ8_E2E_URL)  (3 checks, 44 ms)
       · E2E: Error kind 3 (unavailable: server has no model key)
124 tests, 122 passed, 0 failed, 2 skipped, 8392 checks
```

El log de `wrangler dev` mostró `POST /v1/turn 503` con
`invalid worker configuration: ANTHROPIC_API_KEY is missing`: la petición
llegó, el código de acceso fue aceptado y el servidor respondió con el 503
`unavailable` del contrato. Eso valida transporte, cabeceras, contrato de error
y http local. **No** valida una respuesta real del modelo: eso queda para el
autor con la clave real (ver `DEPLOY.md` del servidor).

Sin `IAQ8_E2E_URL`, la suite completa da 124 tests, 121 pasan, 3 omitidos.

## Apuntar el plugin a un servidor

`IAQ8_DEFAULT_SERVER_URL` conserva su valor por defecto (`https://example.invalid`,
todavía no hay URL). Para otro servidor, al configurar:

```
cmake -S iaq8 -B build/iaq8-debug -DIAQ8_DEFAULT_SERVER_URL=http://localhost:8787
# o, tras desplegar:
cmake -S iaq8 -B build/iaq8-release -DIAQ8_DEFAULT_SERVER_URL=https://<worker>.workers.dev
```

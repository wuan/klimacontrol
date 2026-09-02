# http-api Specification Delta

## ADDED Requirements

### Requirement: Unsupported request content types are rejected explicitly

Endpoints that expect a JSON request body SHALL reject a request carrying any other content type with `415 Unsupported Media Type`, naming the type they expect. They SHALL NOT allow such a request to fall through to the framework's `501 Handler did not handle the request`.

The reason is concrete. ESPAsyncWebServer treats a `application/x-www-form-urlencoded` body as request parameters: it parses the body into params and never invokes the route's body callback, so no response is produced and `_send()` substitutes a 501. That status describes the framework's confusion rather than the caller's mistake, gives no indication that the content type is at fault, and is indistinguishable from a genuine firmware defect. Diagnosing one such case from the 501 alone consumed several hours.

#### Scenario: JSON endpoint receives a form-encoded body

- **WHEN** a POST carrying `Content-Type: application/x-www-form-urlencoded` is sent to an endpoint that expects JSON
- **THEN** the response SHALL be `415` and SHALL name the expected content type
- **AND** it SHALL NOT be `501`

#### Scenario: Correct content type is unaffected

- **WHEN** a POST carrying `Content-Type: application/json` is sent
- **THEN** the request SHALL be handled normally

### Requirement: A no-response outcome indicates a defect, not an API result

`501 Handler did not handle the request` is produced by the framework when a matched handler leaves a request unanswered. Where it is reachable, it SHALL be treated as a defect or as an unhandled input class to be given a proper status, and SHALL NOT be documented as an outcome of any endpoint.

Any handler that responds from a body callback rather than from its request callback SHALL guarantee a response on every path through that callback, including early returns and error paths.

#### Scenario: Every documented path answers

- **WHEN** a documented endpoint is exercised on any of its success, validation-failure or authorisation-failure paths
- **THEN** it SHALL produce the status that path documents

#### Scenario: A diagnostic must not destroy the real response

- **WHEN** code in a request callback wishes to report that no response was produced
- **THEN** it SHALL first check that no response exists, because `AsyncWebServerRequest::send()` deletes and replaces any response already set by the body callback

### Requirement: Recent request outcomes are observable over a GET

The firmware SHALL retain a bounded in-memory record of recent HTTP requests, exposed over a `GET` endpoint, holding at least the URL, method, the response status seen by the middleware chain, the request content type and length, elapsed time, and free heap.

It SHALL be readable by a `GET`, deliberately, so that a fault affecting request bodies can still be observed: a diagnostic that travels by the same route as the thing it measures cannot be trusted when that route is what is suspect. The status recorded SHALL be the one visible to the middleware, which runs before the framework substitutes a 501 — so a request that produced no response SHALL be distinguishable from one that deliberately returned 501.

#### Scenario: A request that produced no response is identifiable

- **WHEN** a handler is matched but produces no response
- **THEN** the record SHALL show that no response existed, rather than showing the substituted status

#### Scenario: Content type is recorded

- **WHEN** a request is recorded
- **THEN** the content type as received by the device SHALL be included, so a mismatch between what a client believes it sent and what arrived is visible

#### Scenario: The buffer keeps the most recent requests

- **WHEN** more requests are served than the buffer holds
- **THEN** the oldest SHALL be evicted and the most recent retained

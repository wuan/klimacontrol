# http-api Specification Delta

## ADDED Requirements

### Requirement: Every matched request produces a response

The firmware SHALL produce an HTTP response for every request matched by a registered handler. A handler SHALL NOT leave a request unanswered such that the framework substitutes `501 Handler did not handle the request`; that status indicates a firmware defect rather than a legitimate API outcome, and SHALL NOT be observable through normal use of any documented endpoint.

This applies in particular to POST endpoints that carry a request body. Where a route responds from a body callback rather than from its request callback, the route SHALL still guarantee a response on every path through that callback, including early returns and error paths.

#### Scenario: Body-carrying POST always answers

- **WHEN** a POST with a JSON body is sent to any documented endpoint
- **THEN** the response SHALL be a status the endpoint documents — a success, a validation failure, or a CSRF rejection
- **AND** it SHALL NOT be `501`

#### Scenario: Response does not depend on uptime

- **WHEN** the same body-carrying POST is issued shortly after boot and again after the device has been running for an extended period
- **THEN** both SHALL produce the same status for the same input

#### Scenario: Handlers do not degrade permanently

- **WHEN** a device has served requests for an extended period
- **THEN** every endpoint SHALL remain as functional as it was immediately after boot
- **AND** no endpoint SHALL require a reboot to resume working

#### Scenario: A diagnostic must not destroy the real response

- **WHEN** code in a request callback wishes to report that no response was produced
- **THEN** it SHALL first check that no response exists, because `AsyncWebServerRequest::send()` deletes and replaces any response already set by the body callback

# Spec Delta

## ADDED Requirements

### Requirement: Each page is one self-contained response

Each HTML page the firmware serves SHALL be self-contained: the shared styles
and the icon SHALL be inlined, so that loading a page issues exactly one HTTP
request. Pages SHALL NOT reference a separate stylesheet or icon resource.

#### Scenario: Loading the dashboard issues one request

- **WHEN** a browser opens `/`
- **THEN** it issues a single `GET /` and makes no subsequent request for a
  stylesheet or icon

#### Scenario: Loading the settings page issues one request

- **WHEN** a browser opens `/settings`
- **THEN** it issues a single `GET /settings` and makes no subsequent request
  for `/common.css` or `/favicon.svg`

#### Scenario: AP-mode configuration page issues one request

- **WHEN** a browser opens any page while the device is in AP mode
- **THEN** the served configuration page is self-contained and triggers no
  stylesheet or icon sub-request

#### Scenario: Device Info page issues one request

- **WHEN** a browser opens `/about`
- **THEN** it issues a single `GET /about` and makes no subsequent request for a
  stylesheet or icon

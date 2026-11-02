# Spec Delta

## ADDED Requirements

### Requirement: A page load stays above the low-heap threshold

Loading one web page SHALL NOT reduce
`heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` to the low-heap guard threshold
(16 KiB), and SHALL NOT leave the internal heap lower once the response
completes: the cost of a page load is the transient TCP send path of a single
connection, released when the send finishes. Repeated page loads SHALL NOT
monotonically reduce free internal heap.

#### Scenario: Single settings load leaves headroom

- **WHEN** an idle device serves one `GET /settings`
- **THEN** the lowest internal free heap observed during the request stays
  above the 16 KiB low-heap guard threshold
- **AND** after the response completes, internal free heap returns to its
  pre-request level

#### Scenario: Loading a page does not fan out into concurrent connections

- **WHEN** a client loads any page
- **THEN** the device holds only one in-flight response connection for that
  page load, so the peak internal-heap cost is that of one send path rather
  than several

#### Scenario: Repeated page loads do not leak

- **WHEN** a client loads the same page repeatedly
- **THEN** internal free heap after each load equals the level after the first,
  with no downward drift

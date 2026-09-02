# pid-autotune Specification Delta

## REMOVED Requirements

### Requirement: Derived gains are applied in memory only

**Reason**: The premise no longer holds. This requirement existed because `DeviceConfig` had no gain fields, which it states explicitly as its justification. This change adds those fields, so the requirement's own stated reason for existing is gone, and its central instruction — that any interface offering acceptance must disclaim durability — becomes false rather than merely obsolete.

**Migration**: Replaced by *Derived gains are persisted when accepted* below. Acceptance now survives a restart, so an interface that previously had to warn the user that gains would be lost SHALL instead stop warning. No stored data is affected: no device has ever persisted a gain, so there is nothing to migrate.

## ADDED Requirements

### Requirement: Derived gains are persisted when accepted

Accepting a derived result SHALL persist the gains and apply them to the running controller, suspending it so that the change is treated as a discontinuity. The gains SHALL survive a restart, because a run costs hours of the plant's time and a result whose lifetime is shorter than the experiment that produced it is not worth the experiment.

Persistence SHALL precede application, so that the gains a user is shown as being in force are the gains that will survive a restart.

Acceptance SHALL remain an explicit request rather than being applied automatically when a run converges. A run that silently rewrote the gains of a heating system while nobody was watching is a poor default, and derived values are worth a human glance given what they cost to obtain.

Because the gains are applied by the task that owns the controller, acceptance SHALL be reported as a request that was validated and accepted rather than as a change already applied. The gains actually in force SHALL be observable separately, so that a caller can confirm the outcome.

#### Scenario: Accepting applies and suspends

- **WHEN** a derived result is accepted
- **THEN** the controller SHALL adopt the gains and restart bumplessly on its next tick

#### Scenario: Accepted gains survive a restart

- **WHEN** the device restarts after a result was accepted
- **THEN** the accepted gains SHALL be in force rather than the compiled-in defaults

#### Scenario: Accepting without a result

- **WHEN** acceptance is requested and no converged result exists
- **THEN** the request SHALL be refused, and neither the stored nor the running gains SHALL change

#### Scenario: Convergence alone does not change the gains

- **WHEN** a run converges and no acceptance is requested
- **THEN** the gains in force SHALL be unchanged, and the derived values SHALL be reported as available for acceptance

#### Scenario: A failed write does not leave divergent state

- **WHEN** persisting an accepted result fails
- **THEN** the running controller SHALL NOT be left using gains that would not survive a restart

#### Scenario: The applied result is observable

- **WHEN** acceptance has been requested and accepted
- **THEN** the gains in force SHALL be readable, so that the caller can confirm the control task applied them

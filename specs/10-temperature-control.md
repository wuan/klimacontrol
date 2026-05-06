# Temperature Control Specification

## Purpose

Define PID-based temperature control algorithm.

## Requirements

### Control State
1. **S10.1** SHALL track control enabled/disabled state
2. **S10.2** SHALL store in DeviceConfig.temperature_control_enabled
3. **S10.3** SHALL default to disabled on startup

### Setpoint Management
4. **S10.4** SHALL track target temperature setpoint
5. **S10.5** SHALL store in DeviceConfig.target_temperature
6. **S10.6** Default setpoint SHALL be 22.0°C
7. **S10.7** Setpoint SHALL be valid in range 10.0°C to 30.0°C

### Control Loop
8. **S10.8** SHALL execute in SensorMonitor task
9. **S10.9** SHALL run at 1-second intervals by default
10. **S10.10** SHALL call updateControl() on each sensor reading
11. **S10.11** SHALL be skipped if control disabled or no valid sensor data

### PID Algorithm
12. **S10.12** SHALL use PID (Proportional-Integral-Derivative) control
13. **S10.13** SHALL calculate error = setpoint - process_variable
14. **S10.14** Process variable SHALL be current temperature
15. **S10.15** SHALL calculate: P = Kp * error, I = Ki * integral, D = Kd * derivative
16. **S10.16** SHALL calculate output = P + I + D
17. **S10.17** SHALL clamp output to valid range [0.0, 1.0]

### PID Parameters
18. **S10.18** SHALL have tunable Kp, Ki, Kd
19. **S10.19** Default parameters SHALL provide stable response

### Integral Windup Prevention
20. **S10.20** SHALL implement integral windup prevention (anti-windup clamping)
21. **S10.21** SHALL clamp integral term to output range
22. **S10.22** FUTURE: Stop accumulation when output saturated (not implemented)
23. **S10.23** FUTURE: Reset integral term when control disabled (not implemented)

### Output Handling
24. **S10.24** updateControl() SHALL return control output value
25. **S10.25** Output SHALL be 0.0 when control disabled or no valid data

### Sensor Data Integration
26. **S10.26** SHALL use averaged temperature from all sensors
27. **S10.27** SHALL use getTemperature() from SensorController
28. **S10.28** SHALL check isDataValid() and hasConnectedSensors()

### Hysteresis
29. **S10.29** FUTURE: Implement configurable hysteresis (not implemented)
30. **S10.30** FUTURE: Default hysteresis 0.5°C (not implemented)
31. **S10.31** FUTURE: Output 0.0 within hysteresis band (not implemented)

### Safety Limits
32. **S10.32** SHALL implement maximum output limits
33. **S10.33** SHALL implement maximum/minimum temperature limits
34. **S10.34** SHALL stop control if temperature exceeds safety limits

## Scenarios

### Scenario S10-A: Basic Control Loop
Given control enabled, target 22.0°C, current 20.0°C,
When updateControl() is called,
Then:
- error = 2.0
- P = Kp * 2.0
- I = Ki * integral_of_error
- D = Kd * derivative_of_error
- output = P + I + D (positive, clamped to [0.0, 1.0])

### Scenario S10-B: Temperature Reached Setpoint
Given target 22.0°C, current 22.0°C,
When updateControl() is called,
Then:
- error = 0.0
- P = 0.0
- Output = 0.0 (proportional and integral terms contribute nothing at zero error)

### Scenario S10-C: Overshoot Handling
Given target 22.0°C, current 23.0°C,
When updateControl() is called,
Then:
- error = -1.0
- Output is reduced (negative error reduces P term contribution)
- Output clamped to [0.0, 1.0]

### Scenario S10-D: Enabling Control
Given control disabled,
When setControlEnabled(true) is called,
Then:
- Control SHALL be enabled
- Bumpless transfer SHALL prevent output jump

### Scenario S10-E: Integral Windup Prevention
Given large error, output saturated at 1.0,
Then integral term is clamped to prevent windup

# Test Structure

This project uses PlatformIO's native testing framework with multiple independent test suites.

## Test Suites

Each test suite is in its own directory under `test/`:

```
test/
├── test_color/          # Color utility tests
├── test_jump/           # Jump animation tests
└── test_smooth_blend/   # SmoothBlend class tests
```

## How It Works

- **Each directory = One test suite** - Each directory is compiled and run independently
- **Each suite has its own main()** - No conflicts between test files
- **Separate execution** - Test suites run in isolation with their own build artifacts

## Running Tests

### Run all test suites:
```bash
pio test -e native
```

### Run a specific test suite:
```bash
pio test -e native -f test_color
pio test -e native -f test_jump
pio test -e native -f test_smooth_blend
```

### Run with verbose output:
```bash
pio test -e native -v
```

## Adding New Tests

To add a new test suite:

1. Create a new directory under `test/`:
   ```bash
   mkdir test/test_myfeature
   ```

2. Create a test file with Unity tests and a main() function:
   ```cpp
   #include "unity.h"
   #include "myfeature.h"

   void setUp() {}
   void tearDown() {}

   void test_myfeature_works() {
       TEST_ASSERT_TRUE(true);
   }

   int runUnityTests() {
       UNITY_BEGIN();
       RUN_TEST(test_myfeature_works);
       return UNITY_END();
   }

   int main() {
       return runUnityTests();
   }
   ```

3. Run the test:
   ```bash
   pio test -e native -f test_myfeature
   ```

## Test Suite Details

### test_color (5 tests)
Tests for color manipulation functions:
- Color component extraction (red, green, blue)
- Color construction from RGB values
- Individual component tests

### test_jump (4 tests)
Tests for the Jump ball animation:
- Position calculations
- Peak behavior
- Trajectory validation

### test_smooth_blend (2 tests)
Tests for smooth color transitions:
- Single color blending
- Multiple color blending
- Blend progress tracking

## CI/CD

Tests run automatically on GitHub Actions for every push/PR. See `.github/workflows/test.yml`.

## Notes

- Tests only run on the `native` environment (not ESP32)
- Mock objects are used where hardware dependencies exist
- Each test suite can be developed and debugged independently

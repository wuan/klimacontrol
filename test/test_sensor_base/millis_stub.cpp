// Provide millis() stub for native sensor base tests
#ifndef ARDUINO
unsigned long millis() {
    static unsigned long counter = 0;
    return counter++;
}
#endif

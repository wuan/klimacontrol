// Provide millis() stub for native sensor tests
#ifndef ARDUINO
unsigned long millis() {
    static unsigned long counter = 0;
    return counter++;
}
#endif
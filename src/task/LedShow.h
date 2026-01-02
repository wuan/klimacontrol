#ifndef LEDZ_LEDSHOW_H
#define LEDZ_LEDSHOW_H
#include "ShowController.h"

namespace Task {
    class LedShow {
        ShowController &controller;
        TaskHandle_t taskHandle = nullptr;


        static void taskWrapper(void *pvParameters);
        void task();
    public:
        LedShow(ShowController& controller);

        void startTask();
    };
}


#endif //LEDZ_LEDSHOW_H

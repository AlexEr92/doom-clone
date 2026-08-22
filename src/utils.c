#include "utils.h"
#include <SDL.h>

double get_time_seconds(void)
{
    static Uint64 frequency = 0;
    if (frequency == 0) {
        frequency = SDL_GetPerformanceFrequency();
    }
    return (double)SDL_GetPerformanceCounter() / (double)frequency;
}

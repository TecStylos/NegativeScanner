#include "glInit.h"

#include <iostream>

#include "glIncludes.h"

namespace ns
{
    bool g_is_initialized = false;

    void init(int* pargc, char** argv)
    {
        glutInit(pargc, argv);

        g_is_initialized = true;
    }

    bool is_initialized()
    {
        return g_is_initialized;
    }
}
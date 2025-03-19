#pragma once

namespace ns
{
    struct Vertex
    {
        struct Position
        {
            float x, y;
        } pos;

        struct UVCoords
        {
            float u, v;
        } uvCoords;
    };
}
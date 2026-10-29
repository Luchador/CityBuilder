#pragma once

/**
 * A Lot is comprised of one or more Zone tiles, forming a square or rectangle. The terrain beneath them must be flat.
 * Different zones have different preferred Lot sizes. In high land value areas Lots can combine to form larger Lots.
 */

enum LotStatus
{
    UNDEVELOPED,
    CONSTRUCTION,
    BUILT,
    ABANDONED,
    DESTROYED
};

class Lot
{
    public:
        unsigned int width, height;
        enum LotStatus lotStatus = UNDEVELOPED;
};
#pragma once

static inline unsigned int getNextBox(const unsigned currBox, const unsigned numOfBoxes)
{
    // increment with wrap-around
    return (currBox == numOfBoxes - 1) ? 0 : currBox + 1;
}

static inline unsigned int getPrevBox(const unsigned currBox, const unsigned numOfBoxes)
{
    // decrement with wrap-around
    return (currBox == 0) ? numOfBoxes - 1 : currBox - 1;
}

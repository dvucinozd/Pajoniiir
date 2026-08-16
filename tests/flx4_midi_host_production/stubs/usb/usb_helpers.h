#pragma once
static inline int usb_round_up_to_mps(int size, int mps)
{
    return mps > 0 ? ((size + mps - 1) / mps) * mps : size;
}

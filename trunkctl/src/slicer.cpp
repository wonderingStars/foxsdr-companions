// trunkctl - symbol slicer, see slicer.h. This file is part of trunkctl (GPL-3.0).
#include "slicer.h"

namespace trunkctl {

Slicer::Slicer(double rate, double baud, int phases) : sps_(rate / baud), ph_((size_t)phases) {
    for (int p = 0; p < phases; p++)
        ph_[(size_t)p].next_boundary = sps_ * (1.0 + (double)p / phases);
}

void Slicer::push(float s, BitSink &sink) {
    const double decay = 1.0 / 256.0; // droop of the level trackers per symbol
    for (size_t p = 0; p < ph_.size(); p++) {
        Phase &f = ph_[p];
        f.acc += s;
        f.count++;
        if ((double)(n_ + 1) >= f.next_boundary) {
            double v = f.acc / f.count;
            f.acc = 0;
            f.count = 0;
            f.next_boundary += sps_;
            if (!f.primed) {
                f.hi = f.lo = v;
                f.primed = true;
            } else {
                if (v > f.hi) f.hi = v; else f.hi += (v - f.hi) * decay;
                if (v < f.lo) f.lo = v; else f.lo += (v - f.lo) * decay;
            }
            double thr = 0.5 * (f.hi + f.lo);
            sink.bit((int)p, v > thr ? 1 : 0, n_);
        }
    }
    n_++;
}

} // namespace trunkctl

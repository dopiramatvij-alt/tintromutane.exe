// Audio-only verification: main never starts the desktop simulator.
#include "../outputs/gdi_chaos.cpp"

int main() {
    FILE* output = nullptr;
    if (fopen_s(&output, "phase2_samples.raw", "wb") != 0) return 1;
    for (uint32_t t = 0; t < BYTEBEAT_RATES[1] * PHASE_SECONDS; ++t) {
        const uint8_t sample = SecondBytebeatSample(t);
        if (fwrite(&sample, 1, 1, output) != 1) { fclose(output); return 2; }
    }
    return fclose(output) == 0 ? 0 : 3;
}

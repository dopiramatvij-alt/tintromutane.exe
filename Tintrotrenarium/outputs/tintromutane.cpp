
// Build in an x64 Native Tools Command Prompt for Visual Studio:
// cl /nologo /std:c++17 /EHsc /W4 /O2 /MT gdi_chaos.cpp /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib winmm.lib
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")

constexpr DWORD FRAME_DELAY_MS = 35;
// Set the output sample rate in Hz for each bytebeat, then recompile.
// Common choices: 8000, 11025, 22050, 44100, 48000.
constexpr DWORD BYTEBEAT_RATES[] = {
    22050, // bytebeat 1: 0-10 seconds
    8000, // bytebeat 2: 10-20 seconds
    8000, // bytebeat 3: 20-30 seconds
    44100, // bytebeat 4: 30-40 seconds
    8000, // bytebeat 5: 40-50 seconds
    8000, // bytebeat 6: 50-60 seconds
    11025, // RGB split: 60-70 seconds
    22050, // vortex: 70-80 seconds
    32000, // mosaic lenses: 80-90 seconds
    16000, // mirrored tunnel: 90-100 seconds
    22050, // plasma bloom: 100-110 seconds
    22050, // full-screen melt: 110-120 seconds
    8000, // final kaleidoscope: from 120 seconds, repeats every five seconds
};
constexpr DWORD PHASE_SECONDS = 10;
constexpr DWORD KALEIDOSCOPE_MS = 5000;
constexpr unsigned FIRST_NEW_TRACK = 6;
constexpr unsigned NEW_EFFECT_COUNT = 6;
constexpr unsigned KALEIDOSCOPE_TRACK = FIRST_NEW_TRACK + NEW_EFFECT_COUNT;
static_assert(sizeof(BYTEBEAT_RATES) / sizeof(BYTEBEAT_RATES[0]) == KALEIDOSCOPE_TRACK + 1,
              "Each effect needs its own audio-rate setting.");

struct PlaybackSegment {
    unsigned track;
    ULONGLONG startMs;
    ULONGLONG endMs;
};

// Play twelve ten-second tracks, then repeat only the final kaleidoscope.
static PlaybackSegment GetPlaybackSegment(ULONGLONG elapsed) {
    constexpr ULONGLONG trackMs = PHASE_SECONDS * 1000ULL;
    constexpr ULONGLONG introMs = KALEIDOSCOPE_TRACK * trackMs;
    if (elapsed < introMs) {
        const unsigned track = static_cast<unsigned>(elapsed / trackMs);
        return {track, track * trackMs, (track + 1) * trackMs};
    }
    const ULONGLONG cycleStart = introMs + ((elapsed - introMs) / KALEIDOSCOPE_MS) * KALEIDOSCOPE_MS;
    return {KALEIDOSCOPE_TRACK, cycleStart, cycleStart + KALEIDOSCOPE_MS};
}

static unsigned GetShaderId(ULONGLONG elapsed, const PlaybackSegment& segment) {
    if (segment.track == KALEIDOSCOPE_TRACK) return 12;
    if (segment.track >= FIRST_NEW_TRACK) return 13 + segment.track - FIRST_NEW_TRACK;
    const unsigned variant = static_cast<unsigned>(((elapsed - segment.startMs) / 5000) % 2);
    return segment.track + 6 * variant;
}

static uint8_t KaleidoscopeBytebeatSample(uint32_t t) {
    // (t*t/(1+(t>>9&t>>8))&128). A wide product avoids early overflow at higher rates.
    const uint64_t square = static_cast<uint64_t>(t) * t;
    const uint32_t divisor = 1u + ((t >> 9) & (t >> 8));
    return static_cast<uint8_t>((square / divisor) & 128u);
}

// Phase 2 matches the linked Dollchan formula in unsigned Bytebeat mode.
// Decimal division and signed truncation are needed before wrapping to 8 bits.
static uint8_t SecondBytebeatSample(uint32_t t) {
    const double ramp = static_cast<double>((t * 2u) % 32768u);
    const double value = ramp * std::sin(ramp / 200.0);
    const int32_t truncated = static_cast<int32_t>(value);
    return static_cast<uint8_t>(static_cast<uint32_t>(truncated) & 255u);
}

// The additional tracks have separate formulas and sample-rate entries.
static uint8_t AdditionalBytebeatSample(unsigned effect, uint32_t t) {
    uint32_t value = 0;
    switch (effect) {
    case 0: value = (t * ((t >> 10) | (t >> 13))) ^ ((t >> 6) * 9u); break;
    case 1: value = t * (((t >> 11) & 7u) + 3u) + ((t >> 4) ^ (t >> 9)); break;
    case 2: value = ((t * 5u & (t >> 8)) | (t * 3u & (t >> 10))) ^ ((t >> 6) * 7u); break;
    case 3: value = t * ((t >> 12) | ((t >> 8) & (t >> 10))) + ((t >> 5) | (t >> 7)); break;
    case 4:
        // User formula: 9*(t*((t>>9|t>>13)&15)&16), unsigned Bytebeat at 22050 Hz.
        return static_cast<uint8_t>(9u * ((t * (((t >> 9) | (t >> 13)) & 15u)) & 16u));
    case 5:
        value = (t * (((t >> 10) & 7u) + 1u)) ^ ((t >> 5) * (1u + ((t >> 13) & 3u)));
        value += (t >> 8) | (t >> 11);
        break;
    }
    return static_cast<uint8_t>(((value & 255u) / 2u) + 64u);
}

class BytebeatAudio {
public:
    void Start(ULONGLONG start) {
        stopped = false;
        worker = std::thread(&BytebeatAudio::Play, this, start);
    }

    void Stop() {
        stopped = true;
        if (worker.joinable()) worker.join();
    }

    ~BytebeatAudio() { Stop(); }

private:
    std::atomic<bool> stopped{false};
    std::thread worker;

    void Play(ULONGLONG start) {
        while (!stopped) {
            const PlaybackSegment segment = GetPlaybackSegment(GetTickCount64() - start);
            const unsigned phase = segment.track;
            const DWORD rate = BYTEBEAT_RATES[phase];
            const ULONGLONG end = start + segment.endMs;
            if (!rate) {
                while (!stopped && GetTickCount64() < end) Sleep(20);
                continue;
            }
            WAVEFORMATEX fmt{};
            fmt.wFormatTag = WAVE_FORMAT_PCM;
            fmt.nChannels = 1;
            fmt.nSamplesPerSec = rate;
            fmt.wBitsPerSample = 8;
            fmt.nBlockAlign = 1;
            fmt.nAvgBytesPerSec = rate;

            HWAVEOUT device = nullptr;
            if (waveOutOpen(&device, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
                while (!stopped && GetTickCount64() < end) Sleep(20);
                continue;
            }
            const size_t count = static_cast<size_t>(
                static_cast<ULONGLONG>(rate) * (segment.endMs - segment.startMs) / 1000);
            std::vector<uint8_t> samples(count);
            for (size_t i = 0; i < samples.size() && !stopped; ++i) {
                const uint32_t t = static_cast<uint32_t>(i);
                uint32_t value = 0;
                switch (phase) {
                case 0: value = t >> 6 ^ t & t >> 9 ^ t >> 12 | (t << (t >> 6) % 4 ^ -t & -t >> 13) % 128 ^ -t >> 1; break;
                case 1:
                    // Keep the linked player's full 8-bit output for this track.
                    samples[i] = SecondBytebeatSample(t);
                    continue;
                case 2: value = t * (t ^ t + (t >> 15 | 1) ^ (t - 1280 ^ t) >> 10) * t; break;
                case 3: value = t + (t & t >> 12) * t >> 8; break;
                case 4: value = 430 * (5 * t >> 11 | 5 * t >> 1); break;
                case 5: value = (t & (t >> 7 | t >> 8 | t >> 16) ^ t) * t; break;
                case 6: case 7: case 8: case 9: case 10: case 11:
                    samples[i] = AdditionalBytebeatSample(phase - FIRST_NEW_TRACK, t);
                    continue;
                case KALEIDOSCOPE_TRACK:
                    samples[i] = KaleidoscopeBytebeatSample(t);
                    continue;
                }
                samples[i] = static_cast<uint8_t>(((value & 255u) / 2u) + 64u);
            }
            WAVEHDR header{};
            header.lpData = reinterpret_cast<LPSTR>(samples.data());
            header.dwBufferLength = static_cast<DWORD>(samples.size());
            const bool prepared = !stopped && waveOutPrepareHeader(device, &header, sizeof(header)) == MMSYSERR_NOERROR;
            if (prepared && !stopped) waveOutWrite(device, &header, sizeof(header));
            while (!stopped && GetTickCount64() < end) Sleep(20);
            waveOutReset(device);
            if (prepared) waveOutUnprepareHeader(device, &header, sizeof(header));
            waveOutClose(device);
        }
    }
};

// Render from an immutable snapshot so the result is independent of pixel order.
// Return false on cancellation; the caller then exits through normal cleanup.
static bool ApplyKaleidoscope(uint32_t* destination, const uint32_t* source,
                             int width, int height, unsigned frame,
                             bool (*shouldStop)()) {
    if (!destination || !source || width <= 0 || height <= 0) return false;
    const double centerX = (width - 1) * 0.5;
    const double centerY = (height - 1) * 0.5;
    const double scale = std::max(1.0, std::min(width, height) * 0.5);
    const double time = static_cast<double>(frame % 20000u);
    const double zoom = 0.85 + 0.15 * std::sin(time * 0.025);
    const double c = std::cos(time * 0.018) * zoom;
    const double s = std::sin(time * 0.018) * zoom;
    const auto fold = [](double coordinate, int extent) {
        const double repeated = std::fmod(coordinate, 2.0);
        const double mirrored = repeated <= 1.0 ? repeated : 2.0 - repeated;
        return std::clamp(static_cast<int>(mirrored * (extent - 1)), 0, extent - 1);
    };
    for (int py = 0; py < height; ++py) {
        if ((py & 15) == 0 && shouldStop && shouldStop()) return false;
        const double dy = (py - centerY) / scale;
        for (int px = 0; px < width; ++px) {
            const double dx = (px - centerX) / scale;
            double u = std::abs(dx * c - dy * s);
            double v = std::abs(dx * s + dy * c);
            if (v > u) std::swap(u, v);
            const int sampleX = fold(u, width);
            const int sampleY = fold(v, height);
            destination[static_cast<size_t>(py) * width + px] =
                source[static_cast<size_t>(sampleY) * width + sampleX];
        }
    }
    return true;
}

// Full-screen flowing displacement. The runtime feeds each completed frame back
// into this renderer, making desktop pixels progressively melt and leave trails.
static bool ApplyScreenMelt(uint32_t* destination, const uint32_t* source,
                            int width, int height, unsigned frame,
                            bool (*shouldStop)()) {
    if (!destination || !source || width <= 0 || height <= 0) return false;
    const double time = static_cast<double>(frame % 20000u);
    const int maxDrop = std::max(2, height / 100);
    const int maxShift = std::max(1, width / 160);
    const int trail = std::max(1, width / 250);
    std::vector<int> columnDrop(width), rowShift(height);
    for (int x = 0; x < width; ++x) {
        const double flow = 0.5 + 0.3 * std::sin(x * 0.023 + time * 0.047) +
                            0.2 * std::sin(x * 0.007 - time * 0.031);
        columnDrop[x] = 1 + static_cast<int>(flow * maxDrop);
    }
    for (int y = 0; y < height; ++y) {
        rowShift[y] = static_cast<int>((0.65 * std::sin(y * 0.021 + time * 0.035) +
                                       0.35 * std::sin(y * 0.006 - time * 0.025)) * maxShift);
    }
    const auto pixelAt = [&](int x, int y) {
        x %= width; y %= height;
        if (x < 0) x += width;
        if (y < 0) y += height;
        return source[static_cast<size_t>(y) * width + x];
    };
    for (int py = 0; py < height; ++py) {
        if ((py & 15) == 0 && shouldStop && shouldStop()) return false;
        const int direction = rowShift[py] >= 0 ? 1 : -1;
        for (int px = 0; px < width; ++px) {
            const int drop = columnDrop[px];
            const int sx = px - rowShift[py], sy = py - drop;
            const uint32_t a = pixelAt(sx, sy);
            const uint32_t b = pixelAt(sx - direction * trail, sy - std::max(1, drop / 2));
            const uint32_t c = pixelAt(sx - direction * trail * 2, sy - drop);
            const uint32_t r = (((a >> 16) & 255u) * 7u + ((b >> 16) & 255u) * 3u + ((c >> 16) & 255u) * 2u) / 12u;
            const uint32_t g = (((a >> 8) & 255u) * 7u + ((b >> 8) & 255u) * 3u + ((c >> 8) & 255u) * 2u) / 12u;
            const uint32_t blue = ((a & 255u) * 7u + (b & 255u) * 3u + (c & 255u) * 2u) / 12u;
            destination[static_cast<size_t>(py) * width + px] = (r << 16) | (g << 8) | blue;
        }
    }
    return true;
}

// Full-desktop spatial effects sample an immutable source image.
// IDs: RGB split, vortex, mosaic lenses, mirrored tunnel, plasma bloom, full-screen melt.
static bool ApplyAdditionalShader(unsigned effect, uint32_t* destination,
                                  const uint32_t* source, int width, int height,
                                  unsigned frame, bool (*shouldStop)()) {
    if (effect >= NEW_EFFECT_COUNT || !destination || !source || width <= 0 || height <= 0) return false;
    if (effect == 5) return ApplyScreenMelt(destination, source, width, height, frame, shouldStop);
    const double time = static_cast<double>(frame % 20000u);
    const double centerX = (width - 1) * 0.5, centerY = (height - 1) * 0.5;
    const double halfW = std::max(1.0, width * 0.5), halfH = std::max(1.0, height * 0.5);
    const double c = std::cos(time * 0.018), s = std::sin(time * 0.018);
    const int shiftX = static_cast<int>(std::sin(time * 0.06) * width * 0.025);
    const int shiftY = static_cast<int>(std::cos(time * 0.047) * height * 0.025);
    const int cell = 48 + static_cast<int>(16 * (1.0 + std::sin(time * 0.017)));
    const double zoom = 0.55 + 0.35 * std::sin(time * 0.019);
    const auto pixelAt = [&](int x, int y) {
        x %= width; y %= height;
        if (x < 0) x += width;
        if (y < 0) y += height;
        return source[static_cast<size_t>(y) * width + x] & 0x00FFFFFFu;
    };
    std::vector<int> xWave, yWave;
    if (effect == 4) {
        xWave.resize(width); yWave.resize(height);
        for (int x = 0; x < width; ++x) xWave[x] = static_cast<int>(127 * std::sin(x * 0.015 + time * 0.055));
        for (int y = 0; y < height; ++y) yWave[y] = static_cast<int>(127 * std::cos(y * 0.021 - time * 0.043));
    }
    for (int py = 0; py < height; ++py) {
        if ((py & 15) == 0 && shouldStop && shouldStop()) return false;
        const double ny = (py - centerY) / halfH;
        for (int px = 0; px < width; ++px) {
            const double nx = (px - centerX) / halfW;
            uint32_t result = 0;
            switch (effect) {
            case 0:
                result = (pixelAt(px + shiftX, py) & 0x00FF0000u) |
                         (pixelAt(px, py + shiftY) & 0x0000FF00u) |
                         (pixelAt(px - shiftX, py - shiftY) & 0x000000FFu);
                break;
            case 1: {
                const double bend = 0.85 / (1.0 + 1.5 * (nx * nx + ny * ny));
                const int sx = static_cast<int>(centerX + (nx * c - ny * s * bend) * halfW);
                const int sy = static_cast<int>(centerY + (nx * s * bend + ny * c) * halfH);
                result = pixelAt(sx, sy);
                break;
            }
            case 2: {
                double localX = (px % cell) - cell * 0.5;
                const double localY = (py % cell) - cell * 0.5;
                if (((px / cell) ^ (py / cell)) & 1) localX = -localX;
                const double lens = zoom + 0.2 * (localX * localX + localY * localY) / (cell * cell);
                const int sx = static_cast<int>((px / cell) * cell + cell * 0.5 + localX * lens);
                const int sy = static_cast<int>((py / cell) * cell + cell * 0.5 + localY * lens);
                result = pixelAt(sx, sy);
                break;
            }
            case 3: {
                const double radius = std::max(std::abs(nx), std::abs(ny));
                if (radius < 0.000001) {
                    result = pixelAt(width / 2, height / 2);
                } else {
                    const double depth = radius * 4.0 + time * 0.013;
                    const double layer = std::fmod(depth, 1.0);
                    const double mirror = (static_cast<int>(depth) & 1) ? -1.0 : 1.0;
                    const int sx = static_cast<int>(centerX + mirror * nx / radius * layer * halfW);
                    const int sy = static_cast<int>(centerY + ny / radius * layer * halfH);
                    result = pixelAt(sx, sy);
                }
                break;
            }
            case 4: {
                const uint32_t original = pixelAt(px, py);
                const uint32_t field = static_cast<uint32_t>(xWave[px] + yWave[py] + 254) & 255u;
                const uint32_t bloom = static_cast<uint32_t>((nx * nx + ny * ny) * 96) + frame * 3u;
                const uint32_t r = (((original >> 16) & 255u) + field) / 2u;
                const uint32_t g = (((original >> 8) & 255u) + ((field + bloom + 85u) & 255u)) / 2u;
                const uint32_t b = ((original & 255u) + 255u - field) / 2u;
                result = (r << 16) | (g << 8) | b;
                break;
            }
            }
            destination[static_cast<size_t>(py) * width + px] = result;
        }
    }
    return true;
}

class GDIChaos {
public:
    bool Initialize() {
        x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        if (w < 100 || h < 100)
            return false;

        screen = GetDC(nullptr);
        if (!screen) return false;

        SetStretchBltMode(screen, COLORONCOLOR);
        return true;
    }

    void Run(ULONGLONG start) {
        unsigned frame = 0;
        while (true) {
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
                break;

            const ULONGLONG elapsed = GetTickCount64() - start;
            const PlaybackSegment segment = GetPlaybackSegment(elapsed);
            const unsigned phase = std::min(segment.track, 5u);
            // New effects use paired ten-second tracks; the kaleidoscope is the final loop.
            for (int i = 0; i < 3; ++i) {
                switch (phase) {
                case 0: Random(0, 1) ? Zoom() : ColorGlitch(); break;
                case 1: Random(0, 1) ? Melt() : Rings(); break;
                case 2: Random(0, 1) ? Stretch() : MirrorBlock(); break;
                case 3: Random(0, 1) ? Invert() : OffsetTiles(); break;
                case 4: Random(0, 1) ? Pixelate() : Zoom(); break;
                default: Random(0, 1) ? OffsetTiles() : MirrorBlock(); break;
                }
            }

            if (frame % 8 == 0) {
                switch (phase % 3) {
                case 0: Skew(); break;
                case 1: MirrorScreen(); break;
                default: ShrinkEcho(); break;
                }
            }

            // Process the entire virtual desktop with an animated pixel shader.
            if (!Shader(GetShaderId(elapsed, segment), frame)) break;

            GdiFlush();
            Sleep(FRAME_DELAY_MS);
            ++frame;
        }
    }

    void Cleanup() {
        if (screen) {
            ReleaseDC(nullptr, screen);
            screen = nullptr;
        }

        // Request that normal desktop windows redraw.
        RedrawWindow(
            GetDesktopWindow(), nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE |
            RDW_ALLCHILDREN | RDW_UPDATENOW
        );

        EnumWindows([](HWND hwnd, LPARAM) -> BOOL {
            if (IsWindowVisible(hwnd)) {
                InvalidateRect(hwnd, nullptr, TRUE);
                RedrawWindow(
                    hwnd, nullptr, nullptr,
                    RDW_INVALIDATE | RDW_ERASE |
                    RDW_FRAME | RDW_UPDATENOW
                );
            }
            return TRUE;
        }, 0);
    }

    ~GDIChaos() {
        if (screen)
            Cleanup();
    }

private:
    HDC screen = nullptr;
    std::vector<uint32_t> shaderSource;
    std::vector<uint32_t> meltFeedback;
    unsigned meltStartFrame = 0;
    bool meltActive = false;
    int x = 0, y = 0, w = 0, h = 0;

    std::mt19937 rng{std::random_device{}()};

    int Random(int low, int high) {
        return std::uniform_int_distribution<int>(
            low, high
        )(rng);
    }

    bool Shader(unsigned phase, unsigned frame) {
        const int width = w, height = h;
        const int sx = x, sy = y;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HDC memory = CreateCompatibleDC(screen);
        if (!memory) return true;
        HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap || !bits) { if (bitmap) DeleteObject(bitmap); DeleteDC(memory); return true; }
        HGDIOBJ old = SelectObject(memory, bitmap);
        if (!old || old == HGDI_ERROR) { DeleteObject(bitmap); DeleteDC(memory); return true; }
        bool keepRunning = true;
        if (BitBlt(memory, 0, 0, width, height, screen, sx, sy, SRCCOPY)) {
            GdiFlush();
            auto* pixels = static_cast<uint32_t*>(bits);
            if (phase >= 12 && phase < 13 + NEW_EFFECT_COUNT) {
                shaderSource.assign(pixels, pixels + static_cast<size_t>(width) * height);
                const auto shouldStop = []() -> bool { return (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0; };
                if (phase == 18) {
                    if (!meltActive || meltFeedback.size() != shaderSource.size()) {
                        meltFeedback = shaderSource;
                        meltStartFrame = frame;
                        meltActive = true;
                    }
                    keepRunning = ApplyScreenMelt(pixels, meltFeedback.data(), width, height,
                        frame - meltStartFrame, shouldStop);
                    if (keepRunning) {
                        meltFeedback.assign(pixels, pixels + static_cast<size_t>(width) * height);
                    }
                } else {
                    meltActive = false;
                    if (phase == 12) {
                        keepRunning = ApplyKaleidoscope(pixels, shaderSource.data(), width, height, frame, shouldStop);
                    } else {
                        keepRunning = ApplyAdditionalShader(phase - 13, pixels, shaderSource.data(), width, height, frame, shouldStop);
                    }
                }
            } else {
                for (int py = 0; py < height; ++py) {
                    if ((py & 31) == 0 && (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) {
                        keepRunning = false;
                        break;
                    }
                    for (int px = 0; px < width; ++px) {
                        const size_t index = static_cast<size_t>(py) * width + px;
                        const uint32_t pixel = pixels[index];
                        const uint32_t r = (pixel >> 16) & 255u, g = (pixel >> 8) & 255u, b = pixel & 255u;
                        const uint32_t xx = static_cast<uint32_t>(px), yy = static_cast<uint32_t>(py);
                        uint32_t nr, ng, nb;
                        switch (phase % 12) {
                        case 0: {
                            const uint32_t pulse = ((xx + frame * 8u) ^ (yy * 2u + frame * 5u)) & 255u;
                            nr = r + pulse; ng = g ^ (pulse >> 1); nb = b + (255u - pulse); break;
                        }
                        case 1: {
                            const uint32_t plasma = ((xx * yy >> 7) + frame * 11u) & 255u;
                            nr = g ^ plasma; ng = b + plasma; nb = r ^ (plasma * 2u); break;
                        }
                        case 2: {
                            const uint32_t dx = xx - static_cast<uint32_t>(width / 2);
                            const uint32_t dy = yy - static_cast<uint32_t>(height / 2);
                            const uint32_t rings = ((dx * dx + dy * dy) >> 10) + frame * 9u;
                            nr = r ^ rings; ng = g + rings * 2u; nb = b ^ (rings >> 1); break;
                        }
                        case 3: {
                            const uint32_t checker = (((xx + frame * 5u) / 40u) ^ ((yy + frame * 3u) / 40u)) & 1u;
                            nr = checker ? 255u - r : b; ng = checker ? 255u - g : r; nb = checker ? 255u - b : g; break;
                        }
                        case 4: {
                            const uint32_t zoom = (xx * 1664525u + yy * 1013904223u + frame * 69069u) >> 24;
                           
                        }
                        case 5: {
                            const uint32_t bands = ((xx + yy + frame * 13u) >> 5) & 7u;
                            nr = (bands & 1u) ? 255u - b : g; ng = (bands & 2u) ? 255u - r : b; nb = (bands & 4u) ? 255u - g : r; break;
                        }
                        case 6: {
                            const uint32_t mosaic = ((xx / 36u) * (yy / 36u) + frame * 5u) & 255u;
                            nr = b ^ mosaic; ng = r + mosaic * 3u; nb = g ^ (mosaic >> 1); break;
                        }
                        case 7: {
                            const uint32_t ripple = ((xx * xx + yy * yy) >> 11) + frame * 17u;
                            nr = r + ripple; ng = b ^ (ripple * 3u); nb = g + (ripple >> 1); break;
                        }
                        case 8: {
                            const uint32_t grid = ((xx / 48u + frame / 3u) ^ (yy / 48u + frame / 4u)) & 3u;
                            nr = grid & 1u ? 255u - g : b; ng = grid & 2u ? 255u - b : r; nb = 255u - r + grid * 25u; break;
                        }
                        case 9: {
                            const uint32_t flare = ((xx | yy) + (xx & yy) * 2u + frame * 21u) & 255u;
                            nr = r ^ flare; ng = g + (flare ^ frame); nb = b + flare * 2u; break;
                        }
                        case 10: {
                            const uint32_t tiles = ((xx / 24u) ^ (yy / 24u) ^ (frame / 3u)) & 255u;
                            nr = 255u - b + tiles * 5u; ng = r ^ (tiles * 7u); nb = 255u - g + tiles * 3u; break;
                        }
                        default: {
                            const uint32_t burst = ((xx ^ (yy * 3u)) + frame * 15u) & 255u;
                            nr = g + burst; ng = 255u - b + burst; nb = r ^ (burst * 5u); break;
                        }
                        }
                        pixels[index] = ((nr & 255u) << 16) | ((ng & 255u) << 8) | (nb & 255u);
                    }
                }
            }
            if (keepRunning) BitBlt(screen, sx, sy, width, height, memory, 0, 0, SRCCOPY);
        }
        SelectObject(memory, old);
        DeleteObject(bitmap);
        DeleteDC(memory);
        return keepRunning;
    }

    void Rings() {
        const int cx = x + Random(0, w - 1), cy = y + Random(0, h - 1);
        const int radius = Random(20, 150);
        HPEN pen = CreatePen(PS_SOLID, Random(2, 8), RGB(Random(0, 255), Random(0, 255), Random(0, 255)));
        if (!pen) return;
        HGDIOBJ oldPen = SelectObject(screen, pen);
        if (!oldPen || oldPen == HGDI_ERROR) { DeleteObject(pen); return; }
        HGDIOBJ oldBrush = SelectObject(screen, GetStockObject(NULL_BRUSH));
        if (oldBrush && oldBrush != HGDI_ERROR) {
            Ellipse(screen, cx - radius, cy - radius, cx + radius, cy + radius);
            SelectObject(screen, oldBrush);
        }
        SelectObject(screen, oldPen);
        DeleteObject(pen);
    }

    void Skew() {
        HDC memory = CreateCompatibleDC(screen);
        if (!memory) return;
        HBITMAP bitmap = CreateCompatibleBitmap(screen, w, h);
        if (!bitmap) { DeleteDC(memory); return; }
        HGDIOBJ old = SelectObject(memory, bitmap);
        if (old && old != HGDI_ERROR) {
            if (BitBlt(memory, 0, 0, w, h, screen, x, y, SRCCOPY)) {
                const int shift = std::max(5, w / 25);
                POINT points[3] = {{x + shift, y}, {x + w, y + shift}, {x, y + h - shift}};
                PlgBlt(screen, points, memory, 0, 0, w, h, nullptr, 0, 0);
            }
            SelectObject(memory, old);
        }
        DeleteObject(bitmap);
        DeleteDC(memory);
    }

    void MirrorScreen() {
        StretchBlt(screen, x + w, y, -w, h, screen, x, y, w, h, SRCCOPY);
    }

    void ShrinkEcho() {
        const int marginX = std::max(1, w / 20), marginY = std::max(1, h / 20);
        StretchBlt(screen, x + marginX, y + marginY, w - marginX * 2, h - marginY * 2,
                   screen, x, y, w, h, SRCCOPY);
    }

    void MirrorBlock() {
        const int width = std::min(Random(40, 300), w);
        const int height = std::min(Random(40, 220), h);
        const int sx = x + Random(0, w - width), sy = y + Random(0, h - height);
        StretchBlt(screen, sx + width, sy, -width, height, screen, sx, sy, width, height, SRCCOPY);
    }

    void Zoom() {
        const int marginX = std::max(1, w / 35), marginY = std::max(1, h / 35);
        StretchBlt(screen, x, y, w, h, screen, x + marginX, y + marginY,
                   w - marginX * 2, h - marginY * 2, SRCCOPY);
    }

    void OffsetTiles() {
        for (int i = 0; i < 8; ++i) {
            const int width = std::min(Random(30, 180), w);
            const int height = std::min(Random(30, 180), h);
            const int sx = x + Random(0, w - width), sy = y + Random(0, h - height);
            BitBlt(screen, sx + Random(-65, 65), sy + Random(-65, 65),
                   width, height, screen, sx, sy, SRCCOPY);
        }
    }

    void Pixelate() {
        const int width = std::min(Random(80, 300), w), height = std::min(Random(80, 240), h);
        const int sx = x + Random(0, w - width), sy = y + Random(0, h - height);
        const int smallW = std::max(1, width / 12), smallH = std::max(1, height / 12);
        HDC memory = CreateCompatibleDC(screen);
        if (!memory) return;
        HBITMAP bitmap = CreateCompatibleBitmap(screen, smallW, smallH);
        if (!bitmap) { DeleteDC(memory); return; }
        HGDIOBJ old = SelectObject(memory, bitmap);
        if (old && old != HGDI_ERROR) {
            SetStretchBltMode(memory, COLORONCOLOR);
            StretchBlt(memory, 0, 0, smallW, smallH, screen, sx, sy, width, height, SRCCOPY);
            StretchBlt(screen, sx, sy, width, height, memory, 0, 0, smallW, smallH, SRCCOPY);
            SelectObject(memory, old);
        }
        DeleteObject(bitmap);
        DeleteDC(memory);
    }

    void Melt() {
        int width = Random(15, 100);
        int height = Random(30, 180);

        width = std::min(width, w);
        height = std::min(height, h);

        int sx = Random(0, w - width);
        int sy = Random(0, h - height);
        int drop = Random(5, 70);

        BitBlt(
            screen,
            x + sx, y + sy + drop,
            width, height,
            screen,
            x + sx, y + sy,
            SRCCOPY
        );
    }

    void Stretch() {
        int sx = Random(0, w - 1);
        int sy = Random(0, h - 1);

        int width = std::min(Random(50, 400), w - sx);
        int height = std::min(Random(30, 250), h - sy);

        int dw = width + Random(-30, 100);
        int dh = height + Random(-20, 70);

        dw = std::max(1, dw);
        dh = std::max(1, dh);

        StretchBlt(
            screen,
            x + sx, y + sy,
            dw, dh,
            screen,
            x + sx, y + sy,
            width, height,
            SRCCOPY
        );
    }

    void Invert() {
        int width = std::min(Random(30, 250), w);
        int height = std::min(Random(20, 180), h);

        int sx = Random(0, w - width);
        int sy = Random(0, h - height);

        PatBlt(
            screen,
            x + sx, y + sy,
            width, height,
            DSTINVERT
        );
    }

    void ColorGlitch() {
        int width = std::min(Random(20, 180), w);
        int height = std::min(Random(5, 60), h);

        int sx = Random(0, w - width);
        int sy = Random(0, h - height);

        COLORREF color = RGB(
            Random(0, 255),
            Random(0, 255),
            Random(0, 255)
        );

        HBRUSH brush = CreateSolidBrush(color);
        if (!brush) return;

        HGDIOBJ old = SelectObject(screen, brush);

        PatBlt(
            screen,
            x + sx, y + sy,
            width, height,
            PATINVERT
        );

        SelectObject(screen, old);
        DeleteObject(brush);
    }

};

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    int result = MessageBoxA(
        nullptr,
        "run malware?",
        "GDI Chaos - Safety Confirmation",
        MB_YESNO | MB_ICONWARNING |
        MB_DEFBUTTON2 | MB_TOPMOST
    );

    if (result != IDYES)
        return 0;

    GDIChaos chaos;

    if (!chaos.Initialize()) {
        MessageBoxA(
            nullptr, "Unable to access screen.",
            "Error", MB_OK | MB_ICONERROR
        );
        return 1;
    }

    BytebeatAudio audio;
    const ULONGLONG start = GetTickCount64();
    audio.Start(start);

    chaos.Run(start);

    audio.Stop();
    chaos.Cleanup();

    return 0;
}

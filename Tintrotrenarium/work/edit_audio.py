from pathlib import Path
p = Path('../outputs/gdi_chaos.cpp')
s = p.read_text(encoding='utf-8')
s = s.replace('#include <algorithm>', '#include <algorithm>\n#include <atomic>\n#include <thread>')
start = s.index('// Audio output sample rate')
end = s.index('class GDIChaos')
s = s[:start] + r'''// Set the output sample rate in Hz for each bytebeat, then recompile.
// Common choices: 8000, 11025, 22050, 44100, 48000.
constexpr DWORD BYTEBEAT_RATES[] = {
    48000, // bytebeat 1: 0-10 seconds
    22050, // bytebeat 2: 10-20 seconds
    44100, // bytebeat 3: 20-30 seconds
    11025, // bytebeat 4: 30-40 seconds
    32000, // bytebeat 5: 40-50 seconds
    48000  // bytebeat 6: 50-60 seconds
};
constexpr DWORD PHASE_SECONDS = 10;

class BytebeatAudio {
public:
    void Start() {
        stopped = false;
        worker = std::thread(&BytebeatAudio::Play, this);
    }

    void Stop() {
        stopped = true;
        if (worker.joinable()) worker.join();
    }

    ~BytebeatAudio() { Stop(); }

private:
    std::atomic<bool> stopped{false};
    std::thread worker;

    void Play() {
        const ULONGLONG start = GetTickCount64();
        for (unsigned phase = 0; phase < 6 && !stopped; ++phase) {
            const DWORD rate = BYTEBEAT_RATES[phase];
            const ULONGLONG end = start + static_cast<ULONGLONG>(phase + 1) * PHASE_SECONDS * 1000;
            if (!rate) continue;
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
            std::vector<uint8_t> samples(static_cast<size_t>(rate) * PHASE_SECONDS);
            for (size_t i = 0; i < samples.size() && !stopped; ++i) {
                const uint32_t t = static_cast<uint32_t>(i);
                uint32_t value = 0;
                switch (phase) {
                case 0: value = (t * ((t >> 5) | (t >> 8))) ^ ((t >> 4) ^ (t * (t >> 11))); break;
                case 1: value = (t * ((t >> 9) | (t >> 13))) & (t >> 6); break;
                case 2: value = (t * ((t >> 11) & (t >> 8) & 123u)) | (t >> 3); break;
                case 3: value = (t * ((t >> 10) ^ (t >> 12))) + (t >> 5); break;
                case 4: value = ((t >> 6) | t | (t >> (t >> 16 & 7u))) * 10u + ((t >> 11) & 7u); break;
                default: value = ((t * 5u & (t >> 7)) | (t * 3u & (t >> 10))) ^ (t >> 4); break;
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

''' + s[end:]
p.write_text(s, encoding='utf-8')

#include "common.h"

namespace {
std::atomic<bool> stopped{false}, paused{false};
using Clock = std::chrono::steady_clock;
struct Audio {
    bool loopback;
    std::atomic<bool> running{true};
    std::thread worker;
    std::mutex mutex;
    std::deque<float> queue;
    std::string error;
    Audio(bool loop) : loopback(loop) {}
    void start() {
        std::promise<void> ready;
        auto initialized = ready.get_future();
        worker = std::thread([this, p = std::move(ready)]() mutable {
            HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            bool signaled = false;
            try {
                check(co, "Initialize audio COM");
                ComPtr<IMMDeviceEnumerator> enumerator;
                check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                       IID_PPV_ARGS(&enumerator)),
                      "Create audio enumerator");
                ComPtr<IMMDevice> device;
                check(enumerator->GetDefaultAudioEndpoint(loopback ? eRender : eCapture, eConsole,
                                                          &device),
                      loopback ? "No default speaker device"
                               : "No default microphone; check Windows privacy settings");
                ComPtr<IAudioClient> client;
                check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                       (void**)client.GetAddressOf()),
                      "Activate WASAPI");
                WAVEFORMATEX format{};
                format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
                format.nChannels = 2;
                format.nSamplesPerSec = 48000;
                format.wBitsPerSample = 32;
                format.nBlockAlign = 8;
                format.nAvgBytesPerSec = 384000;
                DWORD flags =
                    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
                if (loopback)
                    flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
                check(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 1000000, 0, &format,
                                         nullptr),
                      "Initialize 48 kHz WASAPI shared capture");
                ComPtr<IAudioCaptureClient> capture;
                check(client->GetService(IID_PPV_ARGS(&capture)), "Get WASAPI capture service");
                check(client->Start(), "Start audio capture");
                p.set_value();
                signaled = true;
                while (running) {
                    UINT32 available = 0;
                    check(capture->GetNextPacketSize(&available), "Get audio packet");
                    while (available) {
                        BYTE* data = nullptr;
                        UINT32 frames = 0;
                        DWORD status = 0;
                        check(capture->GetBuffer(&data, &frames, &status, nullptr, nullptr),
                              "Read audio packet");
                        {
                            std::lock_guard<std::mutex> lock(mutex);
                            if (paused) {
                                queue.clear();
                            } else {
                                const float* samples = (float*)data;
                                for (size_t i = 0; i < (size_t)frames * 2; i++)
                                    queue.push_back(
                                        (status & AUDCLNT_BUFFERFLAGS_SILENT) ? 0.f : samples[i]);
                                while (queue.size() > 192000)
                                    queue.pop_front();
                            }
                        }
                        check(capture->ReleaseBuffer(frames), "Release audio packet");
                        check(capture->GetNextPacketSize(&available), "Get next audio packet");
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(4));
                }
                client->Stop();
            } catch (const std::exception& e) {
                if (!signaled)
                    p.set_exception(std::current_exception());
                else {
                    std::lock_guard<std::mutex> lock(mutex);
                    error = e.what();
                    stopped = true;
                }
            }
            if (SUCCEEDED(co))
                CoUninitialize();
        });
        initialized.get();
    }
    void mix(std::vector<float>& output, float gain) {
        std::lock_guard<std::mutex> lock(mutex);
        for (size_t i = 0; i < output.size() && !queue.empty(); i++) {
            output[i] += queue.front() * gain;
            queue.pop_front();
        }
    }
    ~Audio() {
        running = false;
        if (worker.joinable())
            worker.join();
    }
};
void writeSample(IMFSinkWriter* writer, DWORD stream, const BYTE* data, DWORD size, LONGLONG time,
                 LONGLONG duration) {
    ComPtr<IMFMediaBuffer> buffer;
    check(MFCreateMemoryBuffer(size, &buffer), "Allocate media buffer");
    BYTE* ptr = nullptr;
    check(buffer->Lock(&ptr, nullptr, nullptr), "Lock media buffer");
    memcpy(ptr, data, size);
    check(buffer->Unlock(), "Unlock media buffer");
    check(buffer->SetCurrentLength(size), "Set media length");
    ComPtr<IMFSample> sample;
    check(MFCreateSample(&sample), "Create media sample");
    check(sample->AddBuffer(buffer.Get()), "Add media buffer");
    check(sample->SetSampleTime(time), "Set sample timestamp");
    check(sample->SetSampleDuration(duration), "Set sample duration");
    check(writer->WriteSample(stream, sample.Get()), "Encode media sample");
}
void videoType(IMFMediaType* type, GUID subtype, int w, int h, int fps) {
    check(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Set video type");
    check(type->SetGUID(MF_MT_SUBTYPE, subtype), "Set video subtype");
    check(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive), "Set interlace");
    check(MFSetAttributeSize(type, MF_MT_FRAME_SIZE, w, h), "Set frame size");
    check(MFSetAttributeRatio(type, MF_MT_FRAME_RATE, fps, 1), "Set frame rate");
    check(MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1), "Set aspect ratio");
}
void audioType(IMFMediaType* type, GUID subtype) {
    check(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "Set audio type");
    check(type->SetGUID(MF_MT_SUBTYPE, subtype), "Set audio subtype");
    check(type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2), "Set audio channels");
    check(type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000), "Set audio rate");
    check(type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16), "Set audio bits");
}
} // namespace
int record(const std::wstring& path, int x, int y, int width, int height, int fps, int bitrate,
           const std::wstring& audio, bool cursor, bool synthetic) {
    if (width < 4 || height < 4 || width > 7680 || height > 4320 ||
        (fps != 15 && fps != 30 && fps != 60) || bitrate < 100000 || bitrate > 50000000)
        throw std::runtime_error("Invalid dimensions, frame rate or bitrate (maximum 7680x4320)");
    int w = width & ~1, h = height & ~1;
    bool sound = synthetic || audio != L"none";
    stopped = false;
    paused = false;
    ComPtr<IMFAttributes> attributes;
    check(MFCreateAttributes(&attributes, 3), "Create writer attributes");
    check(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE),
          "Enable hardware encoders");
    check(attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE),
          "Disable writer throttling");
    ComPtr<IMFSinkWriter> writer;
    check(MFCreateSinkWriterFromURL(path.c_str(), nullptr, attributes.Get(), &writer),
          "Create MP4 output; verify destination is writable");
    ComPtr<IMFMediaType> output;
    check(MFCreateMediaType(&output), "Create video output type");
    videoType(output.Get(), MFVideoFormat_H264, w, h, fps);
    check(output->SetUINT32(MF_MT_AVG_BITRATE, bitrate), "Set bitrate");
    DWORD video = 0;
    check(writer->AddStream(output.Get(), &video), "Add H.264 stream");
    ComPtr<IMFMediaType> input;
    check(MFCreateMediaType(&input), "Create video input type");
    videoType(input.Get(), MFVideoFormat_RGB32, w, h, fps);
    check(input->SetUINT32(MF_MT_DEFAULT_STRIDE, w * 4), "Set RGB stride");
    check(writer->SetInputMediaType(video, input.Get(), nullptr),
          "Configure video encoder; install Media Feature Pack on Windows N");
    DWORD audioStream = 0;
    if (sound) {
        ComPtr<IMFMediaType> encoded;
        check(MFCreateMediaType(&encoded), "Create AAC type");
        audioType(encoded.Get(), MFAudioFormat_AAC);
        check(encoded->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 20000), "Set AAC bitrate");
        check(writer->AddStream(encoded.Get(), &audioStream), "Add AAC stream");
        ComPtr<IMFMediaType> pcm;
        check(MFCreateMediaType(&pcm), "Create PCM type");
        audioType(pcm.Get(), MFAudioFormat_PCM);
        check(pcm->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4), "Set PCM alignment");
        check(pcm->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 192000), "Set PCM bandwidth");
        check(writer->SetInputMediaType(audioStream, pcm.Get(), nullptr), "Configure AAC encoder");
    }
    Audio system(true), microphone(false);
    if (!synthetic) {
        if (audio == L"system" || audio == L"both")
            system.start();
        if (audio == L"mic" || audio == L"both")
            microphone.start();
    }
    // The sink writer consumes positive-stride RGB32 as top-down scanlines.
    check(writer->BeginWriting(), "Start MP4 writer");
    Bitmap frame(w, h, true);
    if (!synthetic)
        std::thread([]() {
            std::string line;
            while (std::getline(std::cin, line)) {
                if (line == "stop") {
                    stopped = true;
                    break;
                }
                if (line == "pause")
                    paused = true;
                if (line == "resume")
                    paused = false;
            }
            stopped = true;
        }).detach();
    EXECUTION_STATE awake =
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    (void)awake;
    std::cout << "READY" << std::endl;
    auto start = Clock::now(), next = start;
    Clock::duration pauseDuration{};
    LONGLONG audioFrames = 0, lastVideo = -1;
    int count = 0;
    HRESULT finalResult = S_OK;
    try {
        while (!stopped) {
            if (paused) {
                auto p = Clock::now();
                while (paused && !stopped)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                pauseDuration += Clock::now() - p;
                next = Clock::now();
                if (stopped)
                    break;
            }
            auto now = Clock::now();
            LONGLONG timestamp = synthetic ? (LONGLONG)count * 10000000 / fps
                                           : std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                 now - start - pauseDuration)
                                                     .count() /
                                                 100;
            if (timestamp <= lastVideo)
                timestamp = lastVideo + 1;
            if (synthetic) {
                for (int row = 0; row < h; row++) {
                    for (int col = 0; col < w; col++) {
                        size_t at = ((size_t)row * w + col) * 4;
                        frame.pixels[at] = (BYTE)((col + count * 4) % 256);
                        frame.pixels[at + 1] = (BYTE)(row % 256);
                        frame.pixels[at + 2] = 100;
                        frame.pixels[at + 3] = 255;
                    }
                }
            } else {
                frame.capture(x, y);
                if (cursor) {
                    CURSORINFO ci{sizeof(ci)};
                    if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING)) {
                        ICONINFO icon{};
                        if (GetIconInfo(ci.hCursor, &icon)) {
                            DrawIconEx(frame.dc, ci.ptScreenPos.x - x - icon.xHotspot,
                                       ci.ptScreenPos.y - y - icon.yHotspot, ci.hCursor, 0, 0, 0,
                                       nullptr, DI_NORMAL);
                            if (icon.hbmMask)
                                DeleteObject(icon.hbmMask);
                            if (icon.hbmColor)
                                DeleteObject(icon.hbmColor);
                        }
                    }
                }
            }
            if (synthetic) {
                RECT marker{0, 0, 40, 40};
                FillRect(frame.dc, &marker, (HBRUSH)GetStockObject(WHITE_BRUSH));
                SetBkMode(frame.dc, TRANSPARENT);
                SetTextColor(frame.dc, RGB(255, 255, 255));
                TextOutW(frame.dc, 52, 12, L"SAKURA / TOP", 12);
            }
            GdiFlush();
            writeSample(writer.Get(), video, frame.pixels, w * h * 4, timestamp, 10000000 / fps);
            lastVideo = timestamp;
            if (sound) {
                LONGLONG target = (timestamp + 10000000 / fps) * 48000 / 10000000;
                int frames = (int)std::min<LONGLONG>(target - audioFrames, 48000);
                if (frames > 0) {
                    std::vector<float> mix((size_t)frames * 2, 0);
                    if (synthetic) {
                        for (int i = 0; i < frames; i++) {
                            float s = (float)(0.15 * sin(2 * 3.141592653589793 * 440 *
                                                         (audioFrames + i) / 48000));
                            mix[i * 2] = mix[i * 2 + 1] = s;
                        }
                    } else {
                        float gain = audio == L"both" ? 0.5f : 1.f;
                        system.mix(mix, gain);
                        microphone.mix(mix, gain);
                    }
                    std::vector<int16_t> pcm(mix.size());
                    for (size_t i = 0; i < mix.size(); i++)
                        pcm[i] = (int16_t)(std::clamp(mix[i], -1.f, 1.f) * 32767.f);
                    writeSample(writer.Get(), audioStream, (BYTE*)pcm.data(), (DWORD)pcm.size() * 2,
                                audioFrames * 10000000 / 48000,
                                (LONGLONG)frames * 10000000 / 48000);
                    audioFrames += frames;
                }
            }
            count++;
            if (synthetic && count >= 90)
                break;
            next += std::chrono::nanoseconds(1000000000 / fps);
            if (next < Clock::now())
                next = Clock::now();
            if (!synthetic)
                std::this_thread::sleep_until(next);
        }
        finalResult = writer->Finalize();
        SetThreadExecutionState(ES_CONTINUOUS);
        check(finalResult, "Finalize MP4; check free disk space");
    } catch (...) {
        stopped = true;
        writer->Finalize();
        SetThreadExecutionState(ES_CONTINUOUS);
        throw;
    }
    {
        std::lock_guard<std::mutex> lock(system.mutex);
        if (!system.error.empty())
            throw std::runtime_error(system.error + "; partial recording was finalized");
    }
    {
        std::lock_guard<std::mutex> lock(microphone.mutex);
        if (!microphone.error.empty())
            throw std::runtime_error(microphone.error + "; partial recording was finalized");
    }
    std::cout << "SAVED" << std::endl;
    return 0;
}
int inspect(const std::wstring& path) {
    int counts[2] = {0, 0};
    LONGLONG ends[2] = {0, 0};
    for (int stream = 0; stream < 2; stream++) {
        ComPtr<IMFAttributes> attributes;
        check(MFCreateAttributes(&attributes, 1), "Create reader attributes");
        check(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE),
              "Enable color conversion");
        ComPtr<IMFSourceReader> reader;
        check(MFCreateSourceReaderFromURL(path.c_str(), attributes.Get(), &reader),
              "Read test MP4");
        DWORD index =
            stream == 0 ? MF_SOURCE_READER_FIRST_VIDEO_STREAM : MF_SOURCE_READER_FIRST_AUDIO_STREAM;
        ComPtr<IMFMediaType> decoded;
        check(MFCreateMediaType(&decoded), "Create decoded media type");
        check(
            decoded->SetGUID(MF_MT_MAJOR_TYPE, stream == 0 ? MFMediaType_Video : MFMediaType_Audio),
            "Set decoded major type");
        check(
            decoded->SetGUID(MF_MT_SUBTYPE, stream == 0 ? MFVideoFormat_RGB32 : MFAudioFormat_PCM),
            "Set decoded format");
        check(reader->SetCurrentMediaType(index, nullptr, decoded.Get()),
              "Configure actual media decoding");
        LONGLONG last = -1;
        for (;;) {
            DWORD flags = 0;
            LONGLONG time = 0;
            ComPtr<IMFSample> sample;
            check(reader->ReadSample(index, 0, nullptr, &flags, &time, &sample),
                  "Decode test sample");
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
                break;
            if (!sample)
                continue;
            if (time < last)
                throw std::runtime_error("Decoded timestamps regress");
            last = time;
            DWORD length = 0;
            check(sample->GetTotalLength(&length), "Read decoded sample length");
            if (!length)
                throw std::runtime_error("Decoded sample is empty");
            counts[stream]++;
        }
        ends[stream] = last;
    }
    if (counts[0] < 80 || counts[1] < 10)
        throw std::runtime_error("Self-test missing decoded video or audio");
    if (std::llabs(ends[0] - ends[1]) > 5000000)
        throw std::runtime_error("Self-test A/V duration mismatch");
    std::cout << "SELFTEST_OK decoded_video=" << counts[0] << " decoded_audio=" << counts[1]
              << std::endl;
    return 0;
}

#include "MidiFilePlayer.h"
#include "FluidSynthModel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>

namespace {
constexpr size_t maxFileBytes{16 * 1024 * 1024};
constexpr size_t maxEvents{250000};
constexpr double maxDuration{24.0 * 60.0 * 60.0};
constexpr double minimumLoopSeconds{0.05};

bool systemReset(const juce::MidiMessage& m) {
    if (!m.isSysEx()) return false;
    return FluidSynthModel::isSystemResetSysex(m.getSysExData(), m.getSysExDataSize());
}

// Strict SMF reader: JUCE's reader silently truncates some damaged tracks and
// reorders equal-tick note pairs. Keeping file order matters for program/RPN MIDI.
struct Reader {
    const juce::uint8* data;
    size_t position{}, end;
    bool take(size_t count) const { return count <= end - position; }
    juce::uint8 byte() { return data[position++]; }
    bool vlq(juce::uint32& value) {
        value = 0;
        for (int i = 0; i < 4; ++i) {
            if (!take(1)) return false;
            const auto b = byte();
            value = (value << 7) | (b & 127u);
            if ((b & 128u) == 0) return true;
        }
        return false;
    }
    juce::uint32 big(int bytes) {
        juce::uint32 value{};
        for (int i = 0; i < bytes; ++i) value = (value << 8) | byte();
        return value;
    }
};
}

MidiFilePlayer::MidiFilePlayer(juce::AudioProcessor& owner,
                             std::function<void(juce::MidiBuffer&)> setup)
    : processor(owner), applySetup(std::move(setup)) {}

std::shared_ptr<MidiFilePlayer::Song> MidiFilePlayer::readSong(const juce::File& file,
                                                            juce::String& error) {
    const auto fail = [&error](const char* message) -> std::shared_ptr<Song> {
        error = message; return {};
    };
    if (!file.existsAsFile()) return fail("The MIDI file could not be opened.");
    if (file.getSize() <= 0 || file.getSize() > static_cast<juce::int64>(maxFileBytes))
        return fail("Choose a MIDI file smaller than 16 MB.");
    juce::MemoryBlock bytes;
    if (!file.loadFileAsData(bytes) || bytes.getSize() > maxFileBytes)
        return fail("The MIDI file could not be read.");
    Reader r{static_cast<const juce::uint8*>(bytes.getData()), 0, bytes.getSize()};
    if (!r.take(14) || r.big(4) != 0x4d546864u) return fail("This is not a Standard MIDI file.");
    const auto headerSize = r.big(4);
    if (headerSize < 6 || !r.take(headerSize)) return fail("The MIDI header is incomplete.");
    const auto format = r.big(2), tracks = r.big(2), division = r.big(2);
    if (format > 1) return fail("MIDI format 2 contains separate songs. Choose a format 0 or 1 file.");
    if (tracks == 0 || tracks > 1024 || (format == 0 && tracks != 1))
        return fail("The MIDI track count is invalid.");
    r.position += headerSize - 6;
    double ticksPerSecond{};
    if ((division & 0x8000u) != 0) {
        const int fps = 256 - static_cast<int>(division >> 8);
        const int subframes = static_cast<int>(division & 255u);
        if ((fps != 24 && fps != 25 && fps != 29 && fps != 30) || subframes == 0)
            return fail("The MIDI SMPTE timing is invalid.");
        ticksPerSecond = (fps == 29 ? 30000.0 / 1001.0 : static_cast<double>(fps)) * subframes;
    } else if (division == 0) {
        return fail("The MIDI tick resolution is invalid.");
    }
    struct Tempo { juce::uint64 tick; juce::uint32 micros; };
    std::vector<Tempo> tempos;
    auto result = std::make_shared<Song>();
    result->fileName = file.getFileName();
    result->tracks = static_cast<int>(tracks);
    unsigned int channelMask{};
    juce::uint64 lastTick{};
    size_t parsedEvents{};
    bool hasNotes{};
    for (juce::uint32 track = 0; track < tracks; ++track) {
        if (!r.take(8) || r.big(4) != 0x4d54726bu) return fail("A MIDI track header is missing.");
        const auto trackBytes = r.big(4);
        if (!r.take(trackBytes)) return fail("A MIDI track is incomplete.");
        Reader t{r.data, r.position, r.position + trackBytes};
        r.position += trackBytes;
        juce::uint64 tick{};
        juce::uint8 running{};
        bool ended{};
        while (t.position < t.end) {
            juce::uint32 delta{};
            if (!t.vlq(delta) || !t.take(1)) return fail("A MIDI event is incomplete.");
            tick += delta;
            if (++parsedEvents > maxEvents) return fail("This MIDI file has more than 250,000 events.");
            auto statusByte = t.data[t.position];
            if (statusByte >= 128) ++t.position;
            else if (running != 0) statusByte = running;
            else return fail("A MIDI event has invalid running status.");
            if (statusByte >= 0x80 && statusByte <= 0xef) {
                running = statusByte;
                const int count = (statusByte & 0xf0) == 0xc0 || (statusByte & 0xf0) == 0xd0 ? 1 : 2;
                if (!t.take(static_cast<size_t>(count))) return fail("A MIDI channel event is incomplete.");
                juce::uint8 raw[3]{statusByte, t.byte(), 0};
                if (count == 2) raw[2] = t.byte();
                if (raw[1] >= 128 || raw[2] >= 128) return fail("A MIDI event has invalid data bytes.");
                juce::MidiMessage message{raw, count + 1};
                hasNotes = hasNotes || message.isNoteOn();
                channelMask |= 1u << (message.getChannel() - 1);
                result->events.push_back({std::move(message), 0.0, tick});
                result->bufferBytes += static_cast<size_t>(count + 1 + 6);
            } else if (statusByte == 0xff) {
                running = 0;
                if (!t.take(1)) return fail("A MIDI metadata event is incomplete.");
                const auto type = t.byte();
                juce::uint32 count{};
                if (!t.vlq(count) || !t.take(count)) return fail("A MIDI metadata event is incomplete.");
                if (type == 0x51) {
                    if (count != 3) return fail("A MIDI tempo event is invalid.");
                    const auto micros = t.big(3);
                    if (micros == 0) return fail("A MIDI tempo cannot be zero.");
                    tempos.push_back({tick, micros});
                } else {
                    t.position += count;
                }
                if (type == 0x2f) {
                    if (count != 0 || t.position != t.end) return fail("The MIDI end-of-track event is invalid.");
                    ended = true;
                }
            } else if (statusByte == 0xf0) {
                running = 0;
                juce::uint32 count{};
                if (!t.vlq(count) || !t.take(count) || count == 0)
                    return fail("A MIDI SysEx event is incomplete.");
                if (count > 65534) return fail("A MIDI SysEx packet is too large (maximum 65,534 bytes).");
                if (t.data[t.position + count - 1] != 0xf7)
                    return fail("Split SysEx packets are not supported. Choose a MIDI with complete SysEx events.");
                result->events.push_back({juce::MidiMessage::createSysExMessage(t.data + t.position,
                    static_cast<int>(count - 1)), 0.0, tick});
                result->bufferBytes += static_cast<size_t>(count + 7);
                t.position += count;
            } else {
                return fail("This MIDI contains an unsupported system or escaped SysEx event.");
            }
        }
        if (!ended) return fail("A MIDI track is missing its end-of-track event.");
        lastTick = juce::jmax(lastTick, tick);
    }
    if (r.position != r.end) return fail("Unexpected data follows the MIDI tracks.");
    if (!hasNotes) return fail("This MIDI file contains no notes to play.");
    std::stable_sort(result->events.begin(), result->events.end(),
        [](const Event& a, const Event& b) { return a.tick < b.tick; });
    std::stable_sort(tempos.begin(), tempos.end(),
        [](const Tempo& a, const Tempo& b) { return a.tick < b.tick; });
    // Display tempo follows the same merged map as PPQ timing. SMPTE files
    // still expose their tempo metadata, although frame timing is independent.
    result->tempoMap.push_back({0.0, 120.0});
    juce::uint64 displayTick{};
    double displaySeconds{}, displaySecondsPerTick{0.5 / static_cast<double>(division)};
    for (const auto& tempo : tempos) {
        if (ticksPerSecond > 0)
            displaySeconds = static_cast<double>(tempo.tick) / ticksPerSecond;
        else
            displaySeconds += static_cast<double>(tempo.tick - displayTick) * displaySecondsPerTick;
        const TempoPoint point{displaySeconds, 60000000.0 / static_cast<double>(tempo.micros)};
        if (tempo.tick == displayTick)
            result->tempoMap.back() = point;
        else
            result->tempoMap.push_back(point);
        displayTick = tempo.tick;
        displaySecondsPerTick = static_cast<double>(tempo.micros) / (1000000.0 * division);
    }
    size_t tempoIndex{};
    juce::uint64 previousTick{};
    double previousSeconds{}, secondsPerTick{0.5 / static_cast<double>(division)};
    const auto secondsFor = [&](juce::uint64 tick) mutable {
        if (ticksPerSecond > 0) return static_cast<double>(tick) / ticksPerSecond;
        while (tempoIndex < tempos.size() && tempos[tempoIndex].tick <= tick) {
            const auto& tempo = tempos[tempoIndex++];
            previousSeconds += static_cast<double>(tempo.tick - previousTick) * secondsPerTick;
            previousTick = tempo.tick;
            secondsPerTick = static_cast<double>(tempo.micros) / (1000000.0 * division);
        }
        return previousSeconds + static_cast<double>(tick - previousTick) * secondsPerTick;
    };
    // Copy the stateful lambda: each call advances the tempo map monotonically.
    auto convert = secondsFor;
    for (auto& event : result->events) {
        event.seconds = convert(event.tick);
        event.message.setTimeStamp(event.seconds);
    }
    result->duration = convert(lastTick);
    if (!std::isfinite(result->duration) || result->duration <= 0 || result->duration > maxDuration)
        return fail("Choose a MIDI with a length between zero and 24 hours.");
    for (int ch = 0; ch < 16; ++ch)
        if ((channelMask & (1u << ch)) != 0) ++result->channels;
    float largestBin{};
    for (const auto& event : result->events)
        if (event.message.isNoteOn()) {
            const auto bin = static_cast<size_t>(juce::jlimit(0, 255,
                static_cast<int>(event.seconds * 256.0 / result->duration)));
            largestBin = juce::jmax(largestBin, ++result->noteDensity[bin]);
        }
    if (largestBin > 0)
        for (auto& value : result->noteDensity) value /= largestBin;
    error.clear();
    return result;
}

void MidiFilePlayer::addPanic(juce::MidiBuffer& buffer, int sample) {
    for (int ch = 1; ch <= 16; ++ch) {
        buffer.addEvent(juce::MidiMessage::controllerEvent(ch, 64, 0), sample);
        buffer.addEvent(juce::MidiMessage::controllerEvent(ch, 66, 0), sample);
        buffer.addEvent(juce::MidiMessage::allSoundOff(ch), sample);
        buffer.addEvent(juce::MidiMessage::allNotesOff(ch), sample);
    }
}

juce::MidiBuffer MidiFilePlayer::makeSetup(const Song& source, double position, bool restoreNotes) {
    juce::MidiBuffer setup;
    setup.ensureSize(source.bufferBytes + 32768);
    addPanic(setup, 0);
    // Juicy16's default bank interpretation is GS. GM On would silently make
    // FluidSynth ignore Bank Select in files without their own mode reset.
    const juce::uint8 reset[]{0x41, 0x7f, 0x42, 0x12, 0x40, 0, 0x7f, 0, 0x41};
    setup.addEvent(juce::MidiMessage::createSysExMessage(reset, 9), 0);
    // Explicitly reset pending bank selection as well as the sounding program.
    for (int ch = 1; ch <= 16; ++ch) {
        setup.addEvent(juce::MidiMessage::controllerEvent(ch, 0, 0), 0);
        setup.addEvent(juce::MidiMessage::controllerEvent(ch, 32, 0), 0);
        setup.addEvent(juce::MidiMessage::programChange(ch, 0), 0);
    }
    struct Note { juce::uint8 velocity{}; bool down{}, captured{}; int heldBy{}; };
    std::array<std::array<Note, 128>, 16> notes{};
    std::array<int, 16> sustain{}, sostenuto{};
    // Ordinary CC/bend/pressure writes can collapse between resets. Preserve
    // bank/program and RPN/NRPN sequences, including Data Increment/Decrement.
    std::array<std::array<int, 131>, 16> latest;
    for (auto& channel : latest) channel.fill(-1);
    std::vector<size_t> ordered;
    std::vector<size_t> pending;
    const auto flush = [&] {
        for (const auto& channel : latest)
            for (const int i : channel) if (i >= 0) pending.push_back(static_cast<size_t>(i));
        std::sort(pending.begin(), pending.end());
        ordered.insert(ordered.end(), pending.begin(), pending.end());
        pending.clear();
        for (auto& channel : latest) channel.fill(-1);
    };
    for (size_t i = 0; i < source.events.size() && source.events[i].seconds < position; ++i) {
        const auto& m = source.events[i].message;
        if (m.isSysEx()) {
            flush(); ordered.push_back(i);
            if (systemReset(m)) { notes = {}; sustain = {}; sostenuto = {}; }
            continue;
        }
        const auto ch = static_cast<size_t>(m.getChannel() - 1);
        if (m.isNoteOn()) {
            notes[ch][static_cast<size_t>(m.getNoteNumber())] = {m.getVelocity(), true, false, 0};
        } else if (m.isNoteOff()) {
            auto& note = notes[ch][static_cast<size_t>(m.getNoteNumber())];
            note.down = false;
            if (sostenuto[ch] >= 64 && note.captured) note.heldBy = 1;
            else if (sustain[ch] >= 64) note.heldBy = 2;
            else note = {};
        } else {
            int slot{-1};
            if (m.isController()) {
                const int cc = m.getControllerNumber(), value = m.getControllerValue();
                if (cc == 64 || cc == 66) {
                    if (cc == 64) sustain[ch] = value;
                    else sostenuto[ch] = value;
                    for (auto& note : notes[ch]) {
                        if (cc == 66 && value >= 64 && note.velocity != 0) note.captured = true;
                        if (value < 64 && note.heldBy == (cc == 64 ? 2 : 1)) note = {};
                        if (cc == 66 && value < 64) note.captured = false;
                    }
                } else if (cc == 120) {
                    notes[ch] = {};
                } else if (cc == 121) {
                    sustain[ch] = sostenuto[ch] = 0;
                    for (auto& note : notes[ch]) {
                        if (!note.down) note = {};
                        note.captured = false;
                    }
                } else if (cc >= 123) {
                    for (auto& note : notes[ch]) {
                        note.down = false;
                        if (sostenuto[ch] >= 64 && note.captured) note.heldBy = 1;
                        else if (sustain[ch] >= 64) note.heldBy = 2;
                        else note = {};
                    }
                }
                if (cc == 121) flush();
                if (cc == 0 || cc == 32 || cc == 6 || cc == 38 || cc >= 96) pending.push_back(i);
                else slot = cc;
            } else if (m.isPitchWheel()) slot = 128;
            else if (m.isChannelPressure()) slot = 129;
            else if (m.isAftertouch()) { pending.push_back(i); continue; }
            else if (m.isProgramChange()) pending.push_back(i);
            if (slot >= 0) latest[ch][static_cast<size_t>(slot)] = static_cast<int>(i);
        }
    }
    flush();
    for (const auto i : ordered) setup.addEvent(source.events[i].message, 0);
    // Replay has no historical notes. Reconstruct pedal capture in the order the
    // synth needs: captured notes, sostenuto down, other notes, then note-offs.
    for (size_t ch = 0; ch < 16; ++ch) {
        const int channel = static_cast<int>(ch) + 1;
        setup.addEvent(juce::MidiMessage::controllerEvent(channel, 64, 0), 0);
        setup.addEvent(juce::MidiMessage::controllerEvent(channel, 66, 0), 0);
        if (!restoreNotes) continue;
        const auto captured = [](const Note& note) { return note.down ? note.captured : note.heldBy == 1; };
        for (int key = 0; key < 128; ++key) {
            const auto& note = notes[ch][static_cast<size_t>(key)];
            if (note.velocity != 0 && captured(note))
                setup.addEvent(juce::MidiMessage::noteOn(channel, key, note.velocity), 0);
        }
        setup.addEvent(juce::MidiMessage::controllerEvent(channel, 66, sostenuto[ch]), 0);
        for (int key = 0; key < 128; ++key) {
            const auto& note = notes[ch][static_cast<size_t>(key)];
            if (note.velocity != 0 && !captured(note))
                setup.addEvent(juce::MidiMessage::noteOn(channel, key, note.velocity), 0);
        }
        setup.addEvent(juce::MidiMessage::controllerEvent(channel, 64, sustain[ch]), 0);
        for (int key = 0; key < 128; ++key)
            if (notes[ch][static_cast<size_t>(key)].velocity != 0 && !notes[ch][static_cast<size_t>(key)].down)
                setup.addEvent(juce::MidiMessage::noteOff(channel, key), 0);
    }
    return setup;
}

bool MidiFilePlayer::validateFile(const juce::File& file, juce::String& error) {
    return readSong(file, error) != nullptr;
}

bool MidiFilePlayer::loadFile(const juce::File& file, juce::String& error) {
    auto replacement = readSong(file, error);
    if (!replacement) return false;
    auto initialSetup = makeSetup(*replacement, 0, false);
    auto initialLoopSetup = makeSetup(*replacement, 0, true);
    const juce::ScopedLock lock{processor.getCallbackLock()};
    applySetup(initialSetup);
    song = std::move(replacement);
    const double oldSpeed = status.speed;
    const auto nextRevision = status.revision + 1;
    status = {};
    status.revision = nextRevision;
    status.speed = oldSpeed;
    status.fileName = song->fileName;
    status.durationSeconds = status.loopEndSeconds = song->duration;
    status.trackCount = song->tracks;
    status.channelCount = song->channels;
    cursor = 0;
    pendingEndPanic = false;
    pendingLoopSetup = false;
    loopSetup.swapWith(initialLoopSetup);
    reserveBuffers();
    return true;
}

void MidiFilePlayer::locateCursor() {
    cursor = static_cast<size_t>(std::lower_bound(song->events.begin(), song->events.end(), status.positionSeconds,
        [](const Event& event, double position) { return event.seconds < position; }) - song->events.begin());
}

void MidiFilePlayer::moveTo(double position, bool startPlaying) {
    if (!std::isfinite(position)) return;
    std::shared_ptr<const Song> source;
    { const juce::ScopedLock lock{processor.getCallbackLock()}; source = song; }
    if (!source) return;
    position = juce::jlimit(0.0, source->duration, position);
    const bool willPlay = startPlaying && position < source->duration;
    auto setup = makeSetup(*source, position, willPlay);
    const juce::ScopedLock lock{processor.getCallbackLock()};
    if (song != source) return;
    status.positionSeconds = position;
    status.playing = willPlay;
    pendingEndPanic = false;
    pendingLoopSetup = false;
    locateCursor();
    applySetup(setup);
}

void MidiFilePlayer::play() {
    auto current = getStatus();
    if (current.fileName.isEmpty() || current.playing) return;
    double position = current.positionSeconds;
    if (position >= current.durationSeconds || (current.looping && position >= current.loopEndSeconds))
        position = current.looping ? current.loopStartSeconds : 0.0;
    moveTo(position, true);
}
void MidiFilePlayer::pause() {
    const juce::ScopedLock lock{processor.getCallbackLock()};
    status.playing = false;
    pendingEndPanic = false;
    pendingLoopSetup = false;
    juce::MidiBuffer panic; addPanic(panic, 0); applySetup(panic);
}
void MidiFilePlayer::stop() { moveTo(0, false); }
void MidiFilePlayer::seek(double position) { moveTo(position, getStatus().playing); }
void MidiFilePlayer::setSpeed(double factor) {
    if (!std::isfinite(factor)) return;
    const juce::ScopedLock lock{processor.getCallbackLock()};
    status.speed = juce::jlimit(0.25, 4.0, factor);
}
void MidiFilePlayer::setLooping(bool enabled) {
    const juce::ScopedLock lock{processor.getCallbackLock()};
    status.looping = enabled && status.durationSeconds >= minimumLoopSeconds;
}
bool MidiFilePlayer::setLoopRange(double start, double end) {
    if (!std::isfinite(start) || !std::isfinite(end)) return false;
    std::shared_ptr<const Song> source;
    { const juce::ScopedLock lock{processor.getCallbackLock()}; source = song; }
    if (!source) return false;
    start = juce::jlimit(0.0, source->duration, start);
    end = juce::jlimit(0.0, source->duration, end);
    if (end - start < juce::jmin(minimumLoopSeconds, source->duration)) return false;
    auto setup = makeSetup(*source, start, true);
    // A short repeating section cannot replay an unbounded RPN/SysEx history
    // inside the audio callback. Keep the previously valid range on rejection.
    if (setup.getNumEvents() > 8192) return false;
    const juce::ScopedLock lock{processor.getCallbackLock()};
    if (song != source) return false;
    status.loopStartSeconds = start; status.loopEndSeconds = end;
    loopSetup.swapWith(setup);
    reserveBuffers();
    return true;
}
MidiFilePlayer::Status MidiFilePlayer::getStatus() const {
    const juce::ScopedLock lock{processor.getCallbackLock()};
    auto current = status;
    if (song != nullptr) {
        const auto after = std::upper_bound(song->tempoMap.begin(), song->tempoMap.end(),
            current.positionSeconds,
            [](double position, const TempoPoint& point) { return position < point.seconds; });
        if (after != song->tempoMap.begin()) current.tempoBpm = std::prev(after)->bpm;
    }
    current.playbackTempoBpm = current.tempoBpm * current.speed;
    return current;
}
std::array<float, 256> MidiFilePlayer::getNoteDensity() const {
    const juce::ScopedLock lock{processor.getCallbackLock()};
    return song != nullptr ? song->noteDensity : std::array<float, 256>{};
}
void MidiFilePlayer::prepare(int blockSize, double rate) {
    const juce::ScopedLock lock{processor.getCallbackLock()};
    maximumBlockSize = juce::jmax(1, blockSize);
    preparedSampleRate = std::isfinite(rate) && rate > 0 ? rate : 48000;
    reserveBuffers();
}
void MidiFilePlayer::reserveBuffers() {
    if (!song) { renderBuffer.ensureSize(32768); return; }
    const double span = juce::jmax(1.0e-12, status.loopEndSeconds - status.loopStartSeconds);
    const double loops = std::ceil(maximumBlockSize * 4.0 / (preparedSampleRate * span)) + 2;
    size_t regionBytes{};
    for (const auto& event : song->events)
        if (event.seconds >= status.loopStartSeconds && event.seconds < status.loopEndSeconds)
            regionBytes += static_cast<size_t>(event.message.getRawDataSize() + 6);
    const auto bytes = song->bufferBytes + static_cast<size_t>(juce::jmin(loops, 64.0))
        * (static_cast<size_t>(loopSetup.data.size()) + regionBytes + 32768);
    renderBuffer.ensureSize(bytes);
}

void MidiFilePlayer::appendNextBlock(juce::MidiBuffer& buffer, int numSamples, double rate) {
    if (pendingEndPanic && numSamples > 0) { addPanic(buffer, 0); pendingEndPanic = false; }
    if (pendingLoopSetup && numSamples > 0) {
        addPanic(buffer, 0); buffer.addEvents(loopSetup, 0, -1, 0); pendingLoopSetup = false;
    }
    if (!song || !status.playing || numSamples <= 0 || !std::isfinite(rate) || rate <= 0) return;
    const double secondsPerSample = status.speed / rate;
    double frame{};
    int loops{};
    while (frame < numSamples && status.playing) {
        const double end = status.looping ? status.loopEndSeconds : song->duration;
        if (status.positionSeconds >= end) {
            const int sample = static_cast<int>(std::floor(frame + 1.0e-8));
            if (!status.looping || ++loops > 64) {
                if (sample < numSamples) addPanic(buffer, sample);
                else pendingEndPanic = true;
                status.playing = false; status.positionSeconds = end; break;
            }
            status.positionSeconds = status.loopStartSeconds;
            locateCursor();
            if (sample < numSamples) {
                addPanic(buffer, sample); buffer.addEvents(loopSetup, 0, -1, sample);
            } else pendingLoopSetup = true;
            continue;
        }
        const double availableFrames = (end - status.positionSeconds) / secondsPerSample;
        const double length = juce::jmin(static_cast<double>(numSamples) - frame, availableFrames);
        const double from = status.positionSeconds;
        const double to = from + length * secondsPerSample;
        while (cursor < song->events.size()) {
            const auto& event = song->events[cursor];
            if (event.seconds >= to - 1.0e-12 || event.seconds >= end) break;
            const double eventFrame = frame + (event.seconds - from) / secondsPerSample;
            // Use one quantisation rule for events and loop boundaries. Floor
            // keeps eligible events in their half-open block, including the
            // last fractional sample before end-of-song or marker B.
            const int sample = juce::jlimit(0, numSamples - 1,
                static_cast<int>(std::floor(eventFrame + 1.0e-8)));
            buffer.addEvent(event.message, sample);
            ++cursor;
        }
        frame += length;
        status.positionSeconds = availableFrames <= length + 1.0e-8 ? end : to;
    }
    if (!status.looping && status.positionSeconds >= song->duration && status.playing) {
        status.playing = false;
        pendingEndPanic = true;
    }
    // A boundary exactly at the end belongs to the next callback. End-of-song
    // cleanup can occur there without cutting the final audio sample short.
}

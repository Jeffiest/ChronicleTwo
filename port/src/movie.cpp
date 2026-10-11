#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// The game headers keep MWCC's pragmas, which clang names but ignores.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#include "movie.hpp"
#pragma clang diagnostic pop

#include "cdvd_file.hpp"
#include "movie_port.hpp"
#include "platform/clock.hpp"

// The host replacement for ps2/src/movie.cpp. Retail decodes the PSS through the IPU and shows
// its output by vblank DMA from three threads; the host has none of that hardware and cannot
// link a software MPEG-2 decoder (see movie_port.hpp), so it plays the stream on the game's own
// clock instead: Load reads the stream's presentation time stamps, Play anchors the playout and
// SwitchThread lets it run out. The game's frame counters keep the captions and overlays timed.
// CMovie's allocation sizes and order still match retail's, because the callers plan the memory
// stacks they pass in around them, and the sceMpeg* surface stays defined so the unit's PS2 half
// still links; nothing calls those entries while the timing-only player runs.
//
// The playout state lives in file statics the way retail's unit keeps its own (videoDec, infile,
// readBuf, isStarted, ...): only one movie plays at a time, and a fresh Load rebinds it.

namespace {

/**
 * One playback's position on the game's clock.
 */
struct Playback {
    std::string           name;            /**< Name the game passed, for a decoder backend. */
    cdvd::File            file;            /**< Disc entry of the PSS, when one was found. */
    movie_port::PssTiming timing;          /**< Timing read from the stream's headers. */
    double                duration = 0.0;  /**< Seconds, the PTS span plus one frame. */
    std::int64_t          start_tick = 0;  /**< Clock tick the playout is anchored at. */
    bool                  file_ok = false; /**< Whether the PSS is in the disc index. */
    bool                  decoded = false; /**< Whether a decoder backend was opened. */
    bool                  playing = false; /**< Whether Play has been called. */
    bool                  started = false; /**< Whether display may begin (at once). */
    bool                  ended = false;   /**< Whether the stream has run out. */
    bool                  loop = false;    /**< Whether playback wraps at the end. */
};

Playback playback;

/**
 * The movie the playout belongs to; Load rebinds it.
 */
CMovie *player;

movie_port::VideoSource *video_source;

/**
 * Bytes read from each end of a stream to find its timing.
 */
constexpr std::uint64_t kTimingSpan = 0x40000;

bool ReadSpan(const cdvd::File &file, std::uint64_t offset, std::vector<std::uint8_t> &bytes) {
    std::uint64_t length = std::min<std::uint64_t>(file.size - offset, kTimingSpan);

    bytes.resize(static_cast<std::size_t>(length));
    return length == 0 || cdvd::ReadAt(file, offset, bytes.data(), length);
}

void ReadTiming(const cdvd::File &file, movie_port::PssTiming &timing) {
    std::vector<std::uint8_t> span;

    if (ReadSpan(file, 0, span)) {
        movie_port::ScanPssSpan(span.data(), span.size(), timing);
    }

    if (file.size > kTimingSpan && ReadSpan(file, file.size - kTimingSpan, span)) {
        movie_port::ScanPssSpan(span.data(), span.size(), timing);
    }
}

/**
 * One stream's callback registration, as sceMpegAddStrCallback left it.
 */
struct MpegStreamSlot {
    sceMpegCallback callback = nullptr; /**< Registered callback, or null. */
    void           *user = nullptr;     /**< Data the callback is passed. */
    int             ch = 0;             /**< Channel the callback was registered for. */
};

/**
 * The host MPEG decoder's state: the callbacks carried by sceMpeg::sys, as the headers intend.
 */
struct MpegHost {
    sceMpegCallback event[7] = {};       /**< Event callbacks, indexed by sceMpegCbType. */
    void           *event_user[7] = {};  /**< Data the event callbacks are passed. */
    MpegStreamSlot  stream[5];           /**< Demux callbacks, indexed by sceMpegStrType. */
};

MpegHost *HostOf(sceMpeg *mp) {
    return mp != nullptr ? static_cast<MpegHost *>(mp->sys) : nullptr;
}

} // namespace

void movie_port::SetVideoSource(VideoSource *source) {
    video_source = source;
}

movie_port::VideoSource *movie_port::GetVideoSource() {
    return video_source;
}

std::int64_t movie_port::PtsAt(const std::uint8_t *p) {
    return (std::int64_t) ((p[0] >> 1) & 0x07) << 30 | (std::int64_t) p[1] << 22 |
           (std::int64_t) ((p[2] >> 1) & 0x7F) << 15 | (std::int64_t) p[3] << 7 | (p[4] >> 1);
}

void movie_port::ScanPssSpan(const std::uint8_t *data, std::size_t size, PssTiming &timing) {
    static constexpr double kFrameRates[] = {0.0, 24000.0 / 1001.0, 24.0, 25.0,
                                             30000.0 / 1001.0, 30.0, 50.0, 60000.0 / 1001.0, 60.0};

    for (std::size_t i = 0; i + 4 <= size; i++) {
        if (data[i] != 0 || data[i + 1] != 0 || data[i + 2] != 1) {
            continue;
        }

        int code = data[i + 3];

        if (code == 0xB3) {
            if (timing.width != 0 || i + 8 > size) {
                continue;
            }

            int width = (data[i + 4] << 4) | (data[i + 5] >> 4);
            int height = ((data[i + 5] & 0xF) << 8) | data[i + 6];

            if (width > 0 && height > 0) {
                timing.width = width;
                timing.height = height;
            }

            int rate = data[i + 7] & 0xF;

            if (rate >= 1 && rate <= 8) {
                timing.frame_hz = kFrameRates[rate];
            }
        } else if (code >= 0xE0 && code <= 0xEF) {
            if (i + 9 > size) {
                break;
            }

            const std::uint8_t *header = &data[i + 6];
            std::size_t         at;

            if ((header[0] & 0xC0) == 0x80) { // MPEG-2 PES header
                if (((header[1] >> 6) & 3) < 2 || i + 14 > size) {
                    continue;
                }

                at = i + 9;
            } else if (((header[0] & 0xF0) == 0x20 || (header[0] & 0xF0) == 0x30) && i + 11 <= size) {
                at = i + 6; // MPEG-1 PES header
            } else {
                continue;
            }

            if (((data[at] & 1) & (data[at + 2] & 1) & (data[at + 4] & 1)) == 0) {
                continue;
            }

            std::int64_t pts = PtsAt(&data[at]);
            timing.last_pts = pts;

            if (timing.first_pts < 0) {
                timing.first_pts = pts;
            }
        }
    }
}

void CMovie::Load(char *name, mgCMemory **memory, int width, int height, bool with_audio, bool loop,
                  bool init_sound) {
    (void) with_audio;
    (void) init_sound;

    // Retail's allocation order and sizes; the callers' memory stacks are planned around them.
    vo_data = (VoData *) memory[0]->stAlloc64(GetVoBufDataSize() / 16);
    vi_buf_data = memory[1]->stAlloc64(GetViBufDataSize() / 16);
    vi_buf_tag = memory[2]->stAlloc64(GetViBufTagSize() / 16);
    mpeg_work = (u_char *) memory[3]->stAlloc64(GetMpegWorkSize(width, height) / 16);
    memory[4]->stAlloc64(GetReadBufSize() / 16);

    for (int i = 0; i < 2; i++) {
        image_tag[0][i] = (u_int *) memory[5]->stAlloc64(GetTagProgSize(width, height) / 16);
        image_tag[1][i] = (u_int *) memory[5]->stAlloc64(GetTagProgSize(width, height) / 16);
    }

    player = this;
    playback = Playback{};
    playback.name = name != nullptr ? name : "";
    playback.loop = loop;
    is_playing = false;

    // Retail always opens the disc's MOVIE directory; the extracted tree serves the name through
    // the same index. A missing file leaves the playout ended so the mode skips, where retail's
    // strFileOpen retries forever.
    std::string path = "MOVIE/";
    path += playback.name;
    playback.file_ok = cdvd::Find(path, playback.file);

    if (!playback.file_ok) {
        return;
    }

    ReadTiming(playback.file, playback.timing);

    if (playback.timing.first_pts >= 0 && playback.timing.last_pts > playback.timing.first_pts) {
        double seconds = (double) (playback.timing.last_pts - playback.timing.first_pts) / 90000.0;
        double frame = playback.timing.frame_hz > 0.0 ? 1.0 / playback.timing.frame_hz : 0.0;

        playback.duration = seconds + frame;
    }

    if (video_source != nullptr) {
        playback.decoded = video_source->Open(playback.name.c_str());
    }
}

void CMovie::Load(char *name, mgCMemory *memory, int width, int height, bool with_audio, bool loop) {
    mgCMemory *pools[6] = {memory, memory, memory, memory, memory, memory};

    Load(name, pools, width, height, with_audio, loop, true);
}

void CMovie::Load(char *name, mgCMemory *memory, int width, int height, bool with_audio, bool loop,
                  bool init_sound) {
    mgCMemory *pools[6] = {memory, memory, memory, memory, memory, memory};

    Load(name, pools, width, height, with_audio, loop, init_sound);
}

void CMovie::Play(char *path) {
    if (player != this) {
        return;
    }

    (void) path; // Retail draws decoded frames into this texture; the host has no frames to draw.

    playback.playing = true;
    playback.started = true;
    playback.ended = !playback.file_ok || playback.duration <= 0.0;
    playback.start_tick = ClockTickCount();
    is_playing = true;
}

void CMovie::SwitchThread() {
    if (player != this || !playback.playing || playback.ended) {
        return;
    }

    double rate = ClockTickRate();

    if (rate <= 0.0) {
        return;
    }

    double elapsed = (double) (ClockTickCount() - playback.start_tick) / rate;

    if (elapsed < playback.duration) {
        return;
    }

    if (!playback.loop) {
        playback.ended = true;
        return;
    }

    // Retail seeks the stream back to its start; the host moves the playout's anchor forward.
    std::int64_t period = std::max<std::int64_t>((std::int64_t) (playback.duration * rate), 1);

    playback.start_tick += period * (std::int64_t) (elapsed / playback.duration);
}

void CMovie::Term() {
    if (player == this) {
        if (playback.decoded && video_source != nullptr) {
            video_source->Close();
        }

        playback.decoded = false;
        playback.playing = false;
        playback.ended = true;
    }

    is_playing = false;
}

int CMovie::EndCheck() {
    return player == this && playback.ended ? 1 : 0;
}

int CMovie::IsStarted() {
    return player == this && playback.started ? 1 : 0;
}

int CMovie::GetVoBufDataSize() {
    return 0x1C0000;
}

int CMovie::GetViBufDataSize() {
    return 0x80000;
}

int CMovie::GetViBufTagSize() {
    return 0x1010;
}

int CMovie::GetMpegWorkSize(int width, int height) {
    return width * height * 9 / 2 + 0x1768;
}

int CMovie::GetReadBufSize() {
    return 0x50050;
}

int CMovie::GetTagProgSize(int width, int height) {
    int macroblocks = (width >> 4) * height;
    int groups = macroblocks >> 4;
    int bytes = ((groups * 6) + 0x6E) * 4 + 0x3F;

    return (bytes >> 6) << 8;
}

int CMovie::videoDecCreate(VideoDec *dec, u_char *mpeg_buffer, int mpeg_size, u_long128 *data,
                           u_long128 *tag, int sectors, TimeStamp *ts, int ts_count) {
    (void) data;
    (void) tag;
    (void) sectors;
    (void) ts;
    (void) ts_count;

    dec->state = VIDEO_DEC_STATE_NORMAL;
    dec->unk_ac = 0;
    dec->hid_endimage = -1;
    dec->hid_vblank = -1;
    return sceMpegCreate(&dec->mpeg, mpeg_buffer, mpeg_size);
}

int CMovie::videoDecSetStream(VideoDec *dec, int id, int param,
                              int (*callback)(sceMpeg *, sceMpegCbData *, void *), void *user) {
    sceMpegAddStrCallback(&dec->mpeg, (sceMpegStrType) (id & 0xFF), param, callback, user);
    return 1;
}

int CMovie::videoDecDelete(VideoDec *dec) {
    dec->state = VIDEO_DEC_STATE_END;
    return sceMpegDelete(&dec->mpeg);
}

int CMovie::videoDecFlush(VideoDec *dec) {
    if (dec->state == VIDEO_DEC_STATE_NORMAL) {
        dec->state = VIDEO_DEC_STATE_FLUSH;
    }

    return 1;
}

extern "C" {

int sceMpegInit(void) {
    return 0;
}

int sceMpegCreate(sceMpeg *mp, u_char *work_area, int work_area_size) {
    if (mp == nullptr || work_area == nullptr || work_area_size <= 0) {
        return -1;
    }

    delete static_cast<MpegHost *>(mp->sys);

    mp->sys = new MpegHost;
    mp->width = 0;
    mp->height = 0;
    mp->frameCount = 0;
    mp->pts = 0;
    mp->dts = 0;
    mp->flags = 0;
    mp->pts2nd = 0;
    mp->dts2nd = 0;
    mp->flags2nd = 0;
    return 0;
}

int sceMpegDelete(sceMpeg *mp) {
    if (mp == nullptr) {
        return -1;
    }

    delete static_cast<MpegHost *>(mp->sys);
    mp->sys = nullptr;
    return 0;
}

int sceMpegReset(sceMpeg *mp) {
    if (mp == nullptr) {
        return -1;
    }

    mp->width = 0;
    mp->height = 0;
    mp->frameCount = 0;
    mp->pts = 0;
    mp->dts = 0;
    mp->flags = 0;
    mp->pts2nd = 0;
    mp->dts2nd = 0;
    mp->flags2nd = 0;
    return 0;
}

int sceMpegGetPicture(sceMpeg *mp, sceIpuRGB32 *rgb32, int mbcount) {
    (void) mp;
    (void) rgb32;
    (void) mbcount;
    return -1; // there is no IPU to decode into
}

int sceMpegIsEnd(sceMpeg *mp) {
    (void) mp;
    return 1; // nothing decodes, so every stream reads as ended
}

sceMpegCallback sceMpegAddCallback(sceMpeg *mp, sceMpegCbType type, sceMpegCallback callback,
                                   void *anyData) {
    MpegHost *host = HostOf(mp);

    if (host == nullptr || type < sceMpegCbError || type > sceMpegCbStr) {
        return nullptr;
    }

    sceMpegCallback previous = host->event[type];

    host->event[type] = callback;
    host->event_user[type] = anyData;
    return previous;
}

sceMpegCallback sceMpegAddStrCallback(sceMpeg *mp, sceMpegStrType strType, int ch,
                                      sceMpegCallback callback, void *anyData) {
    MpegHost *host = HostOf(mp);

    if (host == nullptr || strType < sceMpegStrM2V || strType > sceMpegStrDATA) {
        return nullptr;
    }

    MpegStreamSlot &slot = host->stream[strType];
    sceMpegCallback previous = slot.callback;

    slot.callback = callback;
    slot.user = anyData;
    slot.ch = ch;
    return previous;
}

int sceMpegDemuxPssRing(sceMpeg *mp, u_char *start, int size, u_char *buf_start, int buf_size) {
    (void) buf_start;
    (void) buf_size;

    if (start == nullptr || size <= 0) {
        return 0;
    }

    MpegHost     *host = HostOf(mp);
    const u_char *begin = start;
    const u_char *p = start;
    const u_char *end = start + size;
    const u_char *consumed = start;

    while (p + 4 <= end) {
        if (p[0] != 0 || p[1] != 0 || p[2] != 1) {
            p++;
            continue;
        }

        int code = p[3];

        if (code == 0xBA) { // pack header: a fixed size, no length field
            std::size_t pack = (p[4] & 0xC0) == 0x40 ? 14 : 12;

            if (pack == 14 && p + 14 <= end) {
                pack += p[13] & 0x07; // MPEG-2 pack stuffing
            }

            if (p + pack > end) {
                break;
            }

            p += pack;
            consumed = p;
            continue;
        }

        if (code == 0xB9) { // program end
            p += 4;
            consumed = p;
            break;
        }

        if (p + 6 > end) {
            break;
        }

        int length = (p[4] << 8) | p[5];

        if (p + 6 + length > end) {
            break; // the payload is split; leave the packet for the next call
        }

        const u_char *header = p + 6;
        std::size_t   payload = 0;
        long          pts = -1;
        long          dts = -1;
        int           kind = -1;

        if (code >= 0xE0 && code <= 0xEF) {
            kind = sceMpegStrM2V;
        } else if (code == 0xBD || (code >= 0xC0 && code <= 0xDF)) {
            kind = sceMpegStrPCM;
        }

        if (kind >= 0 && length >= 3 && (header[0] & 0xC0) == 0x80) { // MPEG-2 PES header
            std::size_t header_data = header[2];

            if (3 + header_data > (std::size_t) length) {
                break; // malformed; do not guess where the payload starts
            }

            unsigned flags = (header[1] >> 6) & 0x3;

            if (flags >= 2) {
                pts = (long) movie_port::PtsAt(header + 3);
            }

            if (flags == 3 && header_data >= 10) {
                dts = (long) movie_port::PtsAt(header + 8);
            }

            payload = 3 + header_data;
        }

        if (kind >= 0 && host != nullptr && host->stream[kind].callback != nullptr) {
            sceMpegCbDataStr data{};

            data.type = sceMpegCbStr;
            data.header = const_cast<u_char *>(header);
            data.data = const_cast<u_char *>(header + payload);
            data.len = (u_int) ((std::size_t) length - payload);
            data.pts = pts;
            data.dts = dts;

            host->stream[kind].callback(mp, (sceMpegCbData *) &data, host->stream[kind].user);
        }

        if (mp != nullptr) {
            if (pts >= 0) {
                mp->pts = pts;
            }

            if (dts >= 0) {
                mp->dts = dts;
            }

            if (kind == sceMpegStrM2V) {
                mp->frameCount++;
            }
        }

        p += 6 + length;
        consumed = p;
    }

    return (int) (consumed - begin);
}

} // extern "C"

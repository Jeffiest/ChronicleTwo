#pragma once

#include <cstddef>
#include <cstdint>

// The host movie player's seam. Retail's ps2/src/movie.cpp streams a PSS - an MPEG-2 program
// stream (00 00 01 BA packs, video PES 0xE0..0xEF, audio PES 0xBD carrying SShd chunks) - through
// the IPU and the SPU, shows decoded frames by vblank DMA and runs its read/decode threads
// against the interrupt handlers. port/src/movie.cpp replaces that unit with a playout the host
// can run.
//
// Decode decision: the playout does not decode video. A native decoder needs a library, and the
// build host carries FFmpeg's runtime libraries and CLI but no development headers (no
// libavcodec.pc, no avcodec.h), so nothing can be linked. The player instead reads the
// presentation time stamps out of the stream, runs for the stream's duration on the platform
// clock and ends where the stream does. The moviework texture keeps its contents, so the screen
// shows what the drawing code left there (black) where retail showed video; the captions the
// game draws over a movie (event captions in event_func.cpp, the title's overlays) are driven by
// the caller's own frame counter and CMovie::EndCheck, so a timing-correct playout keeps them
// in place.
//
// To add video, implement VideoSource over libavcodec (or any decoder) and install it with
// SetVideoSource; the player opens it at Load and closes it at Term. Decoding frames and
// uploading them into the game's movie texture is a second seam into the texture manager
// (port/src/mg_texture.cpp): the backend, the SwitchThread pump and that upload land together.

namespace movie_port {

/**
 * One decoded picture, in the movie texture's layout (retail's movie size is 512x416).
 */
struct Frame {
    const std::uint32_t *pixels = nullptr; /**< Pixels of the picture. */
    int                  width = 0;        /**< Width in pixels. */
    int                  height = 0;       /**< Height in pixels. */
};

/**
 * A video decoder backend. The player never owns it and never deletes it.
 */
class VideoSource {
public:
    virtual ~VideoSource() = default;

    /**
     * Opens the movie the game named ("RUSH.PSS" and the like), which resolves through the disc
     * index like any other game file. False when the stream cannot be opened or decoded.
     */
    virtual bool Open(const char *name) = 0;

    /**
     * Decodes the next picture in display order; false at the end of the stream.
     */
    virtual bool NextFrame(Frame &frame) = 0;

    virtual void Close() = 0;
};

/**
 * Installs the decoder backend; nullptr restores the timing-only player.
 */
void SetVideoSource(VideoSource *source);

VideoSource *GetVideoSource();

/**
 * Presentation timing and picture size read from a program stream's own headers.
 */
struct PssTiming {
    std::int64_t first_pts = -1; /**< 90 kHz stamp of the first video packet, or -1. */
    std::int64_t last_pts = -1;  /**< 90 kHz stamp of the last video packet seen. */
    int          width = 0;      /**< Picture width from the sequence header, or 0. */
    int          height = 0;     /**< Picture height from the sequence header, or 0. */
    double       frame_hz = 0.0; /**< Frame rate from the sequence header, or 0. */
};

/**
 * The 33-bit presentation time stamp at p, with its marker bits taken out.
 */
std::int64_t PtsAt(const std::uint8_t *p);

/**
 * Scans one contiguous span of a program stream for the sequence header (picture size and frame
 * rate) and for video presentation time stamps. first_pts is recorded only once, so callers feed
 * the file's head span first and a tail span afterwards; last_pts keeps moving. Compressed data
 * can hold look-alike start codes, so a video packet only counts when its PTS marker bits are
 * all set.
 */
void ScanPssSpan(const std::uint8_t *data, std::size_t size, PssTiming &timing);

} // namespace movie_port

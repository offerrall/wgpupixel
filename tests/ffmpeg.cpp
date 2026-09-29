#include "test.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <set>
#include <string>

int main() {
    test::run("image-only FFmpeg components", [] {
        std::set<std::string> decoders, encoders, demuxers, protocols;
        void* state = nullptr;
        while (const auto* codec = av_codec_iterate(&state)) {
            if (av_codec_is_decoder(codec)) decoders.insert(codec->name);
            if (av_codec_is_encoder(codec)) encoders.insert(codec->name);
        }
        // WebP uses FFmpeg's VP8 decoder internally; it is not an additional file format.
        test::check(decoders == std::set<std::string>{"exr", "mjpeg", "png", "tiff", "vp8", "webp"},
                    "unexpected decoder set: the full/system FFmpeg must never be linked");
        test::check(encoders == std::set<std::string>{"png"}, "unexpected encoders");
        state = nullptr;
        while (const auto* format = av_demuxer_iterate(&state)) demuxers.insert(format->name);
        test::check(demuxers == std::set<std::string>{"exr_pipe", "jpeg_pipe", "png_pipe", "tiff_pipe", "webp_pipe"},
                    "unexpected demuxers");
        state = nullptr;
        test::check(av_muxer_iterate(&state) == nullptr, "file IO writes PNG directly; no muxers needed");
        state = nullptr;
        while (const auto* protocol = avio_enum_protocols(&state, 0)) protocols.insert(protocol);
        test::check(protocols == std::set<std::string>{"file"}, "unexpected input protocols");
        protocols.clear();
        state = nullptr;
        while (const auto* protocol = avio_enum_protocols(&state, 1)) protocols.insert(protocol);
        test::check(protocols == std::set<std::string>{"file"}, "unexpected output protocols");
    });
    return test::finish();
}

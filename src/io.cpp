#include "wgpupixel_io.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/exif.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}
#include <lcms2.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <vector>

namespace wgpupixel::io {
namespace {

template <class T, auto Free> struct IndirectDelete {
    void operator()(T* value) const noexcept {
        Free(&value);
    }
};
template <class T, auto Free> using Owner = std::unique_ptr<T, IndirectDelete<T, Free>>;
using Input = Owner<AVFormatContext, avformat_close_input>;
using File = Owner<AVIOContext, avio_closep>;
using Codec = Owner<AVCodecContext, avcodec_free_context>;
using Frame = Owner<AVFrame, av_frame_free>;
using Packet = Owner<AVPacket, av_packet_free>;
using Dictionary = Owner<AVDictionary, av_dict_free>;
using Profile = std::unique_ptr<void, decltype(&cmsCloseProfile)>;

[[noreturn]] void fail(std::string_view operation, std::string_view message) {
    throw Error(ErrorCode::io_failed, operation, "path", message);
}

void check(int status, std::string_view operation) {
    if (status == AVERROR(ENOMEM)) {
        throw std::bad_alloc();
    }
    if (status < 0) {
        char message[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(status, message, sizeof(message));
        fail(operation, message);
    }
}

template <class T> T allocated(T value) {
    if (!value) {
        throw std::bad_alloc();
    }
    return value;
}

std::string filename(std::string_view path, std::string_view operation) {
    if (path.empty() || path.find('\0') != path.npos) {
        throw Error(ErrorCode::invalid_argument, operation, "path",
                    "path must be nonempty and contain no NUL");
    }
    return std::string(path);
}

template <class Resource> struct Temporary {
    Context& context;
    Resource value;
    bool retained = false;
    ~Temporary() {
        if (!retained) {
            try {
                context.destroy(value);
            } catch (
                const Error&) { /* The context still owns pending resources after device failure. */
            }
        }
    }
};

std::size_t byte_count(int width, int height, std::size_t pixel_size) {
    if (width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() / 16 ||
        std::uint64_t(width) * std::uint64_t(height) >
            std::numeric_limits<std::size_t>::max() / pixel_size) {
        throw Error(ErrorCode::capacity, "io.load", "image", "image exceeds host address capacity");
    }
    return std::size_t(width) * std::size_t(height) * pixel_size;
}

Frame decode(const std::string& path) {
    constexpr auto operation = "io.load";
    AVIOContext* opened = nullptr;
    const int status =
        avio_open2(&opened, ("file:" + path).c_str(), AVIO_FLAG_READ, nullptr, nullptr);
    File file(opened);
    check(status, operation);
    auto* raw = allocated(avformat_alloc_context());
    raw->pb = file.get();
    raw->flags |= AVFMT_FLAG_CUSTOM_IO;
    AVDictionary* options = nullptr;
    const int configured = av_dict_set(&options, "format_whitelist",
                                       "jpeg_pipe,png_pipe,webp_pipe,tiff_pipe,exr_pipe", 0);
    Dictionary settings(options);
    if (configured < 0) {
        avformat_free_context(raw);
        check(configured, operation);
    }
    const int result = avformat_open_input(&raw, nullptr, nullptr, &options);
    settings.release();
    settings.reset(options);
    Input input(raw);
    check(result, operation);
    const AVCodec* decoder = nullptr;
    const int stream = av_find_best_stream(input.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    check(stream, operation);
    Codec codec(allocated(avcodec_alloc_context3(decoder)));
    check(avcodec_parameters_to_context(codec.get(), input->streams[stream]->codecpar), operation);
    codec->thread_count = 0;
    codec->thread_type = FF_THREAD_SLICE;
    codec->err_recognition = AV_EF_CRCCHECK | AV_EF_BITSTREAM;
    // TIFF's strict mode rejects valid extension tags, including orientation.
    if (decoder->id != AV_CODEC_ID_TIFF) {
        codec->err_recognition |= AV_EF_EXPLODE;
    }
    check(avcodec_open2(codec.get(), decoder, nullptr), operation);
    Frame frame(allocated(av_frame_alloc()));
    Packet packet(allocated(av_packet_alloc()));
    for (;;) {
        const int received = avcodec_receive_frame(codec.get(), frame.get());
        if (received == 0) {
            break;
        }
        if (received != AVERROR(EAGAIN)) {
            check(received, operation);
        }
        int read = 0;
        do {
            av_packet_unref(packet.get());
            read = av_read_frame(input.get(), packet.get());
        } while (read >= 0 && packet->stream_index != stream);
        if (read != AVERROR_EOF) {
            check(read, operation);
        }
        check(avcodec_send_packet(codec.get(), read == AVERROR_EOF ? nullptr : packet.get()),
              operation);
    }
    if (frame->decode_error_flags || (frame->flags & AV_FRAME_FLAG_CORRUPT)) {
        fail(operation, "corrupt image frame");
    }
    check(av_frame_apply_cropping(frame.get(), 0), operation);
    return frame;
}

int coefficients(AVColorSpace space) {
    switch (space) {
    case AVCOL_SPC_BT709:
        return SWS_CS_ITU709;
    case AVCOL_SPC_BT2020_NCL:
        return SWS_CS_BT2020;
    case AVCOL_SPC_FCC:
        return SWS_CS_FCC;
    case AVCOL_SPC_SMPTE240M:
        return SWS_CS_SMPTE240M;
    default:
        return SWS_CS_ITU601;
    }
}

Frame unpack(const AVFrame& frame, AVPixelFormat format) {
    const auto source = static_cast<AVPixelFormat>(frame.format);
    if (!sws_isSupportedInput(source) || !sws_isSupportedOutput(format)) {
        fail("io.load", "unsupported decoded pixel layout");
    }
    Frame output(allocated(av_frame_alloc()));
    output->width = frame.width;
    output->height = frame.height;
    output->format = format;
    check(av_frame_get_buffer(output.get(), 32), "io.load");
    std::unique_ptr<SwsContext, decltype(&sws_freeContext)> scaler(
        allocated(sws_getContext(frame.width, frame.height, source, frame.width, frame.height,
                                 format, SWS_BILINEAR | SWS_ACCURATE_RND, nullptr, nullptr,
                                 nullptr)),
        sws_freeContext);
    const auto* descriptor = av_pix_fmt_desc_get(source);
    const int full = frame.color_range == AVCOL_RANGE_JPEG ||
                     (descriptor && ((descriptor->flags & AV_PIX_FMT_FLAG_RGB) ||
                                     descriptor->nb_components <= 2));
    const int* matrix = sws_getCoefficients(coefficients(frame.colorspace));
    check(sws_setColorspaceDetails(scaler.get(), matrix, full, matrix, 1, 0, 1 << 16, 1 << 16),
          "io.load");
    const int rows = sws_scale(scaler.get(), frame.data, frame.linesize, 0, frame.height,
                               output->data, output->linesize);
    check(rows, "io.load");
    if (rows != frame.height) {
        fail("io.load", "incomplete pixel conversion");
    }
    return output;
}

std::vector<std::uint8_t> normalize(const AVFrame& frame) {
    std::vector<std::uint8_t> pixels(byte_count(frame.width, frame.height, 4));
    if (const auto* icc = av_frame_get_side_data(&frame, AV_FRAME_DATA_ICC_PROFILE)) {
        if (icc->size > std::numeric_limits<cmsUInt32Number>::max()) {
            fail("io.load", "ICC profile is too large");
        }
        std::unique_ptr<std::remove_pointer_t<cmsContext>, decltype(&cmsDeleteContext)> cms(
            allocated(cmsCreateContext(nullptr, nullptr)), cmsDeleteContext);
        Profile input(
            cmsOpenProfileFromMemTHR(cms.get(), icc->data, static_cast<cmsUInt32Number>(icc->size)),
            cmsCloseProfile);
        if (!input) {
            fail("io.load", "invalid embedded ICC profile");
        }
        const auto space = cmsGetColorSpace(input.get());
        const bool gray = space == cmsSigGrayData;
        if (!gray && space != cmsSigRgbData) {
            fail("io.load", "only RGB and grayscale ICC profiles are supported");
        }
        if (frame.alpha_mode == AVALPHA_MODE_PREMULTIPLIED) {
            fail("io.load", "premultiplied ICC input is unsupported");
        }
        Profile output(allocated(cmsCreate_sRGBProfileTHR(cms.get())), cmsCloseProfile);
        const auto* descriptor = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame.format));
        const bool wide = !descriptor || descriptor->comp[0].depth > 8;
        const cmsUInt32Number layout =
            gray ? (wide ? TYPE_GRAYA_16 : TYPE_GRAYA_8) : (wide ? TYPE_RGBA_16 : TYPE_RGBA_8);
        std::unique_ptr<void, decltype(&cmsDeleteTransform)> transform(
            cmsCreateTransformTHR(cms.get(), input.get(), layout, output.get(), TYPE_RGBA_8,
                                  INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_COPY_ALPHA),
            cmsDeleteTransform);
        if (!transform) {
            fail("io.load", "cannot convert embedded ICC profile to sRGB");
        }
        auto samples = unpack(frame, gray ? (wide ? AV_PIX_FMT_YA16 : AV_PIX_FMT_YA8)
                                          : (wide ? AV_PIX_FMT_RGBA64 : AV_PIX_FMT_RGBA));
        for (int y = 0; y < frame.height; ++y) {
            cmsDoTransform(transform.get(),
                           samples->data[0] + std::ptrdiff_t(y) * samples->linesize[0],
                           pixels.data() + std::size_t(y) * std::size_t(frame.width) * 4,
                           static_cast<cmsUInt32Number>(frame.width));
        }
    } else if (frame.color_trc == AVCOL_TRC_LINEAR || frame.color_trc == AVCOL_TRC_GAMMA22 ||
               frame.color_trc == AVCOL_TRC_GAMMA28) {
        if (frame.color_primaries != AVCOL_PRI_UNSPECIFIED &&
            frame.color_primaries != AVCOL_PRI_BT709) {
            fail("io.load", "input without ICC requires sRGB/Rec.709 primaries");
        }
        auto samples = unpack(frame, AV_PIX_FMT_GBRAPF32);
        constexpr int planes[] = {2, 0, 1, 3};
        for (int y = 0; y < frame.height; ++y) {
            for (int x = 0; x < frame.width; ++x) {
                const auto sample = [&](int plane) {
                    float value;
                    std::memcpy(&value,
                                samples->data[plane] +
                                    std::ptrdiff_t(y) * samples->linesize[plane] +
                                    std::ptrdiff_t(x) * 4,
                                4);
                    if (!std::isfinite(value)) {
                        fail("io.load", "non-finite image sample");
                    }
                    return value;
                };
                const float alpha = sample(3);
                for (std::size_t c = 0; c < 4; ++c) {
                    float value = sample(planes[c]);
                    if (c < 3 && frame.alpha_mode == AVALPHA_MODE_PREMULTIPLIED) {
                        value = alpha > 0 ? value / alpha : 0;
                    }
                    value = std::clamp(value, 0.0f, 1.0f);
                    if (c < 3) {
                        if (frame.color_trc != AVCOL_TRC_LINEAR) {
                            value =
                                std::pow(value, frame.color_trc == AVCOL_TRC_GAMMA22 ? 2.2f : 2.8f);
                        }
                        value = value <= 0.0031308f ? value * 12.92f
                                                    : 1.055f * std::pow(value, 1 / 2.4f) - 0.055f;
                    }
                    pixels[(std::size_t(y) * std::size_t(frame.width) + std::size_t(x)) * 4 + c] =
                        static_cast<std::uint8_t>(std::lround(value * 255));
                }
            }
        }
    } else {
        if ((frame.color_primaries != AVCOL_PRI_UNSPECIFIED &&
             frame.color_primaries != AVCOL_PRI_BT709) ||
            (frame.color_trc != AVCOL_TRC_UNSPECIFIED &&
             frame.color_trc != AVCOL_TRC_IEC61966_2_1) ||
            frame.alpha_mode == AVALPHA_MODE_PREMULTIPLIED) {
            fail("io.load", "unsupported color encoding: supply an RGB or grayscale ICC profile");
        }
        auto samples = unpack(frame, AV_PIX_FMT_RGBA);
        for (int y = 0; y < frame.height; ++y) {
            std::memcpy(pixels.data() + std::size_t(y) * std::size_t(frame.width) * 4,
                        samples->data[0] + std::ptrdiff_t(y) * samples->linesize[0],
                        std::size_t(frame.width) * 4);
        }
    }
    return pixels;
}

void orient(const AVFrame& frame, std::vector<std::uint8_t>& pixels, int& width, int& height) {
    const auto* matrix = av_frame_get_side_data(&frame, AV_FRAME_DATA_DISPLAYMATRIX);
    if (!matrix) {
        return;
    }
    if (matrix->size < 9 * sizeof(std::int32_t)) {
        fail("io.load", "invalid orientation matrix");
    }
    const int orientation =
        av_exif_matrix_to_orientation(reinterpret_cast<const std::int32_t*>(matrix->data));
    if (orientation < 1 || orientation > 8) {
        fail("io.load", "invalid image orientation");
    }
    if (orientation == 1) {
        return;
    }
    const bool transpose = orientation >= 5;
    const int out_width = transpose ? height : width;
    const int out_height = transpose ? width : height;
    std::vector<std::uint8_t> output(pixels.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int dx = transpose ? y : x;
            int dy = transpose ? x : y;
            if (orientation == 2 || orientation == 3 || orientation == 6 || orientation == 7) {
                dx = out_width - 1 - dx;
            }
            if (orientation == 3 || orientation == 4 || orientation == 7 || orientation == 8) {
                dy = out_height - 1 - dy;
            }
            std::memcpy(
                output.data() + (std::size_t(dy) * std::size_t(out_width) + std::size_t(dx)) * 4,
                pixels.data() + (std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4, 4);
        }
    }
    pixels = std::move(output);
    width = out_width;
    height = out_height;
}

} // namespace

Image load(Context& context, std::string_view path) try {
    auto frame = decode(filename(path, "io.load"));
    auto pixels = normalize(*frame);
    int width = frame->width;
    int height = frame->height;
    orient(*frame, pixels, width, height);
    frame.reset();
    Temporary image{context, context.create_image({static_cast<std::uint32_t>(width),
                                                   static_cast<std::uint32_t>(height)})};
    Temporary upload{context, context.create_upload_buffer(image.value)};
    auto commands = context.create_commands(1);
    context.write(upload.value, pixels);
    commands.upload(upload.value, image.value);
    context.submit_and_wait(commands);
    image.retained = true;
    return image.value;
} catch (const std::bad_alloc&) {
    throw Error(ErrorCode::out_of_memory, "io.load", "", "could not allocate file input resources");
}

void save(Context& context, const Image& image, std::string_view path) try {
    constexpr auto operation = "io.save";
    const auto name = filename(path, operation);
    auto suffix = name.size() >= 4 ? name.substr(name.size() - 4) : std::string{};
    for (char& c : suffix) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c + ('a' - 'A'));
        }
    }
    if (suffix != ".png") {
        throw Error(ErrorCode::invalid_argument, operation, "path",
                    "save requires a .png filename");
    }
    Temporary readback{context, context.create_readback_buffer(image)};
    const auto* encoder = avcodec_find_encoder(AV_CODEC_ID_PNG);
    if (!encoder) {
        fail(operation, "FFmpeg was built without the PNG encoder");
    }
    Codec codec(allocated(avcodec_alloc_context3(encoder)));
    Frame frame(allocated(av_frame_alloc()));
    Packet packet(allocated(av_packet_alloc()));
    codec->width = static_cast<int>(image.size().width);
    codec->height = static_cast<int>(image.size().height);
    codec->pix_fmt = AV_PIX_FMT_RGBA;
    codec->time_base = {1, 1};
    codec->thread_count = 0;
    codec->thread_type = FF_THREAD_SLICE;
    codec->compression_level = 6;
    codec->color_primaries = AVCOL_PRI_BT709;
    codec->color_trc = AVCOL_TRC_IEC61966_2_1;
    codec->color_range = AVCOL_RANGE_JPEG;
    check(av_opt_set(codec->priv_data, "pred", "paeth", 0), operation);
    check(avcodec_open2(codec.get(), encoder, nullptr), operation);
    frame->width = codec->width;
    frame->height = codec->height;
    frame->format = codec->pix_fmt;
    frame->color_primaries = codec->color_primaries;
    frame->color_trc = codec->color_trc;
    frame->color_range = codec->color_range;
    frame->pts = 0;
    check(av_frame_get_buffer(frame.get(), 1), operation);
    const auto size = std::size_t(image.size().width) * image.size().height * 4;
    if (frame->linesize[0] != codec->width * 4) {
        fail(operation, "unexpected encoder row layout");
    }
    auto commands = context.create_commands(1);
    commands.download(image, readback.value);
    context.submit_and_wait(commands);
    context.read(readback.value, {frame->data[0], size});
    check(avcodec_send_frame(codec.get(), frame.get()), operation);
    check(avcodec_send_frame(codec.get(), nullptr), operation);
    std::ofstream output(std::filesystem::path(std::u8string(name.begin(), name.end())),
                         std::ios::binary | std::ios::trunc);
    if (!output) {
        fail(operation, "cannot open output file");
    }
    for (;;) {
        const int result = avcodec_receive_packet(codec.get(), packet.get());
        if (result == AVERROR_EOF) {
            break;
        }
        check(result, operation);
        output.write(reinterpret_cast<const char*>(packet->data), packet->size);
        if (!output) {
            fail(operation, "cannot write output file");
        }
        av_packet_unref(packet.get());
    }
    output.close();
    if (!output) {
        fail(operation, "cannot close output file");
    }
} catch (const std::bad_alloc&) {
    throw Error(ErrorCode::out_of_memory, "io.save", "",
                "could not allocate file output resources");
}

} // namespace wgpupixel::io

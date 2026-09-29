#include "wgpupixel_io.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/exif.h>
#include <libavformat/avformat.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}
#include <lcms2.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <thread>
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
    if (decoder->id == AV_CODEC_ID_TIFF) {
        // The TIFF decoder retains ExtraSamples in EXIF but does not set alpha_mode.
        if (const auto* exif = av_frame_get_side_data(frame.get(), AV_FRAME_DATA_EXIF)) {
            AVExifMetadata metadata{};
            std::unique_ptr<AVExifMetadata, decltype(&av_exif_free)> owner(&metadata, av_exif_free);
            AVExifEntry* alpha = nullptr;
            // Optional metadata must not prevent otherwise valid pixels from loading.
            if (av_exif_parse_buffer(nullptr, exif->data, exif->size, &metadata,
                                     AV_EXIF_TIFF_HEADER) >= 0 &&
                av_exif_get_entry(nullptr, &metadata, 338, 0, &alpha) >= 0 && alpha &&
                alpha->type == AV_TIFF_SHORT && alpha->count == 1) {
                frame->alpha_mode =
                    alpha->value.uint[0] == 1 ? AVALPHA_MODE_PREMULTIPLIED : AVALPHA_MODE_STRAIGHT;
            }
        }
        // FFmpeg assigns gamma 2.2 to all black-is-zero TIFFs without a transfer tag.
        // Use the same untagged sRGB default as RGB TIFF; an ICC profile takes precedence.
        if (frame->color_trc == AVCOL_TRC_GAMMA22) {
            frame->color_trc = AVCOL_TRC_UNSPECIFIED;
        }
        const auto* descriptor = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
        if (descriptor && (descriptor->flags & AV_PIX_FMT_FLAG_FLOAT) &&
            frame->color_trc == AVCOL_TRC_UNSPECIFIED) {
            frame->color_trc = AVCOL_TRC_LINEAR;
        }
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

Frame allocate_frame(const AVFrame& frame, AVPixelFormat format) {
    (void)byte_count(frame.width, frame.height, 16);
    Frame output(allocated(av_frame_alloc()));
    output->width = frame.width;
    output->height = frame.height;
    output->format = format;
    check(av_frame_get_buffer(output.get(), 32), "io.load");
    return output;
}

Frame unpack(const AVFrame& frame, AVPixelFormat format) {
    const auto source = static_cast<AVPixelFormat>(frame.format);
    if (!sws_isSupportedInput(source) || !sws_isSupportedOutput(format)) {
        fail("io.load", "unsupported decoded pixel layout");
    }
    auto output = allocate_frame(frame, format);
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

// Stable swscale in FFmpeg 9 cannot output RGBAF32, and float layout conversions
// can clip/quantize through integer intermediates. Pack decoded float components
// directly; widening binary16 to binary32 is exact, including subnormals.
Frame pack_float(const AVFrame& frame, bool gray = false) {
    const auto* descriptor = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame.format));
    const bool floating = descriptor->flags & AV_PIX_FMT_FLAG_FLOAT;
    const bool big = descriptor->flags & AV_PIX_FMT_FLAG_BE;
    const bool alpha = descriptor->flags & AV_PIX_FMT_FLAG_ALPHA;
    const int channels = gray ? 2 : 4;
    auto output = allocate_frame(frame, gray ? AV_PIX_FMT_YAF32 : AV_PIX_FMT_RGBAF32);
    for (int y = 0; y < frame.height; ++y) {
        auto* row =
            reinterpret_cast<float*>(output->data[0] + std::ptrdiff_t(y) * output->linesize[0]);
        for (int x = 0; x < frame.width; ++x) {
            for (int c = 0; c < channels; ++c) {
                const bool opacity = c == channels - 1;
                float value = 1;
                if (!opacity || alpha) {
                    const int component = opacity ? descriptor->nb_components - 1
                                          : descriptor->nb_components <= 2 ? 0
                                                                           : c;
                    const auto& layout = descriptor->comp[component];
                    const auto* sample = frame.data[layout.plane] +
                                         std::ptrdiff_t(y) * frame.linesize[layout.plane] +
                                         std::ptrdiff_t(x) * layout.step + layout.offset;
                    if (floating && layout.depth == 32) {
                        value = std::bit_cast<float>(big ? AV_RB32(sample) : AV_RL32(sample));
                    } else if (layout.depth == 16) {
                        const unsigned bits = big ? AV_RB16(sample) : AV_RL16(sample);
                        if (floating) {
                            const int exponent = (bits >> 10) & 31;
                            if (exponent == 31) {
                                fail("io.load", "non-finite image sample");
                            }
                            value = std::ldexp(float((bits & 1023) + (exponent ? 1024 : 0)),
                                               exponent ? exponent - 25 : -24);
                            if (bits & 32768) {
                                value = -value;
                            }
                        } else {
                            value = float(bits) / 65535;
                        }
                    } else if (!floating && layout.depth == 8) {
                        value = float(*sample) / 255;
                    } else {
                        fail("io.load", "unsupported decoded sample depth");
                    }
                }
                if (!std::isfinite(value)) {
                    fail("io.load", "non-finite image sample");
                }
                row[x * channels + c] = value;
            }
        }
    }
    return output;
}

struct Pixels {
    TransferFormat format;
    std::size_t pixel_size;
    Frame samples;
};

// A sampled TRC/CLUT has a bounded domain even with float formatters and
// NOOPTIMIZE. Admit only analytic matrix/shaper float input, including gray.
// A profile can carry both matrix tags and a preferred AToB/DToB pipeline.
// Validate support and return whether every TRC is exactly the identity.
bool float_profile_is_linear(cmsHPROFILE profile, bool gray) {
    constexpr auto message = "float ICC requires an XYZ matrix/shaper profile "
                             "with parametric TRCs and no LUTs";
    if (!cmsIsMatrixShaper(profile) || cmsGetPCS(profile) != cmsSigXYZData) {
        fail("io.load", message);
    }
    for (auto tag : {cmsSigAToB0Tag, cmsSigAToB1Tag, cmsSigAToB2Tag, cmsSigDToB0Tag, cmsSigDToB1Tag,
                     cmsSigDToB2Tag, cmsSigDToB3Tag}) {
        if (cmsIsTag(profile, tag)) {
            fail("io.load", message);
        }
    }
    bool linear = true;
    for (auto tag : {cmsSigRedTRCTag, cmsSigGreenTRCTag, cmsSigBlueTRCTag}) {
        const auto* curve =
            static_cast<const cmsToneCurve*>(cmsReadTag(profile, gray ? cmsSigGrayTRCTag : tag));
        if (!curve || cmsGetToneCurveParametricType(curve) <= 0) {
            fail("io.load", message);
        }
        const auto* segment = cmsGetToneCurveSegment(0, curve);
        // cmsIsToneCurveLinear uses a tolerance on the sampled approximation;
        // only an exact identity curve commutes with alpha multiplication.
        linear &= cmsGetToneCurveParametricType(curve) == 1 && segment && segment->Params[0] == 1;
        if (gray) {
            break;
        }
    }
    return linear;
}

void check_icc_sample(float value) {
    // Keep ICC arithmetic away from overflow and LittleCMS's extreme-value
    // sentinels. Check before and after conversion and premultiplication.
    if (!std::isfinite(value) || std::abs(value) > 1e18f) {
        fail("io.load", "ICC sample is non-finite or exceeds magnitude 1e18");
    }
}

Pixels normalize(const AVFrame& frame) {
    const auto* descriptor = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame.format));
    if (!descriptor) {
        fail("io.load", "unsupported decoded pixel layout");
    }
    const bool floating = descriptor->flags & AV_PIX_FMT_FLAG_FLOAT;
    const bool wide = descriptor->comp[0].depth > 8;
    const bool associated = frame.alpha_mode == AVALPHA_MODE_PREMULTIPLIED;
    Pixels pixels{floating ? TransferFormat::rgba32_float
                  : wide   ? TransferFormat::rgba16
                           : TransferFormat::rgba8,
                  floating ? 16u
                  : wide   ? 8u
                           : 4u,
                  {}};
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
        const bool linear_input = floating && float_profile_is_linear(input.get(), gray);
        const bool linear_output = floating || wide || associated;
        // LittleCMS has no integer PREMUL input formatter for its float pipeline
        // (including 2.16). Widen associated samples before using that formatter;
        // straight 16-bit samples go directly through TYPE_RGBA_16/TYPE_GRAYA_16.
        const bool float_input = floating || associated;
        // A linear matrix acts directly on associated RGB, even at zero alpha.
        // Avoid a lossy divide/multiply round trip through the straight formatter.
        const bool keep_associated = associated && linear_input;
        Profile output(allocated(cmsCreate_sRGBProfileTHR(cms.get())), cmsCloseProfile);
        if (linear_output) {
            std::unique_ptr<cmsToneCurve, decltype(&cmsFreeToneCurve)> linear(
                allocated(cmsBuildGamma(cms.get(), 1)), cmsFreeToneCurve);
            for (auto tag : {cmsSigRedTRCTag, cmsSigGreenTRCTag, cmsSigBlueTRCTag}) {
                if (!cmsWriteTag(output.get(), tag, linear.get())) {
                    fail("io.load", "cannot create linear sRGB profile");
                }
            }
        }
        const cmsUInt32Number layout = (gray ? (float_input ? TYPE_GRAYA_FLT
                                                : wide      ? TYPE_GRAYA_16
                                                            : TYPE_GRAYA_8)
                                             : (float_input ? TYPE_RGBA_FLT
                                                : wide      ? TYPE_RGBA_16
                                                            : TYPE_RGBA_8)) |
                                       (associated && !keep_associated ? PREMUL_SH(1) : 0);
        const auto output_layout = linear_output ? TYPE_RGBA_FLT : TYPE_RGBA_8;
        auto samples = float_input
                           ? pack_float(frame, gray)
                           : unpack(frame, gray ? (wide ? AV_PIX_FMT_YA16 : AV_PIX_FMT_YA8)
                                                : (wide ? AV_PIX_FMT_RGBA64 : AV_PIX_FMT_RGBA));
        if (floating) {
            for (int y = 0; y < frame.height; ++y) {
                const auto* row = reinterpret_cast<const float*>(
                    samples->data[0] + std::ptrdiff_t(y) * samples->linesize[0]);
                for (int x = 0; x < frame.width * (gray ? 2 : 4); ++x) {
                    check_icc_sample(row[x]);
                }
            }
        }
        const auto* source = samples.get();
        // RGB float transforms can run in place; keep only two full image buffers
        // (decoded + packed) live, instead of a third transformed frame.
        pixels.samples =
            float_input && !gray
                ? std::move(samples)
                : allocate_frame(frame, linear_output ? AV_PIX_FMT_RGBAF32 : AV_PIX_FMT_RGBA);
        if (linear_output) {
            pixels.format = TransferFormat::rgba32_float;
            pixels.pixel_size = 16;
        }
        // Each worker owns its transform/cache. Construct them serially because
        // reading profile tags may populate LittleCMS's profile cache.
        const unsigned workers =
            linear_output && std::uint64_t(frame.width) * frame.height >= 1'000'000
                ? std::min({4u, std::max(1u, std::thread::hardware_concurrency()),
                            static_cast<unsigned>(frame.height)})
                : 1u;
        using Transform = std::unique_ptr<void, decltype(&cmsDeleteTransform)>;
        std::vector<Transform> transforms;
        for (unsigned i = 0; i < workers; ++i) {
            transforms.emplace_back(
                cmsCreateTransformTHR(cms.get(), input.get(), layout, output.get(), output_layout,
                                      INTENT_RELATIVE_COLORIMETRIC,
                                      cmsFLAGS_COPY_ALPHA |
                                          (linear_output ? cmsFLAGS_NOOPTIMIZE : 0)),
                cmsDeleteTransform);
            if (!transforms.back()) {
                fail("io.load", "cannot convert embedded ICC profile to sRGB");
            }
        }
        auto convert = [&](unsigned worker) {
            const int first = int(std::uint64_t(frame.height) * worker / workers);
            const int last = int(std::uint64_t(frame.height) * (worker + 1) / workers);
            for (int y = first; y < last; ++y) {
                cmsDoTransform(transforms[worker].get(),
                               source->data[0] + std::ptrdiff_t(y) * source->linesize[0],
                               pixels.samples->data[0] +
                                   std::ptrdiff_t(y) * pixels.samples->linesize[0],
                               static_cast<cmsUInt32Number>(frame.width));
            }
        };
        {
            std::vector<std::jthread> threads;
            for (unsigned i = 1; i < workers; ++i) {
                threads.emplace_back(convert, i);
            }
            convert(0);
        }
        if (linear_output) {
            for (int y = 0; y < frame.height; ++y) {
                auto* row = reinterpret_cast<float*>(
                    pixels.samples->data[0] + std::ptrdiff_t(y) * pixels.samples->linesize[0]);
                for (int x = 0; x < frame.width; ++x) {
                    check_icc_sample(row[4 * x + 3]);
                    for (int c = 0; c < 3; ++c) {
                        auto& value = row[4 * x + c];
                        check_icc_sample(value);
                        if (!keep_associated) {
                            value *= row[4 * x + 3];
                            check_icc_sample(value);
                        }
                    }
                }
            }
        }
    } else {
        const auto trc = frame.color_trc;
        if ((frame.color_primaries != AVCOL_PRI_UNSPECIFIED &&
             frame.color_primaries != AVCOL_PRI_BT709) ||
            (trc != AVCOL_TRC_UNSPECIFIED && trc != AVCOL_TRC_IEC61966_2_1 &&
             trc != AVCOL_TRC_LINEAR && trc != AVCOL_TRC_GAMMA22 && trc != AVCOL_TRC_GAMMA28)) {
            fail("io.load", "unsupported color encoding: supply an RGB or grayscale ICC profile");
        }
        if (floating || associated || trc == AVCOL_TRC_LINEAR || trc == AVCOL_TRC_GAMMA22 ||
            trc == AVCOL_TRC_GAMMA28) {
            pixels = {TransferFormat::rgba32_float, 16,
                      floating || (descriptor->flags & AV_PIX_FMT_FLAG_RGB) ||
                              descriptor->nb_components <= 2
                          ? pack_float(frame)
                          : pack_float(*unpack(frame, AV_PIX_FMT_RGBA64))};
            for (int y = 0; y < frame.height; ++y) {
                auto* row = reinterpret_cast<float*>(
                    pixels.samples->data[0] + std::ptrdiff_t(y) * pixels.samples->linesize[0]);
                for (int x = 0; x < frame.width; ++x) {
                    const float alpha = row[4 * x + 3];
                    for (int c = 0; c < 3; ++c) {
                        float& value = row[4 * x + c];
                        if (trc != AVCOL_TRC_LINEAR) {
                            if (associated) {
                                value = alpha > 0 ? value / alpha : 0;
                            }
                            // Untagged/tagged non-ICC curves mirror negative RGB;
                            // ICC parametric curves use LittleCMS's extension (sRGB
                            // extends its linear toe, pure gamma clamps negatives).
                            // Current decoders emit nonlinear samples only as integers.
                            if (trc == AVCOL_TRC_GAMMA22 || trc == AVCOL_TRC_GAMMA28) {
                                value =
                                    std::copysign(std::pow(std::abs(value),
                                                           trc == AVCOL_TRC_GAMMA22 ? 2.2f : 2.8f),
                                                  value);
                            } else {
                                const float magnitude = std::abs(value);
                                value = std::copysign(
                                    magnitude <= 0.04045f
                                        ? magnitude / 12.92f
                                        : std::pow((magnitude + 0.055f) / 1.055f, 2.4f),
                                    value);
                            }
                            value *= alpha;
                        } else if (!associated) {
                            value *= alpha;
                        }
                    }
                }
            }
        } else {
            pixels.samples = unpack(frame, wide ? AV_PIX_FMT_RGBA64LE : AV_PIX_FMT_RGBA);
        }
    }
    return pixels;
}

int orientation(const AVFrame& frame) {
    const auto* matrix = av_frame_get_side_data(&frame, AV_FRAME_DATA_DISPLAYMATRIX);
    if (!matrix) {
        return 1;
    }
    if (matrix->size < 9 * sizeof(std::int32_t)) {
        fail("io.load", "invalid orientation matrix");
    }
    const int orientation =
        av_exif_matrix_to_orientation(reinterpret_cast<const std::int32_t*>(matrix->data));
    if (orientation < 1 || orientation > 8) {
        fail("io.load", "invalid image orientation");
    }
    return orientation;
}

std::vector<std::uint8_t> orient(const AVFrame& samples, std::size_t pixel_size, int orientation) {
    const int width = samples.width;
    const int height = samples.height;
    const bool transpose = orientation >= 5;
    const int out_width = transpose ? height : width;
    const int out_height = transpose ? width : height;
    std::vector<std::uint8_t> output(byte_count(width, height, pixel_size));
    for (int y = 0; y < height; ++y) {
        const auto* row = samples.data[0] + std::ptrdiff_t(y) * samples.linesize[0];
        if (orientation == 1) {
            std::memcpy(output.data() + std::size_t(y) * width * pixel_size, row,
                        std::size_t(width) * pixel_size);
            continue;
        }
        for (int x = 0; x < width; ++x) {
            int dx = transpose ? y : x;
            int dy = transpose ? x : y;
            if (orientation == 2 || orientation == 3 || orientation == 6 || orientation == 7) {
                dx = out_width - 1 - dx;
            }
            if (orientation == 3 || orientation == 4 || orientation == 7 || orientation == 8) {
                dy = out_height - 1 - dy;
            }
            std::memcpy(output.data() +
                            (std::size_t(dy) * std::size_t(out_width) + std::size_t(dx)) *
                                pixel_size,
                        row + std::size_t(x) * pixel_size, pixel_size);
        }
    }
    return output;
}

} // namespace

Image load(Context& context, std::string_view path) try {
    auto frame = decode(filename(path, "io.load"));
    const int direction = orientation(*frame);
    const int width = direction >= 5 ? frame->height : frame->width;
    const int height = direction >= 5 ? frame->width : frame->height;
    auto normalized = normalize(*frame);
    frame.reset();
    auto pixels = orient(*normalized.samples, normalized.pixel_size, direction);
    normalized.samples.reset();
    Temporary image{context, context.create_image({static_cast<std::uint32_t>(width),
                                                   static_cast<std::uint32_t>(height)})};
    Temporary upload{context,
                     context.create_upload_buffer(image.value, {.format = normalized.format})};
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

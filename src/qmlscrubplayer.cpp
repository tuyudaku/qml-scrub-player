#include "qmlscrubplayer.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QIODevice>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QVector>
#include <QVideoFrame>
#include <QVideoSink>
#include <QWaitCondition>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/buffer.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace {

template <typename T, auto Deleter>
struct AvDeleter
{
    void operator()(T *value) const
    {
        if (value) {
            Deleter(&value);
        }
    }
};

using FormatContextPtr = std::unique_ptr<AVFormatContext, AvDeleter<AVFormatContext, avformat_close_input>>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, AvDeleter<AVCodecContext, avcodec_free_context>>;
using FramePtr = std::unique_ptr<AVFrame, AvDeleter<AVFrame, av_frame_free>>;
using PacketPtr = std::unique_ptr<AVPacket, AvDeleter<AVPacket, av_packet_free>>;
using SwsContextPtr = std::unique_ptr<SwsContext, decltype(&sws_freeContext)>;

struct SwrDeleter
{
    void operator()(SwrContext *value) const
    {
        swr_free(&value);
    }
};

using SwrContextPtr = std::unique_ptr<SwrContext, SwrDeleter>;

constexpr int defaultSeekPreviewMaximumDimension = 4096;

struct HardwareDecoder
{
    AVBufferRef *deviceContext = nullptr;
    AVPixelFormat pixelFormat = AV_PIX_FMT_NONE;

    ~HardwareDecoder()
    {
        av_buffer_unref(&deviceContext);
    }

    HardwareDecoder() = default;
    HardwareDecoder(const HardwareDecoder &) = delete;
    HardwareDecoder &operator=(const HardwareDecoder &) = delete;
};

QString ffmpegError(int errorCode)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(errorCode, buffer, sizeof(buffer));
    return QString::fromLocal8Bit(buffer);
}

QString urlToInput(const QUrl &url)
{
    if (url.isLocalFile()) {
        return QFileInfo(url.toLocalFile()).absoluteFilePath();
    }

    return url.toString();
}

qint64 timestampToMs(qint64 timestamp, AVRational timeBase)
{
    if (timestamp == AV_NOPTS_VALUE) {
        return 0;
    }

    return av_rescale_q(timestamp, timeBase, AVRational{1, 1000});
}

qint64 streamDurationToMs(const AVStream *stream)
{
    if (!stream) {
        return 0;
    }

    if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0) {
        return timestampToMs(stream->duration, stream->time_base);
    }

    if (stream->nb_frames > 0 && stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
        return av_rescale_q(stream->nb_frames, av_inv_q(stream->avg_frame_rate), AVRational{1, 1000});
    }

    return 0;
}

qint64 formatDurationToMs(const AVFormatContext *format)
{
    if (!format) {
        return 0;
    }

    qint64 durationMs = format->duration != AV_NOPTS_VALUE
        ? av_rescale_q(format->duration, AVRational{1, AV_TIME_BASE}, AVRational{1, 1000})
        : 0;

    for (unsigned int index = 0; index < format->nb_streams; ++index) {
        durationMs = std::max(durationMs, streamDurationToMs(format->streams[index]));
    }

    return durationMs;
}

QVector<AVHWDeviceType> preferredHardwareDeviceTypes()
{
    QVector<AVHWDeviceType> types;
#if defined(Q_OS_MACOS)
    types << AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
#elif defined(Q_OS_WIN)
    types << AV_HWDEVICE_TYPE_D3D11VA << AV_HWDEVICE_TYPE_DXVA2;
#endif
    return types;
}

AVPixelFormat hardwarePixelFormatForDevice(const AVCodec *codec, AVHWDeviceType deviceType)
{
    for (int index = 0;; ++index) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, index);
        if (!config) {
            break;
        }

        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && config->device_type == deviceType) {
            return config->pix_fmt;
        }
    }

    return AV_PIX_FMT_NONE;
}

AVPixelFormat getHardwareFormat(AVCodecContext *context, const AVPixelFormat *formats)
{
    const auto *hardwareDecoder = static_cast<const HardwareDecoder *>(context->opaque);
    if (!hardwareDecoder) {
        return formats[0];
    }

    for (const AVPixelFormat *format = formats; *format != AV_PIX_FMT_NONE; ++format) {
        if (*format == hardwareDecoder->pixelFormat) {
            return *format;
        }
    }

    return formats[0];
}

void configureHardwareDecoder(const AVCodec *codec, AVCodecContext *codecContext, HardwareDecoder &hardwareDecoder)
{
    for (AVHWDeviceType deviceType : preferredHardwareDeviceTypes()) {
        const AVPixelFormat pixelFormat = hardwarePixelFormatForDevice(codec, deviceType);
        if (pixelFormat == AV_PIX_FMT_NONE) {
            continue;
        }

        AVBufferRef *deviceContext = nullptr;
        if (av_hwdevice_ctx_create(&deviceContext, deviceType, nullptr, nullptr, 0) < 0) {
            continue;
        }

        hardwareDecoder.deviceContext = deviceContext;
        hardwareDecoder.pixelFormat = pixelFormat;
        codecContext->hw_device_ctx = av_buffer_ref(hardwareDecoder.deviceContext);
        codecContext->get_format = getHardwareFormat;
        codecContext->opaque = &hardwareDecoder;
        return;
    }
}

AVFrame *softwareFrameForScaling(AVFrame *frame, HardwareDecoder &hardwareDecoder, FramePtr &transferFrame)
{
    if (hardwareDecoder.pixelFormat == AV_PIX_FMT_NONE || frame->format != hardwareDecoder.pixelFormat) {
        return frame;
    }

    if (!transferFrame) {
        transferFrame.reset(av_frame_alloc());
    }
    if (!transferFrame) {
        return nullptr;
    }

    av_frame_unref(transferFrame.get());
    if (av_hwframe_transfer_data(transferFrame.get(), frame, 0) < 0) {
        return nullptr;
    }

    transferFrame->pts = frame->pts;
    transferFrame->best_effort_timestamp = frame->best_effort_timestamp;
    transferFrame->time_base = frame->time_base;
    return transferFrame.get();
}

} // namespace

class QmlScrubDecoder : public QThread
{
    Q_OBJECT

public:
    enum class SeekMode {
        Exact,
        Preview
    };

    explicit QmlScrubDecoder(QObject *parent = nullptr)
        : QThread(parent)
    {
    }

    ~QmlScrubDecoder() override
    {
        requestStop();
        wait();
    }

    void setSource(const QUrl &source)
    {
        QMutexLocker locker(&m_mutex);
        m_source = source;
    }

    void setPlaying(bool playing)
    {
        {
            QMutexLocker locker(&m_mutex);
            m_playing = playing;
            m_waitCondition.wakeAll();
        }

        if (!isRunning()) {
            start();
        }
    }

    void requestSeek(qint64 position, SeekMode mode, qint64 generation)
    {
        QMutexLocker locker(&m_mutex);
        m_seekPosition = std::max<qint64>(0, position);
        m_seekMode = mode;
        m_seekPending = true;
        m_seekGeneration = generation;
        m_waitCondition.wakeAll();
    }

    void requestStop()
    {
        QMutexLocker locker(&m_mutex);
        m_stopRequested = true;
        m_waitCondition.wakeAll();
    }

    void setLoops(int loops)
    {
        QMutexLocker locker(&m_mutex);
        m_loops = loops;
    }

    void setPlaybackRate(qreal playbackRate)
    {
        QMutexLocker locker(&m_mutex);
        m_playbackRate = std::clamp(playbackRate, 0.05, 8.0);
    }

    void setVolume(float volume)
    {
        QMutexLocker locker(&m_mutex);
        m_volume = std::clamp(volume, 0.0f, 1.0f);
    }

    void setMuted(bool muted)
    {
        QMutexLocker locker(&m_mutex);
        m_muted = muted;
    }

    void setAudioSuppressed(bool suppressed)
    {
        QMutexLocker locker(&m_mutex);
        m_audioSuppressed = suppressed;
        m_waitCondition.wakeAll();
    }

    void setSeekPreviewMaximumDimension(int seekPreviewMaximumDimension)
    {
        QMutexLocker locker(&m_mutex);
        m_seekPreviewMaximumDimension = std::max(64, seekPreviewMaximumDimension);
    }

signals:
    void durationReady(qint64 duration);
    void frameReady(const QImage &image, qint64 position, qint64 generation);
    void statusReady(QmlScrubPlayer::Status status);
    void errorReady(const QString &message);
    void playbackEnded();

protected:
    void run() override
    {
        const QUrl source = currentSource();
        if (source.isEmpty()) {
            emit statusReady(QmlScrubPlayer::Status::NoMedia);
            return;
        }

        emit statusReady(QmlScrubPlayer::Status::Loading);

        AVFormatContext *rawFormat = nullptr;
        const QByteArray input = urlToInput(source).toUtf8();
        if (const int result = avformat_open_input(&rawFormat, input.constData(), nullptr, nullptr); result < 0) {
            emitError(QStringLiteral("Could not open input: %1").arg(ffmpegError(result)));
            return;
        }
        FormatContextPtr format(rawFormat);

        if (const int result = avformat_find_stream_info(format.get(), nullptr); result < 0) {
            emitError(QStringLiteral("Could not read stream info: %1").arg(ffmpegError(result)));
            return;
        }

        const int videoStream = av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (videoStream < 0) {
            emitError(QStringLiteral("No video stream found"));
            return;
        }

        AVStream *stream = format->streams[videoStream];
        const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) {
            emitError(QStringLiteral("No decoder found for video stream"));
            return;
        }

        CodecContextPtr codecContext(avcodec_alloc_context3(codec));
        if (!codecContext) {
            emitError(QStringLiteral("Could not allocate decoder context"));
            return;
        }

        if (const int result = avcodec_parameters_to_context(codecContext.get(), stream->codecpar); result < 0) {
            emitError(QStringLiteral("Could not copy codec parameters: %1").arg(ffmpegError(result)));
            return;
        }

        codecContext->thread_count = 0;
        codecContext->thread_type = FF_THREAD_FRAME;
        HardwareDecoder hardwareDecoder;
        configureHardwareDecoder(codec, codecContext.get(), hardwareDecoder);

        if (const int result = avcodec_open2(codecContext.get(), codec, nullptr); result < 0) {
            emitError(QStringLiteral("Could not open decoder: %1").arg(ffmpegError(result)));
            return;
        }

        const int audioStream = av_find_best_stream(format.get(), AVMEDIA_TYPE_AUDIO, -1, videoStream, nullptr, 0);
        CodecContextPtr audioCodecContext;
        SwrContextPtr resampler;
        std::unique_ptr<QAudioSink> audioSink;
        QIODevice *audioDevice = nullptr;
        FramePtr audioFrame(av_frame_alloc());
        QByteArray audioBuffer;

        if (audioStream >= 0) {
            AVStream *audioAvStream = format->streams[audioStream];
            const AVCodec *audioCodec = avcodec_find_decoder(audioAvStream->codecpar->codec_id);
            if (audioCodec) {
                audioCodecContext.reset(avcodec_alloc_context3(audioCodec));
            }

            if (audioCodecContext
                && avcodec_parameters_to_context(audioCodecContext.get(), audioAvStream->codecpar) >= 0
                && avcodec_open2(audioCodecContext.get(), audioCodec, nullptr) >= 0) {
                if (!initializeAudioOutput(audioCodecContext.get(), resampler, audioSink, audioDevice)) {
                    audioCodecContext.reset();
                    resampler.reset();
                    audioFrame.reset();
                }
            } else {
                audioCodecContext.reset();
                audioFrame.reset();
            }
        } else {
            audioFrame.reset();
        }

        emit durationReady(formatDurationToMs(format.get()));
        emit statusReady(QmlScrubPlayer::Status::Loaded);

        PacketPtr packet(av_packet_alloc());
        FramePtr frame(av_frame_alloc());
        FramePtr transferFrame(av_frame_alloc());
        if (!packet || !frame) {
            emitError(QStringLiteral("Could not allocate FFmpeg frame buffers"));
            return;
        }

        SwsContextPtr sws(nullptr, &sws_freeContext);
        QElapsedTimer clock;
        qint64 clockStartPosition = 0;
        bool clockValid = false;
        qint64 dropFramesBefore = -1;
        bool previewFirstFrameAfterSeek = false;
        qint64 frameGeneration = 0;
        int completedLoops = 0;

        while (!stopRequested()) {
            waitForWork();
            if (stopRequested()) {
                break;
            }

            qint64 consumedSeekPosition = 0;
            qint64 consumedSeekGeneration = frameGeneration;
            SeekMode consumedSeekMode = SeekMode::Exact;
            if (consumeSeek(format.get(), codecContext.get(), audioCodecContext.get(), audioSink.get(), audioDevice, stream, clock, clockStartPosition, clockValid,
                    consumedSeekPosition, consumedSeekGeneration, consumedSeekMode)) {
                dropFramesBefore = consumedSeekPosition;
                previewFirstFrameAfterSeek = consumedSeekMode == SeekMode::Preview;
                frameGeneration = consumedSeekGeneration;
                emit statusReady(QmlScrubPlayer::Status::Buffered);
            }

            const int readResult = av_read_frame(format.get(), packet.get());
            if (readResult == AVERROR_EOF) {
                if (shouldLoop(++completedLoops)) {
                    seekTo(format.get(), codecContext.get(), stream, 0, SeekMode::Exact);
                    if (audioCodecContext) {
                        avcodec_flush_buffers(audioCodecContext.get());
                    }
                    if (audioSink) {
                        audioSink->reset();
                        audioDevice = audioSink->start();
                    }
                    clockValid = false;
                    continue;
                }
                emit statusReady(QmlScrubPlayer::Status::EndOfMedia);
                emit playbackEnded();
                break;
            }
            if (readResult < 0) {
                emitError(QStringLiteral("Could not read frame: %1").arg(ffmpegError(readResult)));
                break;
            }

            if (packet->stream_index == audioStream && audioCodecContext && audioFrame && audioSink && audioDevice) {
                if (!isPlaybackActive() || isAudioSuppressed()) {
                    av_packet_unref(packet.get());
                    continue;
                }
                decodeAudioPacket(audioCodecContext.get(), resampler.get(), audioFrame.get(), packet.get(),
                    audioSink.get(), audioDevice, audioBuffer);
                av_packet_unref(packet.get());
                continue;
            }

            if (packet->stream_index != videoStream) {
                av_packet_unref(packet.get());
                continue;
            }

            const int sendResult = avcodec_send_packet(codecContext.get(), packet.get());
            av_packet_unref(packet.get());
            if (sendResult < 0 && sendResult != AVERROR(EAGAIN)) {
                emitError(QStringLiteral("Could not send packet to decoder: %1").arg(ffmpegError(sendResult)));
                break;
            }

            while (!stopRequested()) {
                const int receiveResult = avcodec_receive_frame(codecContext.get(), frame.get());
                if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
                    break;
                }
                if (receiveResult < 0) {
                    emitError(QStringLiteral("Could not decode frame: %1").arg(ffmpegError(receiveResult)));
                    return;
                }

                AVFrame *displayFrame = softwareFrameForScaling(frame.get(), hardwareDecoder, transferFrame);
                if (!displayFrame) {
                    emitError(QStringLiteral("Could not transfer hardware frame"));
                    return;
                }

                const qint64 bestTimestamp = displayFrame->best_effort_timestamp;
                const qint64 framePosition = timestampToMs(bestTimestamp, stream->time_base);
                const bool previewFrame = previewFirstFrameAfterSeek;
                if (dropFramesBefore >= 0 && framePosition + 30 < dropFramesBefore) {
                    if (previewFirstFrameAfterSeek) {
                        previewFirstFrameAfterSeek = false;
                        if (!emitFrame(sws, displayFrame, framePosition, frameGeneration, false)) {
                            return;
                        }
                        av_frame_unref(frame.get());
                        if (hasPendingSeek()) {
                            break;
                        }
                        continue;
                    }

                    av_frame_unref(frame.get());
                    if (hasPendingSeek()) {
                        break;
                    }
                    continue;
                }
                dropFramesBefore = -1;
                previewFirstFrameAfterSeek = false;

                if (!previewFrame) {
                    throttleFrame(framePosition, clock, clockStartPosition, clockValid);
                }
                if (!emitFrame(sws, displayFrame, framePosition, frameGeneration, !previewFrame)) {
                    return;
                }
                av_frame_unref(frame.get());

                if (previewFrame || hasPendingSeek()) {
                    break;
                }
            }
        }
    }

private:
    QUrl currentSource()
    {
        QMutexLocker locker(&m_mutex);
        return m_source;
    }

    bool stopRequested()
    {
        QMutexLocker locker(&m_mutex);
        return m_stopRequested;
    }

    void waitForWork()
    {
        QMutexLocker locker(&m_mutex);
        while (!m_stopRequested && !m_playing && !m_seekPending) {
            m_waitCondition.wait(&m_mutex);
        }
    }

    bool isPlaybackActive()
    {
        QMutexLocker locker(&m_mutex);
        return m_playing;
    }

    bool isAudioSuppressed()
    {
        QMutexLocker locker(&m_mutex);
        return m_audioSuppressed;
    }

    bool hasPendingSeek()
    {
        QMutexLocker locker(&m_mutex);
        return m_seekPending;
    }

    bool consumeSeek(AVFormatContext *format, AVCodecContext *codecContext, AVCodecContext *audioCodecContext,
        QAudioSink *audioSink, QIODevice *&audioDevice, AVStream *stream,
        QElapsedTimer &clock, qint64 &clockStartPosition, bool &clockValid, qint64 &consumedSeekPosition,
        qint64 &consumedSeekGeneration, SeekMode &consumedSeekMode)
    {
        qint64 seekPosition = 0;
        {
            QMutexLocker locker(&m_mutex);
            if (!m_seekPending) {
                return false;
            }
            seekPosition = m_seekPosition;
            consumedSeekMode = m_seekMode;
            consumedSeekGeneration = m_seekGeneration;
            m_seekPending = false;
        }

        if (!seekTo(format, codecContext, stream, seekPosition, consumedSeekMode)) {
            return false;
        }
        if (audioCodecContext) {
            avcodec_flush_buffers(audioCodecContext);
        }
        if (audioSink) {
            audioSink->reset();
            audioDevice = audioSink->start();
            if (consumedSeekMode == SeekMode::Preview) {
                QMutexLocker locker(&m_mutex);
                m_audioSuppressed = true;
            }
        }

        clock.restart();
        clockStartPosition = seekPosition;
        clockValid = true;
        consumedSeekPosition = seekPosition;
        return true;
    }

    bool seekTo(AVFormatContext *format, AVCodecContext *codecContext, AVStream *stream, qint64 positionMs, SeekMode mode)
    {
        const qint64 timestamp = av_rescale_q(positionMs, AVRational{1, 1000}, stream->time_base);
        const int flags = positionMs <= 0 ? AVSEEK_FLAG_BACKWARD : AVSEEK_FLAG_BACKWARD;
        if (const int result = av_seek_frame(format, stream->index, timestamp, flags); result < 0) {
            emit errorReady(QStringLiteral("Could not seek: %1").arg(ffmpegError(result)));
            return false;
        }

        avcodec_flush_buffers(codecContext);
        codecContext->skip_frame = mode == SeekMode::Preview ? AVDISCARD_NONKEY : AVDISCARD_DEFAULT;
        return true;
    }

    bool initializeAudioOutput(AVCodecContext *audioCodecContext, SwrContextPtr &resampler,
        std::unique_ptr<QAudioSink> &audioSink, QIODevice *&audioDevice)
    {
        const int sampleRate = audioCodecContext->sample_rate > 0 ? audioCodecContext->sample_rate : 48000;
        const int channelCount = std::clamp(audioCodecContext->ch_layout.nb_channels, 1, 8);

        AVChannelLayout outputLayout;
        av_channel_layout_default(&outputLayout, channelCount);

        SwrContext *rawResampler = nullptr;
        const int setupResult = swr_alloc_set_opts2(
            &rawResampler,
            &outputLayout,
            AV_SAMPLE_FMT_S16,
            sampleRate,
            &audioCodecContext->ch_layout,
            audioCodecContext->sample_fmt,
            audioCodecContext->sample_rate,
            0,
            nullptr);
        av_channel_layout_uninit(&outputLayout);

        if (setupResult < 0 || !rawResampler) {
            emit errorReady(QStringLiteral("Could not allocate audio resampler"));
            return false;
        }

        resampler.reset(rawResampler);
        if (const int result = swr_init(resampler.get()); result < 0) {
            emit errorReady(QStringLiteral("Could not initialize audio resampler: %1").arg(ffmpegError(result)));
            return false;
        }

        QAudioFormat format;
        format.setSampleRate(sampleRate);
        format.setChannelCount(channelCount);
        format.setSampleFormat(QAudioFormat::Int16);

        if (!format.isValid()) {
            emit errorReady(QStringLiteral("Invalid audio format"));
            return false;
        }

        audioSink = std::make_unique<QAudioSink>(format);
        audioSink->setBufferSize(format.bytesForDuration(250000));
        audioSink->setVolume(effectiveVolume());
        audioDevice = audioSink->start();
        return audioDevice != nullptr;
    }

    void decodeAudioPacket(AVCodecContext *audioCodecContext, SwrContext *resampler, AVFrame *audioFrame,
        AVPacket *packet, QAudioSink *audioSink, QIODevice *audioDevice, QByteArray &audioBuffer)
    {
        const int sendResult = avcodec_send_packet(audioCodecContext, packet);
        if (sendResult < 0 && sendResult != AVERROR(EAGAIN)) {
            emit errorReady(QStringLiteral("Could not send audio packet: %1").arg(ffmpegError(sendResult)));
            return;
        }

        while (!stopRequested()) {
            const int receiveResult = avcodec_receive_frame(audioCodecContext, audioFrame);
            if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
                break;
            }
            if (receiveResult < 0) {
                emit errorReady(QStringLiteral("Could not decode audio frame: %1").arg(ffmpegError(receiveResult)));
                break;
            }

            const int channelCount = std::clamp(audioCodecContext->ch_layout.nb_channels, 1, 8);
            const int outputSamples = static_cast<int>(av_rescale_rnd(
                swr_get_delay(resampler, audioCodecContext->sample_rate) + audioFrame->nb_samples,
                audioCodecContext->sample_rate,
                audioCodecContext->sample_rate,
                AV_ROUND_UP));
            const int bufferSize = av_samples_get_buffer_size(nullptr, channelCount, outputSamples, AV_SAMPLE_FMT_S16, 1);
            if (bufferSize <= 0) {
                av_frame_unref(audioFrame);
                continue;
            }

            audioBuffer.resize(bufferSize);
            uint8_t *output[] = {reinterpret_cast<uint8_t *>(audioBuffer.data())};
            const int convertedSamples = swr_convert(
                resampler,
                output,
                outputSamples,
                const_cast<const uint8_t **>(audioFrame->extended_data),
                audioFrame->nb_samples);
            av_frame_unref(audioFrame);

            if (convertedSamples <= 0) {
                continue;
            }

            const int bytesPerSample = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
            const int dataSize = convertedSamples * channelCount * bytesPerSample;
            writeAudio(audioSink, audioDevice, audioBuffer.constData(), dataSize);
        }
    }

    void writeAudio(QAudioSink *audioSink, QIODevice *audioDevice, const char *data, int size)
    {
        audioSink->setVolume(effectiveVolume());

        int written = 0;
        while (written < size && !stopRequested() && audioDevice) {
            const qsizetype chunk = audioDevice->write(data + written, size - written);
            if (chunk > 0) {
                written += static_cast<int>(chunk);
                continue;
            }

            QThread::msleep(2);
        }
    }

    bool emitFrame(SwsContextPtr &sws, AVFrame *frame, qint64 framePosition, qint64 generation, bool allowSmoothScale)
    {
        int targetWidth = frame->width;
        int targetHeight = frame->height;
        if (!allowSmoothScale) {
            const int largestDimension = std::max(frame->width, frame->height);
            const int previewMaximumDimension = currentSeekPreviewMaximumDimension();
            if (largestDimension > previewMaximumDimension) {
                targetWidth = std::max(1, frame->width * previewMaximumDimension / largestDimension);
                targetHeight = std::max(1, frame->height * previewMaximumDimension / largestDimension);
            }
        }
        const int scaleFlags = allowSmoothScale ? SWS_FAST_BILINEAR : SWS_POINT;

        sws.reset(sws_getCachedContext(
            sws.release(),
            frame->width,
            frame->height,
            static_cast<AVPixelFormat>(frame->format),
            targetWidth,
            targetHeight,
            AV_PIX_FMT_RGBA,
            scaleFlags,
            nullptr,
            nullptr,
            nullptr));

        if (!sws) {
            emitError(QStringLiteral("Could not create video scaler"));
            return false;
        }

        QImage image(targetWidth, targetHeight, QImage::Format_RGBA8888);
        uint8_t *dstData[] = {image.bits()};
        int dstLinesize[] = {static_cast<int>(image.bytesPerLine())};
        sws_scale(sws.get(), frame->data, frame->linesize, 0, frame->height, dstData, dstLinesize);
        emit frameReady(image, framePosition, generation);
        return true;
    }

    int currentSeekPreviewMaximumDimension()
    {
        QMutexLocker locker(&m_mutex);
        return m_seekPreviewMaximumDimension;
    }

    qreal effectiveVolume()
    {
        QMutexLocker locker(&m_mutex);
        return m_muted ? 0.0 : static_cast<qreal>(m_volume);
    }

    bool shouldLoop(int completedLoops)
    {
        QMutexLocker locker(&m_mutex);
        return m_loops < 0 || completedLoops < m_loops;
    }

    void throttleFrame(qint64 framePosition, QElapsedTimer &clock, qint64 &clockStartPosition, bool &clockValid)
    {
        qreal playbackRate = 1.0;
        {
            QMutexLocker locker(&m_mutex);
            playbackRate = std::max<qreal>(0.05, m_playbackRate);
        }

        if (!clockValid) {
            clock.restart();
            clockStartPosition = framePosition;
            clockValid = true;
            return;
        }

        const qint64 targetElapsed = static_cast<qint64>((framePosition - clockStartPosition) / playbackRate);
        const qint64 delay = targetElapsed - clock.elapsed();
        if (delay > 1) {
            sleepUntilFrameDue(delay);
        }
    }

    void sleepUntilFrameDue(qint64 delay)
    {
        qint64 remaining = std::min<qint64>(delay, 40);
        while (remaining > 0 && !stopRequested() && !hasPendingSeek()) {
            const qint64 slice = std::min<qint64>(remaining, 5);
            QThread::msleep(static_cast<unsigned long>(slice));
            remaining -= slice;
        }
    }

    void emitError(const QString &message)
    {
        emit errorReady(message);
        emit statusReady(QmlScrubPlayer::Status::InvalidMedia);
    }

    QMutex m_mutex;
    QWaitCondition m_waitCondition;
    QUrl m_source;
    bool m_playing = false;
    bool m_stopRequested = false;
    bool m_seekPending = false;
    qint64 m_seekPosition = 0;
    SeekMode m_seekMode = SeekMode::Exact;
    qint64 m_seekGeneration = 0;
    int m_loops = 1;
    qreal m_playbackRate = 1.0;
    float m_volume = 1.0f;
    bool m_muted = false;
    bool m_audioSuppressed = false;
    int m_seekPreviewMaximumDimension = defaultSeekPreviewMaximumDimension;
};

class QmlScrubPreviewDecoder : public QThread
{
    Q_OBJECT

public:
    explicit QmlScrubPreviewDecoder(QObject *parent = nullptr)
        : QThread(parent)
    {
    }

    ~QmlScrubPreviewDecoder() override
    {
        requestStop();
        wait();
    }

    void setSource(const QUrl &source)
    {
        QMutexLocker locker(&m_mutex);
        m_source = source;
    }

    void setPreviewMaximumDimension(int previewMaximumDimension)
    {
        QMutexLocker locker(&m_mutex);
        m_previewMaximumDimension = std::max(64, previewMaximumDimension);
    }

    void requestPreview(qint64 position, qint64 generation)
    {
        {
            QMutexLocker locker(&m_mutex);
            m_pendingPosition = std::max<qint64>(0, position);
            m_pendingGeneration = generation;
            m_previewPending = true;
            m_waitCondition.wakeAll();
        }

        if (!isRunning()) {
            start();
        }
    }

    void requestStop()
    {
        QMutexLocker locker(&m_mutex);
        m_stopRequested = true;
        m_waitCondition.wakeAll();
    }

signals:
    void frameReady(const QImage &image, qint64 position, qint64 generation);
    void errorReady(const QString &message);

protected:
    void run() override
    {
        const QUrl source = currentSource();
        if (source.isEmpty()) {
            return;
        }

        AVFormatContext *rawFormat = nullptr;
        const QByteArray input = urlToInput(source).toUtf8();
        if (const int result = avformat_open_input(&rawFormat, input.constData(), nullptr, nullptr); result < 0) {
            emit errorReady(QStringLiteral("Could not open preview input: %1").arg(ffmpegError(result)));
            return;
        }
        FormatContextPtr format(rawFormat);

        if (const int result = avformat_find_stream_info(format.get(), nullptr); result < 0) {
            emit errorReady(QStringLiteral("Could not read preview stream info: %1").arg(ffmpegError(result)));
            return;
        }

        const int videoStream = av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (videoStream < 0) {
            emit errorReady(QStringLiteral("No preview video stream found"));
            return;
        }

        AVStream *stream = format->streams[videoStream];
        const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) {
            emit errorReady(QStringLiteral("No preview decoder found"));
            return;
        }

        CodecContextPtr codecContext(avcodec_alloc_context3(codec));
        if (!codecContext) {
            emit errorReady(QStringLiteral("Could not allocate preview decoder context"));
            return;
        }

        if (const int result = avcodec_parameters_to_context(codecContext.get(), stream->codecpar); result < 0) {
            emit errorReady(QStringLiteral("Could not copy preview codec parameters: %1").arg(ffmpegError(result)));
            return;
        }

        codecContext->thread_count = 1;
        codecContext->skip_frame = AVDISCARD_NONKEY;
        HardwareDecoder hardwareDecoder;
        configureHardwareDecoder(codec, codecContext.get(), hardwareDecoder);

        if (const int result = avcodec_open2(codecContext.get(), codec, nullptr); result < 0) {
            emit errorReady(QStringLiteral("Could not open preview decoder: %1").arg(ffmpegError(result)));
            return;
        }

        PacketPtr packet(av_packet_alloc());
        FramePtr frame(av_frame_alloc());
        FramePtr transferFrame(av_frame_alloc());
        if (!packet || !frame) {
            emit errorReady(QStringLiteral("Could not allocate preview frame buffers"));
            return;
        }

        SwsContextPtr sws(nullptr, &sws_freeContext);

        while (!stopRequested()) {
            PreviewRequest request = takeRequest();
            if (stopRequested()) {
                break;
            }

            const qint64 keyframePosition = nearestIndexedKeyframeMs(stream, request.position);
            if (!seekToPreviewFrame(format.get(), codecContext.get(), stream, keyframePosition)) {
                continue;
            }

            bool emitted = false;
            while (!stopRequested() && !hasNewerRequest(request.generation)) {
                const int readResult = av_read_frame(format.get(), packet.get());
                if (readResult == AVERROR_EOF) {
                    break;
                }
                if (readResult < 0) {
                    emit errorReady(QStringLiteral("Could not read preview frame: %1").arg(ffmpegError(readResult)));
                    break;
                }

                if (packet->stream_index != videoStream) {
                    av_packet_unref(packet.get());
                    continue;
                }

                const int sendResult = avcodec_send_packet(codecContext.get(), packet.get());
                av_packet_unref(packet.get());
                if (sendResult < 0 && sendResult != AVERROR(EAGAIN)) {
                    emit errorReady(QStringLiteral("Could not send preview packet: %1").arg(ffmpegError(sendResult)));
                    break;
                }

                while (!stopRequested() && !hasNewerRequest(request.generation)) {
                    const int receiveResult = avcodec_receive_frame(codecContext.get(), frame.get());
                    if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
                        break;
                    }
                    if (receiveResult < 0) {
                        emit errorReady(QStringLiteral("Could not decode preview frame: %1").arg(ffmpegError(receiveResult)));
                        return;
                    }

                    AVFrame *displayFrame = softwareFrameForScaling(frame.get(), hardwareDecoder, transferFrame);
                    if (!displayFrame) {
                        emit errorReady(QStringLiteral("Could not transfer preview hardware frame"));
                        return;
                    }

                    const qint64 framePosition = timestampToMs(displayFrame->best_effort_timestamp, stream->time_base);
                    if (!emitPreviewFrame(sws, displayFrame, framePosition, request.generation)) {
                        return;
                    }
                    av_frame_unref(frame.get());
                    emitted = true;
                    break;
                }

                if (emitted) {
                    break;
                }
            }
        }
    }

private:
    struct PreviewRequest
    {
        qint64 position = 0;
        qint64 generation = 0;
    };

    QUrl currentSource()
    {
        QMutexLocker locker(&m_mutex);
        return m_source;
    }

    bool stopRequested()
    {
        QMutexLocker locker(&m_mutex);
        return m_stopRequested;
    }

    PreviewRequest takeRequest()
    {
        QMutexLocker locker(&m_mutex);
        while (!m_stopRequested && !m_previewPending) {
            m_waitCondition.wait(&m_mutex);
        }

        PreviewRequest request{m_pendingPosition, m_pendingGeneration};
        m_previewPending = false;
        return request;
    }

    bool hasNewerRequest(qint64 generation)
    {
        QMutexLocker locker(&m_mutex);
        return m_previewPending && m_pendingGeneration > generation;
    }

    qint64 nearestIndexedKeyframeMs(AVStream *stream, qint64 positionMs)
    {
        const qint64 timestamp = av_rescale_q(positionMs, AVRational{1, 1000}, stream->time_base);
        const AVIndexEntry *entry = avformat_index_get_entry_from_timestamp(stream, timestamp, AVSEEK_FLAG_BACKWARD);
        if (!entry) {
            return positionMs;
        }

        return timestampToMs(entry->timestamp, stream->time_base);
    }

    bool seekToPreviewFrame(AVFormatContext *format, AVCodecContext *codecContext, AVStream *stream, qint64 positionMs)
    {
        const qint64 timestamp = av_rescale_q(positionMs, AVRational{1, 1000}, stream->time_base);
        if (const int result = av_seek_frame(format, stream->index, timestamp, AVSEEK_FLAG_BACKWARD); result < 0) {
            emit errorReady(QStringLiteral("Could not preview seek: %1").arg(ffmpegError(result)));
            return false;
        }

        avcodec_flush_buffers(codecContext);
        codecContext->skip_frame = AVDISCARD_NONKEY;
        return true;
    }

    bool emitPreviewFrame(SwsContextPtr &sws, AVFrame *frame, qint64 framePosition, qint64 generation)
    {
        int targetWidth = frame->width;
        int targetHeight = frame->height;
        const int largestDimension = std::max(frame->width, frame->height);
        const int previewMaximumDimension = currentPreviewMaximumDimension();
        if (largestDimension > previewMaximumDimension) {
            targetWidth = std::max(1, frame->width * previewMaximumDimension / largestDimension);
            targetHeight = std::max(1, frame->height * previewMaximumDimension / largestDimension);
        }

        sws.reset(sws_getCachedContext(
            sws.release(),
            frame->width,
            frame->height,
            static_cast<AVPixelFormat>(frame->format),
            targetWidth,
            targetHeight,
            AV_PIX_FMT_RGBA,
            SWS_POINT,
            nullptr,
            nullptr,
            nullptr));

        if (!sws) {
            emit errorReady(QStringLiteral("Could not create preview scaler"));
            return false;
        }

        QImage image(targetWidth, targetHeight, QImage::Format_RGBA8888);
        uint8_t *dstData[] = {image.bits()};
        int dstLinesize[] = {static_cast<int>(image.bytesPerLine())};
        sws_scale(sws.get(), frame->data, frame->linesize, 0, frame->height, dstData, dstLinesize);
        emit frameReady(image, framePosition, generation);
        return true;
    }

    int currentPreviewMaximumDimension()
    {
        QMutexLocker locker(&m_mutex);
        return m_previewMaximumDimension;
    }

    QMutex m_mutex;
    QWaitCondition m_waitCondition;
    QUrl m_source;
    bool m_stopRequested = false;
    bool m_previewPending = false;
    qint64 m_pendingPosition = 0;
    qint64 m_pendingGeneration = 0;
    int m_previewMaximumDimension = defaultSeekPreviewMaximumDimension;
};

QmlScrubPlayer::QmlScrubPlayer(QObject *parent)
    : QObject(parent),
      m_ownedVideoSink(new QVideoSink(this)),
      m_videoSink(m_ownedVideoSink)
{
}

QmlScrubPlayer::~QmlScrubPlayer()
{
    if (m_previewDecoder) {
        m_previewDecoder->requestStop();
        m_previewDecoder->wait();
    }
    if (m_decoder) {
        m_decoder->requestStop();
        m_decoder->wait();
    }
}

QUrl QmlScrubPlayer::source() const
{
    return m_source;
}

void QmlScrubPlayer::setSource(const QUrl &source)
{
    if (m_source == source) {
        return;
    }

    const bool shouldStartPlayback = m_autoPlay || m_playing;
    setPlaying(false);

    m_source = source;
    emit sourceChanged();

    setPositionFromDecoder(0);
    m_expectedFrameGeneration = 0;
    m_frameGenerationCounter = 0;
    setDuration(0);
    setErrorString(QString());
    setStatus(source.isEmpty() ? Status::NoMedia : Status::Loading);
    recreateDecoder();

    if (shouldStartPlayback && !source.isEmpty()) {
        play();
    }
}

bool QmlScrubPlayer::autoPlay() const
{
    return m_autoPlay;
}

void QmlScrubPlayer::setAutoPlay(bool autoPlay)
{
    if (m_autoPlay == autoPlay) {
        return;
    }

    m_autoPlay = autoPlay;
    emit autoPlayChanged();
}

bool QmlScrubPlayer::isPlaying() const
{
    return m_playing;
}

qint64 QmlScrubPlayer::duration() const
{
    return m_duration;
}

qint64 QmlScrubPlayer::position() const
{
    return m_position;
}

void QmlScrubPlayer::setPosition(qint64 position)
{
    seek(position);
}

float QmlScrubPlayer::volume() const
{
    return m_volume;
}

void QmlScrubPlayer::setVolume(float volume)
{
    const float clampedVolume = std::clamp(volume, 0.0f, 1.0f);
    if (qFuzzyCompare(m_volume, clampedVolume)) {
        return;
    }

    m_volume = clampedVolume;
    if (m_decoder) {
        m_decoder->setVolume(clampedVolume);
    }
    emit volumeChanged();
}

bool QmlScrubPlayer::isMuted() const
{
    return m_muted;
}

void QmlScrubPlayer::setMuted(bool muted)
{
    if (m_muted == muted) {
        return;
    }

    m_muted = muted;
    if (m_decoder) {
        m_decoder->setMuted(muted);
    }
    emit mutedChanged();
}

int QmlScrubPlayer::loops() const
{
    return m_loops;
}

void QmlScrubPlayer::setLoops(int loops)
{
    if (m_loops == loops) {
        return;
    }

    m_loops = loops;
    if (m_decoder) {
        m_decoder->setLoops(loops);
    }
    emit loopsChanged();
}

qreal QmlScrubPlayer::playbackRate() const
{
    return m_playbackRate;
}

void QmlScrubPlayer::setPlaybackRate(qreal playbackRate)
{
    const qreal clampedRate = std::clamp(playbackRate, 0.05, 8.0);
    if (qFuzzyCompare(m_playbackRate, clampedRate)) {
        return;
    }

    m_playbackRate = clampedRate;
    if (m_decoder) {
        m_decoder->setPlaybackRate(clampedRate);
    }
    emit playbackRateChanged();
}

int QmlScrubPlayer::seekPreviewMaximumDimension() const
{
    return m_seekPreviewMaximumDimension;
}

void QmlScrubPlayer::setSeekPreviewMaximumDimension(int seekPreviewMaximumDimension)
{
    const int clampedDimension = std::clamp(seekPreviewMaximumDimension, 64, 4096);
    if (m_seekPreviewMaximumDimension == clampedDimension) {
        return;
    }

    m_seekPreviewMaximumDimension = clampedDimension;
    if (m_decoder) {
        m_decoder->setSeekPreviewMaximumDimension(clampedDimension);
    }
    if (m_previewDecoder) {
        m_previewDecoder->setPreviewMaximumDimension(clampedDimension);
    }
    emit seekPreviewMaximumDimensionChanged();
}

QmlScrubPlayer::Status QmlScrubPlayer::status() const
{
    return m_status;
}

QString QmlScrubPlayer::errorString() const
{
    return m_errorString;
}

QVideoSink *QmlScrubPlayer::videoSink() const
{
    return m_videoSink;
}

void QmlScrubPlayer::setVideoSink(QVideoSink *videoSink)
{
    QVideoSink *nextVideoSink = videoSink ? videoSink : m_ownedVideoSink;
    if (m_videoSink == nextVideoSink) {
        return;
    }

    m_videoSink = nextVideoSink;
    emit videoSinkChanged();
}

void QmlScrubPlayer::play()
{
    if (m_source.isEmpty()) {
        return;
    }

    if (!m_decoder) {
        recreateDecoder();
    }

    setPlaying(true);
    m_decoder->setPlaying(true);
}

void QmlScrubPlayer::pause()
{
    setPlaying(false);
    if (m_decoder) {
        m_decoder->setPlaying(false);
    }
}

void QmlScrubPlayer::stop()
{
    setPlaying(false);
    seek(0);
    if (m_decoder) {
        m_decoder->setPlaying(false);
    }
}

void QmlScrubPlayer::seek(qint64 position)
{
    const qint64 clampedPosition = std::clamp<qint64>(position, 0, m_duration > 0 ? m_duration : position);
    setPositionFromDecoder(clampedPosition);
    if (!m_decoder) {
        recreateDecoder();
    }
    if (m_decoder) {
        const qint64 generation = ++m_frameGenerationCounter;
        m_expectedFrameGeneration = generation;
        m_decoder->setAudioSuppressed(false);
        m_decoder->requestSeek(clampedPosition, QmlScrubDecoder::SeekMode::Exact, generation);
    }
}

void QmlScrubPlayer::previewSeek(qint64 position)
{
    const qint64 clampedPosition = std::clamp<qint64>(position, 0, m_duration > 0 ? m_duration : position);
    setPositionFromDecoder(clampedPosition);
    if (!m_previewDecoder) {
        recreateDecoder();
    }
    if (m_previewDecoder) {
        const qint64 generation = ++m_frameGenerationCounter;
        m_expectedFrameGeneration = generation;
        if (m_decoder) {
            m_decoder->setAudioSuppressed(true);
            m_decoder->requestSeek(clampedPosition, QmlScrubDecoder::SeekMode::Preview, generation);
        }
        m_previewDecoder->requestPreview(clampedPosition, generation);
    }
}

void QmlScrubPlayer::endPreviewSeek(qint64 position)
{
    seek(position);
}

void QmlScrubPlayer::recreateDecoder()
{
    if (m_previewDecoder) {
        m_previewDecoder->requestStop();
        m_previewDecoder->wait();
        m_previewDecoder->deleteLater();
        m_previewDecoder = nullptr;
    }

    if (m_decoder) {
        m_decoder->requestStop();
        m_decoder->wait();
        m_decoder->deleteLater();
        m_decoder = nullptr;
    }

    if (m_source.isEmpty()) {
        return;
    }

    m_decoder = new QmlScrubDecoder(this);
    m_decoder->setSource(m_source);
    m_decoder->setLoops(m_loops);
    m_decoder->setPlaybackRate(m_playbackRate);
    m_decoder->setVolume(m_volume);
    m_decoder->setMuted(m_muted);
    m_decoder->setSeekPreviewMaximumDimension(m_seekPreviewMaximumDimension);

    connect(m_decoder, &QmlScrubDecoder::durationReady, this, &QmlScrubPlayer::setDuration);
    connect(m_decoder, &QmlScrubDecoder::statusReady, this, &QmlScrubPlayer::setStatus);
    connect(m_decoder, &QmlScrubDecoder::errorReady, this, &QmlScrubPlayer::setErrorString);
    connect(m_decoder, &QmlScrubDecoder::playbackEnded, this, [this] {
        setPlaying(false);
    });
    connect(m_decoder, &QmlScrubDecoder::frameReady, this, [this](const QImage &image, qint64 position, qint64 generation) {
        if (generation < m_expectedFrameGeneration) {
            return;
        }
        setPositionFromDecoder(position);
        if (m_videoSink) {
            m_videoSink->setVideoFrame(QVideoFrame(image));
        }
    });

    m_decoder->start();

    m_previewDecoder = new QmlScrubPreviewDecoder(this);
    m_previewDecoder->setSource(m_source);
    m_previewDecoder->setPreviewMaximumDimension(m_seekPreviewMaximumDimension);
    connect(m_previewDecoder, &QmlScrubPreviewDecoder::errorReady, this, &QmlScrubPlayer::setErrorString);
    connect(m_previewDecoder, &QmlScrubPreviewDecoder::frameReady, this, [this](const QImage &image, qint64 position, qint64 generation) {
        if (generation < m_expectedFrameGeneration) {
            return;
        }
        setPositionFromDecoder(position);
        if (m_videoSink) {
            m_videoSink->setVideoFrame(QVideoFrame(image));
        }
    });
    m_previewDecoder->start();
}

void QmlScrubPlayer::setPlaying(bool playing)
{
    if (m_playing == playing) {
        return;
    }

    m_playing = playing;
    emit playingChanged();
}

void QmlScrubPlayer::setDuration(qint64 duration)
{
    if (m_duration == duration) {
        return;
    }

    m_duration = duration;
    emit durationChanged();
}

void QmlScrubPlayer::setPositionFromDecoder(qint64 position)
{
    if (m_position == position) {
        return;
    }

    m_position = position;
    emit positionChanged();
}

void QmlScrubPlayer::setStatus(Status status)
{
    if (m_status == status) {
        return;
    }

    m_status = status;
    emit statusChanged();
}

void QmlScrubPlayer::setErrorString(const QString &errorString)
{
    if (m_errorString == errorString) {
        return;
    }

    m_errorString = errorString;
    emit errorChanged();
}

#include "qmlscrubplayer.moc"

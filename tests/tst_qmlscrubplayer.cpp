#include <qmlscrubplayer.h>

#include <QDir>
#include <QElapsedTimer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QVariant>
#include <QVideoSink>

#include <functional>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec_id.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
}

namespace {

QString ffmpegError(int errorCode)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(errorCode, buffer, sizeof(buffer));
    return QString::fromLocal8Bit(buffer);
}

void requireSuccess(int result, const char *operation)
{
    QVERIFY2(result >= 0, qPrintable(QString::fromLatin1("%1 failed: %2").arg(QString::fromLatin1(operation), ffmpegError(result))));
}

void writePacket(AVFormatContext *format, AVStream *stream, QByteArray &payload, int frameIndex, int duration)
{
    AVPacket *packet = av_packet_alloc();
    QVERIFY(packet != nullptr);
    requireSuccess(av_new_packet(packet, payload.size()), "av_new_packet");
    memcpy(packet->data, payload.constData(), payload.size());
    packet->stream_index = stream->index;
    packet->pts = frameIndex;
    packet->dts = frameIndex;
    packet->duration = duration;
    packet->flags |= AV_PKT_FLAG_KEY;
    requireSuccess(av_interleaved_write_frame(format, packet), "av_interleaved_write_frame");
    av_packet_free(&packet);
}

bool writeEncodedVideoPackets(AVFormatContext *format, AVStream *stream, AVCodecContext *codecContext, AVFrame *frame, QString *error)
{
    int result = avcodec_send_frame(codecContext, frame);
    if (result < 0) {
        *error = QStringLiteral("Could not send video frame: %1").arg(ffmpegError(result));
        return false;
    }

    while (result >= 0) {
        AVPacket *packet = av_packet_alloc();
        if (!packet) {
            *error = QStringLiteral("Could not allocate encoded packet");
            return false;
        }

        result = avcodec_receive_packet(codecContext, packet);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
            av_packet_free(&packet);
            return true;
        }
        if (result < 0) {
            *error = QStringLiteral("Could not receive encoded packet: %1").arg(ffmpegError(result));
            av_packet_free(&packet);
            return false;
        }

        av_packet_rescale_ts(packet, codecContext->time_base, stream->time_base);
        packet->stream_index = stream->index;
        packet->flags |= AV_PKT_FLAG_KEY;
        result = av_interleaved_write_frame(format, packet);
        av_packet_free(&packet);
        if (result < 0) {
            *error = QStringLiteral("Could not write encoded packet: %1").arg(ffmpegError(result));
            return false;
        }
    }

    return true;
}

QString createTestMedia(const QString &path, bool withAudio)
{
    AVFormatContext *format = nullptr;
    int result = avformat_alloc_output_context2(&format, nullptr, "matroska", path.toUtf8().constData());
    if (result < 0 || !format) {
        return QStringLiteral("Could not allocate output context: %1").arg(ffmpegError(result));
    }

    const AVCodec *videoCodec = avcodec_find_encoder(AV_CODEC_ID_FFV1);
    if (!videoCodec) {
        avformat_free_context(format);
        return QStringLiteral("FFV1 encoder is not available");
    }

    AVStream *video = avformat_new_stream(format, nullptr);
    if (!video) {
        avformat_free_context(format);
        return QStringLiteral("Could not create video stream");
    }

    constexpr int width = 32;
    constexpr int height = 24;
    constexpr int frameCount = 30;
    AVCodecContext *videoCodecContext = avcodec_alloc_context3(videoCodec);
    if (!videoCodecContext) {
        avformat_free_context(format);
        return QStringLiteral("Could not allocate video encoder context");
    }
    videoCodecContext->codec_type = AVMEDIA_TYPE_VIDEO;
    videoCodecContext->codec_id = AV_CODEC_ID_FFV1;
    videoCodecContext->width = width;
    videoCodecContext->height = height;
    videoCodecContext->time_base = AVRational{1, 10};
    videoCodecContext->framerate = AVRational{10, 1};
    videoCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    videoCodecContext->gop_size = 1;
    if (format->oformat->flags & AVFMT_GLOBALHEADER) {
        videoCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    result = avcodec_open2(videoCodecContext, videoCodec, nullptr);
    if (result < 0) {
        avcodec_free_context(&videoCodecContext);
        avformat_free_context(format);
        return QStringLiteral("Could not open video encoder: %1").arg(ffmpegError(result));
    }
    video->time_base = videoCodecContext->time_base;
    video->duration = frameCount;
    result = avcodec_parameters_from_context(video->codecpar, videoCodecContext);
    if (result < 0) {
        avcodec_free_context(&videoCodecContext);
        avformat_free_context(format);
        return QStringLiteral("Could not copy video encoder parameters: %1").arg(ffmpegError(result));
    }

    AVStream *audio = nullptr;
    constexpr int sampleRate = 8000;
    constexpr int samplesPerVideoFrame = sampleRate / 10;
    if (withAudio) {
        audio = avformat_new_stream(format, nullptr);
        if (!audio) {
            avcodec_free_context(&videoCodecContext);
            avformat_free_context(format);
            return QStringLiteral("Could not create audio stream");
        }
        audio->time_base = AVRational{1, sampleRate};
        audio->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        audio->codecpar->codec_id = AV_CODEC_ID_PCM_S16LE;
        audio->codecpar->format = AV_SAMPLE_FMT_S16;
        audio->codecpar->sample_rate = sampleRate;
        audio->codecpar->ch_layout = AV_CHANNEL_LAYOUT_MONO;
        audio->codecpar->bits_per_coded_sample = 16;
        audio->codecpar->block_align = 2;
        audio->codecpar->bit_rate = sampleRate * 16;
        audio->duration = samplesPerVideoFrame * frameCount;
    }

    if (!(format->oformat->flags & AVFMT_NOFILE)) {
        result = avio_open(&format->pb, path.toUtf8().constData(), AVIO_FLAG_WRITE);
        if (result < 0) {
            avcodec_free_context(&videoCodecContext);
            avformat_free_context(format);
            return QStringLiteral("Could not open output file: %1").arg(ffmpegError(result));
        }
    }

    result = avformat_write_header(format, nullptr);
    if (result < 0) {
        avio_closep(&format->pb);
        avcodec_free_context(&videoCodecContext);
        avformat_free_context(format);
        return QStringLiteral("Could not write header: %1").arg(ffmpegError(result));
    }

    AVFrame *videoFrame = av_frame_alloc();
    if (!videoFrame) {
        avio_closep(&format->pb);
        avcodec_free_context(&videoCodecContext);
        avformat_free_context(format);
        return QStringLiteral("Could not allocate video frame");
    }
    videoFrame->format = videoCodecContext->pix_fmt;
    videoFrame->width = width;
    videoFrame->height = height;
    result = av_frame_get_buffer(videoFrame, 32);
    if (result < 0) {
        av_frame_free(&videoFrame);
        avio_closep(&format->pb);
        avcodec_free_context(&videoCodecContext);
        avformat_free_context(format);
        return QStringLiteral("Could not allocate video frame buffer: %1").arg(ffmpegError(result));
    }

    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        result = av_frame_make_writable(videoFrame);
        if (result < 0) {
            av_frame_free(&videoFrame);
            avio_closep(&format->pb);
            avcodec_free_context(&videoCodecContext);
            avformat_free_context(format);
            return QStringLiteral("Could not make video frame writable: %1").arg(ffmpegError(result));
        }
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                videoFrame->data[0][y * videoFrame->linesize[0] + x] = static_cast<uint8_t>((frameIndex * 23 + x * 3 + y * 5) & 0xff);
            }
        }
        for (int y = 0; y < height / 2; ++y) {
            for (int x = 0; x < width / 2; ++x) {
                videoFrame->data[1][y * videoFrame->linesize[1] + x] = static_cast<uint8_t>(96 + frameIndex * 2);
                videoFrame->data[2][y * videoFrame->linesize[2] + x] = static_cast<uint8_t>(160 - frameIndex * 2);
            }
        }
        videoFrame->pts = frameIndex;
        QString encodeError;
        if (!writeEncodedVideoPackets(format, video, videoCodecContext, videoFrame, &encodeError)) {
            av_frame_free(&videoFrame);
            avio_closep(&format->pb);
            avcodec_free_context(&videoCodecContext);
            avformat_free_context(format);
            return encodeError;
        }

        if (audio) {
            QByteArray pcm(samplesPerVideoFrame * 2, Qt::Uninitialized);
            auto *samples = reinterpret_cast<qint16 *>(pcm.data());
            for (int i = 0; i < samplesPerVideoFrame; ++i) {
                samples[i] = static_cast<qint16>(((i + frameIndex * samplesPerVideoFrame) % 80) * 128 - 5120);
            }
            writePacket(format, audio, pcm, frameIndex * samplesPerVideoFrame, samplesPerVideoFrame);
        }
    }

    QString flushError;
    if (!writeEncodedVideoPackets(format, video, videoCodecContext, nullptr, &flushError)) {
        av_frame_free(&videoFrame);
        avio_closep(&format->pb);
        avcodec_free_context(&videoCodecContext);
        avformat_free_context(format);
        return flushError;
    }

    result = av_write_trailer(format);
    if (result < 0) {
        av_frame_free(&videoFrame);
        avio_closep(&format->pb);
        avcodec_free_context(&videoCodecContext);
        avformat_free_context(format);
        return QStringLiteral("Could not write trailer: %1").arg(ffmpegError(result));
    }

    av_frame_free(&videoFrame);
    avio_closep(&format->pb);
    avcodec_free_context(&videoCodecContext);
    avformat_free_context(format);
    return {};
}

bool waitUntil(std::function<bool()> predicate, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate()) {
            return true;
        }
        QTest::qWait(20);
    }
    return predicate();
}

} // namespace

class QmlScrubPlayerTest : public QObject
{
    Q_OBJECT

private slots:
    void defaultState();
    void sourceChangeEmitsSignals();
    void playbackOptionsEmitSignals();
    void videoSinkCanBeReplacedAndReset();
    void seekWithoutSourceUpdatesPosition();
    void qmlImportCreatesPlayer();
    void invalidSourceReportsError();
    void generatedVideoEmitsDurationAndFrames();
    void generatedAudioVideoDoesNotBlockFrames();
    void previewSeekSuppressesPlaybackAudio();
};

void QmlScrubPlayerTest::defaultState()
{
    QmlScrubPlayer player;

    QVERIFY(player.source().isEmpty());
    QVERIFY(!player.autoPlay());
    QVERIFY(!player.isPlaying());
    QCOMPARE(player.duration(), 0);
    QCOMPARE(player.position(), 0);
    QCOMPARE(player.remainingTime(), 0);
    QCOMPARE(player.progress(), 0.0);
    QCOMPARE(player.timecode(), QStringLiteral("00:00:00:00"));
    QCOMPARE(player.durationTimecode(), QStringLiteral("00:00:00:00"));
    QCOMPARE(player.currentFrame(), 0);
    QCOMPARE(player.frameCount(), 0);
    QCOMPARE(player.videoSize(), QSize());
    QCOMPARE(player.aspectRatio(), 0.0);
    QCOMPARE(player.frameRate(), 0.0);
    QVERIFY(player.videoCodecName().isEmpty());
    QVERIFY(player.audioCodecName().isEmpty());
    QVERIFY(player.pixelFormat().isEmpty());
    QVERIFY(player.audioFormat().isEmpty());
    QVERIFY(!player.isSeekable());
    QVERIFY(!player.hasAudio());
    QCOMPARE(player.audioChannelCount(), 0);
    QCOMPARE(player.audioSampleRate(), 0);
    QVERIFY(!player.hasVideo());
    QVERIFY(!player.canPlay());
    QVERIFY(!player.canPause());
    QVERIFY(!player.canSeek());
    QCOMPARE(player.volume(), 1.0f);
    QVERIFY(!player.isMuted());
    QCOMPARE(player.loops(), 1);
    QCOMPARE(player.loopStart(), 0);
    QCOMPARE(player.loopEnd(), 0);
    QCOMPARE(player.playbackRate(), 1.0);
    QCOMPARE(player.seekPreviewMaximumDimension(), 4096);
    QCOMPARE(player.previewCacheSize(), 0);
    QCOMPARE(player.previewCacheLimit(), 64);
    QVERIFY(player.markers().isEmpty());
    QCOMPARE(player.playbackState(), QmlScrubPlayer::PlaybackState::Stopped);
    QCOMPARE(player.status(), QmlScrubPlayer::Status::NoMedia);
    QCOMPARE(player.error(), QmlScrubPlayer::Error::NoError);
    QVERIFY(player.errorString().isEmpty());
    QVERIFY(player.captureFrame().isNull());
    QVERIFY(player.videoSink() != nullptr);
}

void QmlScrubPlayerTest::sourceChangeEmitsSignals()
{
    QmlScrubPlayer player;
    QSignalSpy sourceSpy(&player, &QmlScrubPlayer::sourceChanged);
    QSignalSpy statusSpy(&player, &QmlScrubPlayer::statusChanged);
    QSignalSpy mediaInfoSpy(&player, &QmlScrubPlayer::mediaInfoChanged);
    player.addMarker(120);
    QSignalSpy markersSpy(&player, &QmlScrubPlayer::markersChanged);

    const QUrl source = QUrl::fromLocalFile(QStringLiteral("/tmp/nonexistent.mp4"));
    player.setSource(source);

    QCOMPARE(player.source(), source);
    QCOMPARE(sourceSpy.count(), 1);
    QVERIFY(player.markers().isEmpty());
    QCOMPARE(markersSpy.count(), 1);
    QVERIFY(statusSpy.count() >= 1);
    QVERIFY(player.duration() == 0);
    QVERIFY(player.canPlay());
    QCOMPARE(player.videoSize(), QSize());
    QCOMPARE(player.aspectRatio(), 0.0);
    QCOMPARE(player.frameRate(), 0.0);
    QCOMPARE(player.frameCount(), 0);
    QVERIFY(!player.isSeekable());
    QVERIFY(!player.hasAudio());
    QCOMPARE(player.audioChannelCount(), 0);
    QCOMPARE(player.audioSampleRate(), 0);
    QVERIFY(!player.hasVideo());
    QCOMPARE(mediaInfoSpy.count(), 0);

    player.setSource(source);
    QCOMPARE(sourceSpy.count(), 1);
}

void QmlScrubPlayerTest::playbackOptionsEmitSignals()
{
    QmlScrubPlayer player;

    QSignalSpy autoPlaySpy(&player, &QmlScrubPlayer::autoPlayChanged);
    player.setAutoPlay(true);
    QVERIFY(player.autoPlay());
    QCOMPARE(autoPlaySpy.count(), 1);
    player.setAutoPlay(true);
    QCOMPARE(autoPlaySpy.count(), 1);

    QSignalSpy volumeSpy(&player, &QmlScrubPlayer::volumeChanged);
    player.setVolume(0.25f);
    QCOMPARE(player.volume(), 0.25f);
    QCOMPARE(volumeSpy.count(), 1);
    player.setVolume(2.0f);
    QCOMPARE(player.volume(), 1.0f);
    QCOMPARE(volumeSpy.count(), 2);

    QSignalSpy mutedSpy(&player, &QmlScrubPlayer::mutedChanged);
    player.setMuted(true);
    QVERIFY(player.isMuted());
    QCOMPARE(mutedSpy.count(), 1);

    QSignalSpy loopsSpy(&player, &QmlScrubPlayer::loopsChanged);
    player.setLoops(-1);
    QCOMPARE(player.loops(), -1);
    QCOMPARE(loopsSpy.count(), 1);

    QSignalSpy loopRangeSpy(&player, &QmlScrubPlayer::loopRangeChanged);
    player.setLoopStart(100);
    QCOMPARE(player.loopStart(), 100);
    QCOMPARE(loopRangeSpy.count(), 1);
    player.setLoopEnd(500);
    QCOMPARE(player.loopEnd(), 500);
    QCOMPARE(loopRangeSpy.count(), 2);
    player.setLoopEnd(50);
    QCOMPARE(player.loopEnd(), 0);
    QCOMPARE(loopRangeSpy.count(), 3);
    player.setLoopRange(250, 750);
    QCOMPARE(player.loopStart(), 250);
    QCOMPARE(player.loopEnd(), 750);
    QCOMPARE(loopRangeSpy.count(), 4);
    player.setLoopRange(750, 250);
    QCOMPARE(player.loopStart(), 750);
    QCOMPARE(player.loopEnd(), 0);
    QCOMPARE(loopRangeSpy.count(), 5);
    player.setLoopRangeForFrames(3, 8);
    QCOMPARE(player.loopStart(), 120);
    QCOMPARE(player.loopEnd(), 320);
    QCOMPARE(loopRangeSpy.count(), 6);
    player.clearLoopRange();
    QCOMPARE(player.loopStart(), 0);
    QCOMPARE(player.loopEnd(), 0);
    QCOMPARE(loopRangeSpy.count(), 7);

    QSignalSpy rateSpy(&player, &QmlScrubPlayer::playbackRateChanged);
    player.setPlaybackRate(0.001);
    QCOMPARE(player.playbackRate(), 0.05);
    QCOMPARE(rateSpy.count(), 1);
    player.setPlaybackRate(99.0);
    QCOMPARE(player.playbackRate(), 8.0);
    QCOMPARE(rateSpy.count(), 2);

    QSignalSpy previewSizeSpy(&player, &QmlScrubPlayer::seekPreviewMaximumDimensionChanged);
    player.setSeekPreviewMaximumDimension(32);
    QCOMPARE(player.seekPreviewMaximumDimension(), 64);
    QCOMPARE(previewSizeSpy.count(), 1);
    player.setSeekPreviewMaximumDimension(9999);
    QCOMPARE(player.seekPreviewMaximumDimension(), 4096);
    QCOMPARE(previewSizeSpy.count(), 2);

    QSignalSpy previewCacheLimitSpy(&player, &QmlScrubPlayer::previewCacheLimitChanged);
    player.setPreviewCacheLimit(-1);
    QCOMPARE(player.previewCacheLimit(), 0);
    QCOMPARE(previewCacheLimitSpy.count(), 1);
    player.setPreviewCacheLimit(2048);
    QCOMPARE(player.previewCacheLimit(), 1024);
    QCOMPARE(previewCacheLimitSpy.count(), 2);

    QSignalSpy markersSpy(&player, &QmlScrubPlayer::markersChanged);
    player.addMarker(500);
    player.addMarker(100);
    QCOMPARE(player.markers(), (QVariantList{QVariant::fromValue<qint64>(100), QVariant::fromValue<qint64>(500)}));
    QCOMPARE(markersSpy.count(), 2);
    player.addMarker(500);
    QCOMPARE(markersSpy.count(), 2);
    player.addMarkerForFrame(2);
    QCOMPARE(player.markers(), (QVariantList{QVariant::fromValue<qint64>(80), QVariant::fromValue<qint64>(100), QVariant::fromValue<qint64>(500)}));
    QCOMPARE(markersSpy.count(), 3);
    player.removeMarker(100);
    QCOMPARE(player.markers(), (QVariantList{QVariant::fromValue<qint64>(80), QVariant::fromValue<qint64>(500)}));
    QCOMPARE(markersSpy.count(), 4);
    player.removeMarkerForFrame(2);
    QCOMPARE(player.markers(), (QVariantList{QVariant::fromValue<qint64>(500)}));
    QCOMPARE(markersSpy.count(), 5);
    player.clearMarkers();
    QVERIFY(player.markers().isEmpty());
    QCOMPARE(markersSpy.count(), 6);
}

void QmlScrubPlayerTest::videoSinkCanBeReplacedAndReset()
{
    QmlScrubPlayer player;
    QVideoSink replacement;
    QSignalSpy sinkSpy(&player, &QmlScrubPlayer::videoSinkChanged);

    QVideoSink *initialSink = player.videoSink();
    QVERIFY(initialSink != nullptr);

    player.setVideoSink(&replacement);
    QCOMPARE(player.videoSink(), &replacement);
    QCOMPARE(sinkSpy.count(), 1);

    player.setVideoSink(&replacement);
    QCOMPARE(sinkSpy.count(), 1);

    player.setVideoSink(nullptr);
    QCOMPARE(player.videoSink(), initialSink);
    QCOMPARE(sinkSpy.count(), 2);
}

void QmlScrubPlayerTest::seekWithoutSourceUpdatesPosition()
{
    QmlScrubPlayer player;
    QSignalSpy positionSpy(&player, &QmlScrubPlayer::positionChanged);
    QSignalSpy currentFrameSpy(&player, &QmlScrubPlayer::currentFrameChanged);

    player.seek(1200);
    QCOMPARE(player.position(), 1200);
    QCOMPARE(player.currentFrame(), 30);
    QCOMPARE(positionSpy.count(), 1);
    QCOMPARE(currentFrameSpy.count(), 1);

    player.previewSeek(2500);
    QCOMPARE(player.position(), 2500);
    QCOMPARE(player.currentFrame(), 62);
    QCOMPARE(positionSpy.count(), 2);

    player.endPreviewSeek(500);
    QCOMPARE(player.position(), 500);
    QCOMPARE(player.currentFrame(), 12);
    QCOMPARE(positionSpy.count(), 3);

    player.stepForward();
    QCOMPARE(player.position(), 540);
    QCOMPARE(player.currentFrame(), 13);
    QCOMPARE(positionSpy.count(), 4);

    player.stepBackward();
    QCOMPARE(player.position(), 500);
    QCOMPARE(player.currentFrame(), 12);
    QCOMPARE(positionSpy.count(), 5);

    player.stepForward(3);
    QCOMPARE(player.position(), 620);
    QCOMPARE(player.currentFrame(), 15);

    player.stepBackward(2);
    QCOMPARE(player.position(), 540);
    QCOMPARE(player.currentFrame(), 13);

    player.seek(10);
    player.stepBackward();
    QCOMPARE(player.position(), 0);
    QCOMPARE(player.currentFrame(), 0);

    player.seekToFrame(7);
    QCOMPARE(player.positionForFrame(7), 280);
    QCOMPARE(player.frameForPosition(280), 7);
    QCOMPARE(player.position(), 280);
    QCOMPARE(player.currentFrame(), 7);

    player.seekToFrame(-1);
    QCOMPARE(player.positionForFrame(-1), 0);
    QCOMPARE(player.frameForPosition(-100), 0);
    QCOMPARE(player.position(), 0);
    QCOMPARE(player.currentFrame(), 0);

    player.previewSeekToFrame(9);
    QCOMPARE(player.position(), 360);
    QCOMPARE(player.currentFrame(), 9);

    player.endPreviewSeekToFrame(4);
    QCOMPARE(player.position(), 160);
    QCOMPARE(player.currentFrame(), 4);
}

void QmlScrubPlayerTest::qmlImportCreatesPlayer()
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(QMLSCRUBPLAYER_BUILD_DIR "/src"));

    QQmlComponent component(&engine);
    component.setData(R"(
        import QMLScrubPlayer
        QmlScrubPlayer {
            id: player
            autoPlay: true
            volume: 0.5
            property bool helperCallsWorked: false

            function runHelperCalls() {
                setLoopRange(100, 300)
                setLoopRangeForFrames(2, 4)
                clearLoopRange()
                addMarker(100)
                addMarkerForFrame(2)
                removeMarkerForFrame(2)
                clearPreviewCache()
                requestThumbnail(0, 7)
                requestThumbnailForFrame(1, 8)
                helperCallsWorked = loopStart === 0 && loopEnd === 0
                    && markers.length === 1 && markers[0] === 100
            }
        }
    )", QUrl());

    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object != nullptr, qPrintable(component.errorString()));
    QCOMPARE(object->property("autoPlay").toBool(), true);
    QCOMPARE(object->property("volume").toFloat(), 0.5f);
    QVERIFY(QMetaObject::invokeMethod(object.get(), "runHelperCalls"));
    QCOMPARE(object->property("helperCallsWorked").toBool(), true);
}

void QmlScrubPlayerTest::invalidSourceReportsError()
{
    QmlScrubPlayer player;
    QSignalSpy errorSpy(&player, &QmlScrubPlayer::errorChanged);
    QSignalSpy statusSpy(&player, &QmlScrubPlayer::statusChanged);

    player.setSource(QUrl::fromLocalFile(QStringLiteral("/tmp/qmlscrubplayer-missing-file.avi")));

    QVERIFY(waitUntil([&player] {
        return player.status() == QmlScrubPlayer::Status::InvalidMedia;
    }));
    QCOMPARE(player.error(), QmlScrubPlayer::Error::OpenFailed);
    QVERIFY(!player.errorString().isEmpty());
    QVERIFY(errorSpy.count() >= 1);
    QVERIFY(statusSpy.count() >= 1);
}

void QmlScrubPlayerTest::generatedVideoEmitsDurationAndFrames()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mediaPath = dir.filePath(QStringLiteral("video_only.mkv"));
    const QString error = createTestMedia(mediaPath, false);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QmlScrubPlayer player;
    QVideoSink sink;
    player.setVideoSink(&sink);
    QSignalSpy durationSpy(&player, &QmlScrubPlayer::durationChanged);
    QSignalSpy mediaInfoSpy(&player, &QmlScrubPlayer::mediaInfoChanged);
    QSignalSpy frameSpy(&sink, &QVideoSink::videoFrameChanged);
    QSignalSpy positionSpy(&player, &QmlScrubPlayer::positionChanged);
    QSignalSpy thumbnailSpy(&player, &QmlScrubPlayer::thumbnailReady);

    player.setSource(QUrl::fromLocalFile(mediaPath));
    QVERIFY(waitUntil([&player] {
        return player.duration() > 0;
    }));
    QVERIFY(durationSpy.count() >= 1);
    QVERIFY(player.duration() >= 900);
    QCOMPARE(player.remainingTime(), player.duration());
    QCOMPARE(player.progress(), 0.0);
    QCOMPARE(player.timecode(), QStringLiteral("00:00:00:00"));
    QVERIFY(!player.durationTimecode().isEmpty());
    QVERIFY(mediaInfoSpy.count() >= 1);
    QCOMPARE(player.videoSize(), QSize(32, 24));
    QCOMPARE(player.aspectRatio(), 4.0 / 3.0);
    QVERIFY(player.frameRate() > 9.5);
    QVERIFY(player.frameRate() < 10.5);
    QVERIFY(player.frameCount() >= 29);
    QCOMPARE(player.videoCodecName(), QStringLiteral("ffv1"));
    QVERIFY(!player.pixelFormat().isEmpty());
    QVERIFY(player.isSeekable());
    QVERIFY(!player.hasAudio());
    QCOMPARE(player.audioChannelCount(), 0);
    QCOMPARE(player.audioSampleRate(), 0);
    QVERIFY(player.hasVideo());
    QVERIFY(player.canPlay());
    QVERIFY(!player.canPause());
    QVERIFY(player.canSeek());
    QCOMPARE(player.currentFrame(), 0);

    player.stepForward();
    QCOMPARE(player.position(), 100);
    QCOMPARE(player.currentFrame(), 1);
    player.stepBackward();
    QCOMPARE(player.position(), 0);
    QCOMPARE(player.currentFrame(), 0);
    player.seekToFrame(5);
    QCOMPARE(player.positionForFrame(5), 500);
    QCOMPARE(player.frameForPosition(500), 5);
    QCOMPARE(player.timecodeForFrame(5), QStringLiteral("00:00:00:05"));
    QCOMPARE(player.timecodeForPosition(500), QStringLiteral("00:00:00:05"));
    QCOMPARE(player.position(), 500);
    QCOMPARE(player.remainingTime(), player.duration() - 500);
    QVERIFY(player.progress() > 0.0);
    QCOMPARE(player.timecode(), QStringLiteral("00:00:00:05"));
    QCOMPARE(player.currentFrame(), 5);
    player.previewSeekToFrame(7);
    QCOMPARE(player.position(), 700);
    QCOMPARE(player.currentFrame(), 7);
    player.endPreviewSeekToFrame(2);
    QCOMPARE(player.position(), 200);
    QCOMPARE(player.currentFrame(), 2);
    player.requestThumbnailForFrame(4, 42);
    QVERIFY(waitUntil([&thumbnailSpy] {
        return thumbnailSpy.count() > 0;
    }));
    const auto thumbnailArgs = thumbnailSpy.takeFirst();
    QVERIFY(!qvariant_cast<QImage>(thumbnailArgs.at(0)).isNull());
    QCOMPARE(thumbnailArgs.at(2).toInt(), 42);
    player.requestThumbnailForFrame(5);
    QVERIFY(waitUntil([&thumbnailSpy] {
        return thumbnailSpy.count() > 0;
    }));
    const auto autoThumbnailArgs = thumbnailSpy.takeFirst();
    QVERIFY(!qvariant_cast<QImage>(autoThumbnailArgs.at(0)).isNull());
    QVERIFY(autoThumbnailArgs.at(2).toInt() > 0);
    const qint64 lastFrame = player.frameCount() - 1;
    player.seekToFrame(999);
    QCOMPARE(player.positionForFrame(999), player.positionForFrame(lastFrame));
    QCOMPARE(player.frameForPosition(player.duration() + 1000), lastFrame);
    QCOMPARE(player.currentFrame(), lastFrame);
    player.seek(0);

    player.play();
    QCOMPARE(player.playbackState(), QmlScrubPlayer::PlaybackState::Playing);
    QVERIFY(waitUntil([&frameSpy] {
        return frameSpy.count() > 0;
    }));
    QVERIFY(!player.captureFrame().isNull());

    const int framesBeforeSeek = frameSpy.count();
    player.seek(500);
    QVERIFY(waitUntil([&frameSpy, framesBeforeSeek] {
        return frameSpy.count() > framesBeforeSeek;
    }));
    QVERIFY(player.position() >= 0);
    QVERIFY(positionSpy.count() >= 1);
    player.stop();
    QCOMPARE(player.playbackState(), QmlScrubPlayer::PlaybackState::Stopped);
}

void QmlScrubPlayerTest::generatedAudioVideoDoesNotBlockFrames()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mediaPath = dir.filePath(QStringLiteral("audio_video.mkv"));
    const QString error = createTestMedia(mediaPath, true);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QmlScrubPlayer player;
    QVideoSink sink;
    player.setVideoSink(&sink);
    player.setVolume(0.0f);
    QSignalSpy mediaInfoSpy(&player, &QmlScrubPlayer::mediaInfoChanged);
    QSignalSpy frameSpy(&sink, &QVideoSink::videoFrameChanged);

    player.setSource(QUrl::fromLocalFile(mediaPath));
    QVERIFY(waitUntil([&player] {
        return player.duration() > 0;
    }));
    QVERIFY(mediaInfoSpy.count() >= 1);
    QVERIFY(player.hasAudio());
    QCOMPARE(player.audioChannelCount(), 1);
    QCOMPARE(player.audioSampleRate(), 8000);
    QCOMPARE(player.audioCodecName(), QStringLiteral("pcm_s16le"));
    QVERIFY(!player.audioFormat().isEmpty());
    QVERIFY(player.hasVideo());

    player.play();
    QVERIFY(waitUntil([&frameSpy] {
        return frameSpy.count() > 0;
    }));
    QVERIFY(player.errorString().isEmpty() || player.errorString().contains(QStringLiteral("audio"), Qt::CaseInsensitive));
    player.stop();
}

void QmlScrubPlayerTest::previewSeekSuppressesPlaybackAudio()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mediaPath = dir.filePath(QStringLiteral("preview_audio.mkv"));
    const QString error = createTestMedia(mediaPath, true);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QmlScrubPlayer player;
    QVideoSink sink;
    player.setVideoSink(&sink);
    player.setVolume(0.0f);
    QSignalSpy frameSpy(&sink, &QVideoSink::videoFrameChanged);
    QSignalSpy previewCacheSpy(&player, &QmlScrubPlayer::previewCacheChanged);

    player.setSource(QUrl::fromLocalFile(mediaPath));
    QVERIFY(waitUntil([&player] {
        return player.duration() > 0;
    }));

    player.play();
    QVERIFY(waitUntil([&frameSpy] {
        return frameSpy.count() > 0;
    }));

    const int framesBeforePreview = frameSpy.count();
    player.previewSeek(700);
    QVERIFY(waitUntil([&frameSpy, framesBeforePreview] {
        return frameSpy.count() > framesBeforePreview;
    }));
    QVERIFY(player.previewCacheSize() > 0);
    QVERIFY(previewCacheSpy.count() >= 1);

    player.clearPreviewCache();
    QCOMPARE(player.previewCacheSize(), 0);

    player.endPreviewSeek(700);
    QVERIFY(player.position() >= 0);
    player.stop();
}

QTEST_MAIN(QmlScrubPlayerTest)

#include "tst_qmlscrubplayer.moc"

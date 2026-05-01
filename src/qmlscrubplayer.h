#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QQmlEngine>
#include <QSize>
#include <QUrl>
#include <QVariantList>
#include <QVideoSink>
#include <QtCore/QtGlobal>

#if defined(QMLSCRUBPLAYER_LIBRARY)
#  define QMLSCRUBPLAYER_EXPORT Q_DECL_EXPORT
#else
#  define QMLSCRUBPLAYER_EXPORT Q_DECL_IMPORT
#endif

class QmlScrubDecoder;
class QmlScrubPreviewDecoder;

class QMLSCRUBPLAYER_EXPORT QmlScrubPlayer : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool autoPlay READ autoPlay WRITE setAutoPlay NOTIFY autoPlayChanged)
    Q_PROPERTY(bool playing READ isPlaying NOTIFY playingChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(qint64 position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(qint64 remainingTime READ remainingTime NOTIFY timelineChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY timelineChanged)
    Q_PROPERTY(QString timecode READ timecode NOTIFY timelineChanged)
    Q_PROPERTY(QString durationTimecode READ durationTimecode NOTIFY timelineChanged)
    Q_PROPERTY(qint64 currentFrame READ currentFrame NOTIFY currentFrameChanged)
    Q_PROPERTY(qint64 frameCount READ frameCount NOTIFY mediaInfoChanged)
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY mediaInfoChanged)
    Q_PROPERTY(qreal aspectRatio READ aspectRatio NOTIFY mediaInfoChanged)
    Q_PROPERTY(qreal frameRate READ frameRate NOTIFY mediaInfoChanged)
    Q_PROPERTY(QString videoCodecName READ videoCodecName NOTIFY mediaInfoChanged)
    Q_PROPERTY(QString audioCodecName READ audioCodecName NOTIFY mediaInfoChanged)
    Q_PROPERTY(QString pixelFormat READ pixelFormat NOTIFY mediaInfoChanged)
    Q_PROPERTY(QString audioFormat READ audioFormat NOTIFY mediaInfoChanged)
    Q_PROPERTY(bool seekable READ isSeekable NOTIFY mediaInfoChanged)
    Q_PROPERTY(bool hasAudio READ hasAudio NOTIFY mediaInfoChanged)
    Q_PROPERTY(int audioChannelCount READ audioChannelCount NOTIFY mediaInfoChanged)
    Q_PROPERTY(int audioSampleRate READ audioSampleRate NOTIFY mediaInfoChanged)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY mediaInfoChanged)
    Q_PROPERTY(bool canPlay READ canPlay NOTIFY playbackCapabilitiesChanged)
    Q_PROPERTY(bool canPause READ canPause NOTIFY playbackCapabilitiesChanged)
    Q_PROPERTY(bool canSeek READ canSeek NOTIFY playbackCapabilitiesChanged)
    Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(int loops READ loops WRITE setLoops NOTIFY loopsChanged)
    Q_PROPERTY(qint64 loopStart READ loopStart WRITE setLoopStart NOTIFY loopRangeChanged)
    Q_PROPERTY(qint64 loopEnd READ loopEnd WRITE setLoopEnd NOTIFY loopRangeChanged)
    Q_PROPERTY(qreal playbackRate READ playbackRate WRITE setPlaybackRate NOTIFY playbackRateChanged)
    Q_PROPERTY(int seekPreviewMaximumDimension READ seekPreviewMaximumDimension WRITE setSeekPreviewMaximumDimension NOTIFY seekPreviewMaximumDimensionChanged)
    Q_PROPERTY(int previewCacheSize READ previewCacheSize NOTIFY previewCacheChanged)
    Q_PROPERTY(int previewCacheLimit READ previewCacheLimit WRITE setPreviewCacheLimit NOTIFY previewCacheLimitChanged)
    Q_PROPERTY(QVariantList markers READ markers NOTIFY markersChanged)
    Q_PROPERTY(PlaybackState playbackState READ playbackState NOTIFY playbackStateChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(Error error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)
    Q_PROPERTY(QVideoSink *videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)

public:
    enum class Status {
        NoMedia,
        Loading,
        Loaded,
        Buffering,
        Buffered,
        Stalled,
        EndOfMedia,
        InvalidMedia
    };
    Q_ENUM(Status)

    enum class PlaybackState {
        Stopped,
        Playing,
        Paused
    };
    Q_ENUM(PlaybackState)

    enum class Error {
        NoError,
        OpenFailed,
        StreamInfoFailed,
        NoVideoStream,
        DecoderNotFound,
        DecoderAllocationFailed,
        CodecParametersFailed,
        DecoderOpenFailed,
        FrameAllocationFailed,
        ReadFailed,
        PacketSendFailed,
        DecodeFailed,
        HardwareTransferFailed,
        SeekFailed,
        AudioInitializationFailed,
        InvalidAudioFormat,
        ScalerCreationFailed,
        InvalidMedia
    };
    Q_ENUM(Error)

    explicit QmlScrubPlayer(QObject *parent = nullptr);
    ~QmlScrubPlayer() override;

    QUrl source() const;
    void setSource(const QUrl &source);

    bool autoPlay() const;
    void setAutoPlay(bool autoPlay);

    bool isPlaying() const;
    qint64 duration() const;

    qint64 position() const;
    void setPosition(qint64 position);
    qint64 remainingTime() const;
    qreal progress() const;
    QString timecode() const;
    QString durationTimecode() const;
    qint64 currentFrame() const;
    qint64 frameCount() const;

    QSize videoSize() const;
    qreal aspectRatio() const;
    qreal frameRate() const;
    QString videoCodecName() const;
    QString audioCodecName() const;
    QString pixelFormat() const;
    QString audioFormat() const;
    bool isSeekable() const;
    bool hasAudio() const;
    int audioChannelCount() const;
    int audioSampleRate() const;
    bool hasVideo() const;

    bool canPlay() const;
    bool canPause() const;
    bool canSeek() const;

    float volume() const;
    void setVolume(float volume);

    bool isMuted() const;
    void setMuted(bool muted);

    int loops() const;
    void setLoops(int loops);

    qint64 loopStart() const;
    void setLoopStart(qint64 loopStart);

    qint64 loopEnd() const;
    void setLoopEnd(qint64 loopEnd);

    qreal playbackRate() const;
    void setPlaybackRate(qreal playbackRate);

    int seekPreviewMaximumDimension() const;
    void setSeekPreviewMaximumDimension(int seekPreviewMaximumDimension);

    int previewCacheSize() const;
    int previewCacheLimit() const;
    void setPreviewCacheLimit(int previewCacheLimit);
    QVariantList markers() const;

    PlaybackState playbackState() const;
    Status status() const;
    Error error() const;
    QString errorString() const;

    QVideoSink *videoSink() const;
    void setVideoSink(QVideoSink *videoSink);

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 position);
    Q_INVOKABLE void seekToFrame(qint64 frame);
    Q_INVOKABLE void setLoopRange(qint64 loopStart, qint64 loopEnd);
    Q_INVOKABLE void setLoopRangeForFrames(qint64 loopStartFrame, qint64 loopEndFrame);
    Q_INVOKABLE void clearLoopRange();
    Q_INVOKABLE void previewSeek(qint64 position);
    Q_INVOKABLE void previewSeekToFrame(qint64 frame);
    Q_INVOKABLE void endPreviewSeek(qint64 position);
    Q_INVOKABLE void endPreviewSeekToFrame(qint64 frame);
    Q_INVOKABLE void stepForward(int frames = 1);
    Q_INVOKABLE void stepBackward(int frames = 1);
    Q_INVOKABLE qint64 positionForFrame(qint64 frame) const;
    Q_INVOKABLE qint64 frameForPosition(qint64 position) const;
    Q_INVOKABLE QString timecodeForFrame(qint64 frame) const;
    Q_INVOKABLE QString timecodeForPosition(qint64 position) const;
    Q_INVOKABLE QImage captureFrame() const;
    Q_INVOKABLE void clearPreviewCache();
    Q_INVOKABLE void requestThumbnail(qint64 position, int requestId = 0);
    Q_INVOKABLE void requestThumbnailForFrame(qint64 frame, int requestId = 0);
    Q_INVOKABLE void addMarker(qint64 position);
    Q_INVOKABLE void addMarkerForFrame(qint64 frame);
    Q_INVOKABLE void removeMarker(qint64 position);
    Q_INVOKABLE void removeMarkerForFrame(qint64 frame);
    Q_INVOKABLE void clearMarkers();

signals:
    void sourceChanged();
    void autoPlayChanged();
    void playingChanged();
    void durationChanged();
    void positionChanged();
    void timelineChanged();
    void currentFrameChanged();
    void mediaInfoChanged();
    void playbackCapabilitiesChanged();
    void volumeChanged();
    void mutedChanged();
    void loopsChanged();
    void loopRangeChanged();
    void playbackRateChanged();
    void seekPreviewMaximumDimensionChanged();
    void previewCacheChanged();
    void previewCacheLimitChanged();
    void markersChanged();
    void playbackStateChanged();
    void statusChanged();
    void errorChanged();
    void videoSinkChanged();
    void thumbnailReady(const QImage &image, qint64 position, int requestId);

private:
    void recreateDecoder();
    void setPlaying(bool playing);
    void setDuration(qint64 duration);
    void setPositionFromDecoder(qint64 position);
    void setMediaInfo(const QSize &videoSize, qreal frameRate, qint64 frameCount,
        const QString &videoCodecName, const QString &audioCodecName,
        const QString &pixelFormat, const QString &audioFormat,
        bool seekable, bool hasAudio, int audioChannelCount, int audioSampleRate, bool hasVideo);
    void resetMediaInfo();
    qint64 frameStepDuration() const;
    qint64 frameToPosition(qint64 frame) const;
    qint64 positionToFrame(qint64 position) const;
    qint64 clampPosition(qint64 position) const;
    QString formatTimecode(qint64 position) const;
    bool trimPreviewCache();
    void setStatus(Status status);
    void setError(Error error, const QString &errorString);
    void setErrorString(const QString &errorString);
    void emitPlaybackDerivedSignals();

    QmlScrubDecoder *m_decoder = nullptr;
    QmlScrubPreviewDecoder *m_previewDecoder = nullptr;
    QVideoSink *m_ownedVideoSink = nullptr;
    QVideoSink *m_videoSink = nullptr;
    QUrl m_source;
    bool m_autoPlay = false;
    bool m_playing = false;
    qint64 m_duration = 0;
    qint64 m_position = 0;
    QSize m_videoSize;
    qreal m_frameRate = 0.0;
    qint64 m_frameCount = 0;
    QString m_videoCodecName;
    QString m_audioCodecName;
    QString m_pixelFormat;
    QString m_audioFormat;
    bool m_seekable = false;
    bool m_hasAudio = false;
    int m_audioChannelCount = 0;
    int m_audioSampleRate = 0;
    bool m_hasVideo = false;
    float m_volume = 1.0f;
    bool m_muted = false;
    int m_loops = 1;
    qint64 m_loopStart = 0;
    qint64 m_loopEnd = 0;
    qreal m_playbackRate = 1.0;
    int m_seekPreviewMaximumDimension = 4096;
    qint64 m_expectedFrameGeneration = 0;
    qint64 m_frameGenerationCounter = 0;
    int m_thumbnailRequestCounter = 0;
    PlaybackState m_playbackState = PlaybackState::Stopped;
    Status m_status = Status::NoMedia;
    Error m_error = Error::NoError;
    QString m_errorString;
    QImage m_currentFrameImage;
    QHash<qint64, QImage> m_previewFrameCache;
    QList<qint64> m_markers;
    int m_previewCacheLimit = 64;
};

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QUrl>
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
    Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(int loops READ loops WRITE setLoops NOTIFY loopsChanged)
    Q_PROPERTY(qreal playbackRate READ playbackRate WRITE setPlaybackRate NOTIFY playbackRateChanged)
    Q_PROPERTY(int seekPreviewMaximumDimension READ seekPreviewMaximumDimension WRITE setSeekPreviewMaximumDimension NOTIFY seekPreviewMaximumDimensionChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
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

    float volume() const;
    void setVolume(float volume);

    bool isMuted() const;
    void setMuted(bool muted);

    int loops() const;
    void setLoops(int loops);

    qreal playbackRate() const;
    void setPlaybackRate(qreal playbackRate);

    int seekPreviewMaximumDimension() const;
    void setSeekPreviewMaximumDimension(int seekPreviewMaximumDimension);

    Status status() const;
    QString errorString() const;

    QVideoSink *videoSink() const;
    void setVideoSink(QVideoSink *videoSink);

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 position);
    Q_INVOKABLE void previewSeek(qint64 position);
    Q_INVOKABLE void endPreviewSeek(qint64 position);

signals:
    void sourceChanged();
    void autoPlayChanged();
    void playingChanged();
    void durationChanged();
    void positionChanged();
    void volumeChanged();
    void mutedChanged();
    void loopsChanged();
    void playbackRateChanged();
    void seekPreviewMaximumDimensionChanged();
    void statusChanged();
    void errorChanged();
    void videoSinkChanged();

private:
    void recreateDecoder();
    void setPlaying(bool playing);
    void setDuration(qint64 duration);
    void setPositionFromDecoder(qint64 position);
    void setStatus(Status status);
    void setErrorString(const QString &errorString);

    QmlScrubDecoder *m_decoder = nullptr;
    QmlScrubPreviewDecoder *m_previewDecoder = nullptr;
    QVideoSink *m_ownedVideoSink = nullptr;
    QVideoSink *m_videoSink = nullptr;
    QUrl m_source;
    bool m_autoPlay = false;
    bool m_playing = false;
    qint64 m_duration = 0;
    qint64 m_position = 0;
    float m_volume = 1.0f;
    bool m_muted = false;
    int m_loops = 1;
    qreal m_playbackRate = 1.0;
    int m_seekPreviewMaximumDimension = 4096;
    qint64 m_expectedFrameGeneration = 0;
    qint64 m_frameGenerationCounter = 0;
    Status m_status = Status::NoMedia;
    QString m_errorString;
};

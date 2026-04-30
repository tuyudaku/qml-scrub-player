#include <qmlscrubplayer.h>

#include <QSignalSpy>
#include <QTest>
#include <QUrl>
#include <QVideoSink>

class QmlScrubPlayerTest : public QObject
{
    Q_OBJECT

private slots:
    void defaultState();
    void sourceChangeEmitsSignals();
    void playbackOptionsEmitSignals();
    void videoSinkCanBeReplacedAndReset();
    void seekWithoutSourceUpdatesPosition();
};

void QmlScrubPlayerTest::defaultState()
{
    QmlScrubPlayer player;

    QVERIFY(player.source().isEmpty());
    QVERIFY(!player.autoPlay());
    QVERIFY(!player.isPlaying());
    QCOMPARE(player.duration(), 0);
    QCOMPARE(player.position(), 0);
    QCOMPARE(player.volume(), 1.0f);
    QVERIFY(!player.isMuted());
    QCOMPARE(player.loops(), 1);
    QCOMPARE(player.playbackRate(), 1.0);
    QCOMPARE(player.seekPreviewMaximumDimension(), 4096);
    QCOMPARE(player.status(), QmlScrubPlayer::Status::NoMedia);
    QVERIFY(player.errorString().isEmpty());
    QVERIFY(player.videoSink() != nullptr);
}

void QmlScrubPlayerTest::sourceChangeEmitsSignals()
{
    QmlScrubPlayer player;
    QSignalSpy sourceSpy(&player, &QmlScrubPlayer::sourceChanged);
    QSignalSpy statusSpy(&player, &QmlScrubPlayer::statusChanged);

    const QUrl source = QUrl::fromLocalFile(QStringLiteral("/tmp/nonexistent.mp4"));
    player.setSource(source);

    QCOMPARE(player.source(), source);
    QCOMPARE(sourceSpy.count(), 1);
    QVERIFY(statusSpy.count() >= 1);
    QVERIFY(player.duration() == 0);

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

    player.seek(1200);
    QCOMPARE(player.position(), 1200);
    QCOMPARE(positionSpy.count(), 1);

    player.previewSeek(2500);
    QCOMPARE(player.position(), 2500);
    QCOMPARE(positionSpy.count(), 2);

    player.endPreviewSeek(500);
    QCOMPARE(player.position(), 500);
    QCOMPARE(positionSpy.count(), 3);
}

QTEST_MAIN(QmlScrubPlayerTest)

#include "tst_qmlscrubplayer.moc"

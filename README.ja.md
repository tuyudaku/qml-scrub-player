# QMLScrubPlayer

QMLScrubPlayer は、QML で使うための Qt 6.10 向け動画プレイヤーモジュールです。
FFmpeg で動画をデコードし、Qt Quick には `QVideoSink` 経由でフレームを渡します。
通常再生だけでなく、シークバーをドラッグした時の軽快なプレビュー表示を重視しています。

## 特徴

- QML 型: `QmlScrubPlayer`
- FFmpeg ベースの動画デコード
- FFmpeg ベースの音声デコードと `QAudioSink` による出力
- `VideoOutput` との連携: `videoSink: output.videoSink`
- 再生操作: `play()`, `pause()`, `stop()`, `seek(position)`, `seekToFrame(frame)`, `stepForward(frames)`, `stepBackward(frames)`
- フレーム/時刻変換: `positionForFrame(frame)`, `frameForPosition(position)`, `timecodeForFrame(frame)`, `timecodeForPosition(position)`
- ドラッグ中の軽量プレビュー用: `previewSeek(position)`, `previewSeekToFrame(frame)`
- プレビューキャッシュ制御: `previewCacheSize`, `previewCacheLimit`, `clearPreviewCache()`
- サムネイル生成: `requestThumbnail()`, `requestThumbnailForFrame()`, `thumbnailReady`
- タイムラインマーカー: `markers`, `addMarker()`, `removeMarker()`, `clearMarkers()`
- 範囲ループ: `loopStart`, `loopEnd`, `loops`
- ループ範囲ヘルパー: `setLoopRange()`, `setLoopRangeForFrames()`, `clearLoopRange()`
- 現在フレーム画像の取得: `captureFrame()`
- 専用のプレビューデコーダによるスクラブ操作
- キーフレームインデックスを使った高速プレビューシーク
- QML/C++ から参照できるメディア情報: `currentFrame`, `frameCount`, `videoSize`, `aspectRatio`, `frameRate`, `videoCodecName`, `audioCodecName`, `pixelFormat`, `audioFormat`, `seekable`, `hasAudio`, `audioChannelCount`, `audioSampleRate`, `hasVideo`
- UI 制御用の状態: `canPlay`, `canPause`, `canSeek`, `playbackState`, `error`
- 対応環境ではハードウェアデコードを利用
  - macOS: VideoToolbox
  - Windows: D3D11VA、失敗時 DXVA2
- CMake install/export 対応
- 最小構成の QML サンプルアプリ付き

## 必要環境

- Qt 6.10 以降
- CMake 3.21 以降
- 付属の CMake preset を使う場合は Ninja
- vcpkg preset を使う場合、FFmpeg 開発ライブラリは vcpkg manifest mode で自動導入

Qt は vcpkg manifest には含めていません。Qt 公式インストーラーや任意の方法で Qt を
インストールし、`CMAKE_PREFIX_PATH` で Qt のパスを指定してください。

## ビルド

### vcpkg manifest mode を使う場合

vcpkg をインストールし、`VCPKG_ROOT` を設定してから付属 preset で configure します。

```sh
cmake --preset vcpkg -DCMAKE_PREFIX_PATH=/path/to/Qt/6.10/<platform>
cmake --build --preset vcpkg
```

FFmpeg は `vcpkg.json` をもとに自動で導入されます。Windows や、ローカルに FFmpeg
開発環境を用意したくない場合はこちらを推奨します。

### システムに入っている FFmpeg を使う場合

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/<platform>
cmake --build build
```

macOS で Qt の標準インストール先を使う例:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=$HOME/Qt/6.10.2/macos
cmake --build build
```

Homebrew で FFmpeg を入れている場合は、通常 `pkg-config` で自動検出されます。
vcpkg や pkg-config を使わない場合は、`FFMPEG_ROOT` に FFmpeg の `include/` と
`lib/` を含むディレクトリを指定してください。

## サンプルアプリ

```sh
cmake --build build --target qmlscrubplayer_basic
./build/examples/basic/qmlscrubplayer_basic
```

## テスト

```sh
cmake --build build --target qmlscrubplayer_tests
ctest --test-dir build --output-on-failure
```

## QML での使い方

```qml
import QtQuick
import QtMultimedia
import QMLScrubPlayer

QmlScrubPlayer {
    id: player
    source: "file:///path/to/movie.mp4"
    autoPlay: true
    videoSink: output.videoSink
}

VideoOutput {
    id: output
    anchors.fill: parent
    fillMode: VideoOutput.PreserveAspectFit
}
```

シークバーを気持ちよく動かすには、ドラッグ中は `previewSeek(position)`、
ドラッグを離した時に `endPreviewSeek(position)` を呼びます。
フレーム番号ベースの UI では `previewSeekToFrame(frame)` と
`endPreviewSeekToFrame(frame)` も使えます。

```qml
Slider {
    from: 0
    to: player.duration
    value: player.position
    live: true
    onMoved: player.previewSeek(value)
    onPressedChanged: {
        if (!pressed) {
            player.endPreviewSeek(value)
        }
    }
}
```

## FetchContent で利用する

QMLScrubPlayer はまだ発展中のため、別の CMake プロジェクトから利用する場合は
FetchContent を正式な推奨ルートとしています。

```cmake
include(FetchContent)

FetchContent_Declare(
    QMLScrubPlayer
    GIT_REPOSITORY https://github.com/tuyudaku/qml-scrub-player.git
    GIT_TAG        v0.1.0 # リリースタグまたは固定コミットを推奨
)

FetchContent_MakeAvailable(QMLScrubPlayer)

target_link_libraries(my_app PRIVATE QMLScrubPlayer::QMLScrubPlayer)
```

Qt は `CMAKE_PREFIX_PATH` から見つかる必要があります。FFmpeg は pkg-config、
`FFMPEG_ROOT`、または CMake package 経由で見つかるようにしてください。
アプリケーション実行時に QML から `import QMLScrubPlayer` する場合は、生成済み
またはインストール済みの QML import ディレクトリが Qt から見える必要があります。

## インストール

```sh
cmake --install build --prefix /path/to/install
```

インストール先には C++ ライブラリ、公開ヘッダ、CMake package ファイル、
QML モジュールのメタデータ、プロジェクトドキュメントが含まれます。

利用側の CMake:

```cmake
find_package(QMLScrubPlayer CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE QMLScrubPlayer::QMLScrubPlayer)
```

実行時には、インストール先の QML import path がアプリから見える必要があります。
開発中は install prefix の QML ディレクトリを `QML_IMPORT_PATH` に追加すると扱いやすいです。

## C++ からの利用

`QmlScrubPlayer` は公開 C++ クラスとしても利用できます。直接インスタンス化して、
再生時間、現在位置、再生状態、エラーなどを取得できます。

```cpp
#include <qmlscrubplayer.h>

auto *player = new QmlScrubPlayer(this);
player->setSource(QUrl::fromLocalFile("/path/to/movie.mp4"));

connect(player, &QmlScrubPlayer::durationChanged, this, [player] {
    qDebug() << "duration ms:" << player->duration();
});

connect(player, &QmlScrubPlayer::positionChanged, this, [player] {
    qDebug() << "position ms:" << player->position();
});

connect(player, &QmlScrubPlayer::statusChanged, this, [player] {
    qDebug() << "status:" << static_cast<int>(player->status());
});

player->play();
```

C++ から参照できる主な状態は `source()`, `isPlaying()`, `duration()`,
`position()`, `remainingTime()`, `progress()`, `timecode()`, `durationTimecode()`, `currentFrame()`, `frameCount()`,
`videoSize()`, `aspectRatio()`, `frameRate()`, `videoCodecName()`, `audioCodecName()`,
`pixelFormat()`, `audioFormat()`, `isSeekable()`, `hasAudio()`, `audioChannelCount()`,
`audioSampleRate()`, `hasVideo()`, `canPlay()`, `canPause()`, `canSeek()`,
`volume()`, `isMuted()`, `playbackState()`, `status()`, `error()`, `errorString()` です。操作メソッドも QML と同じく
`play()`, `pause()`, `stop()`, `seek()`, `seekToFrame()`, `setLoopRange()`, `setLoopRangeForFrames()`, `clearLoopRange()`, `stepForward()`, `stepBackward()`,
`positionForFrame()`, `frameForPosition()`, `timecodeForFrame()`, `timecodeForPosition()`, `captureFrame()`, `previewSeek()`, `previewSeekToFrame()`,
`endPreviewSeek()`, `endPreviewSeekToFrame()` を利用できます。

## API 一覧

| 分類 | API |
| --- | --- |
| 再生 | `play()`, `pause()`, `stop()`, `playing`, `playbackState`, `canPlay`, `canPause` |
| タイムライン | `duration`, `position`, `remainingTime`, `progress`, `timecode`, `durationTimecode`, `seek()`, `canSeek`, `seekable` |
| フレーム | `currentFrame`, `frameCount`, `seekToFrame()`, `stepForward()`, `stepBackward()`, `positionForFrame()`, `frameForPosition()`, `timecodeForFrame()`, `timecodeForPosition()` |
| スクラブ | `previewSeek()`, `previewSeekToFrame()`, `endPreviewSeek()`, `endPreviewSeekToFrame()`, `previewCacheSize`, `previewCacheLimit`, `clearPreviewCache()` |
| サムネイル | `requestThumbnail()`, `requestThumbnailForFrame()`, `thumbnailReady` |
| マーカー | `markers`, `addMarker()`, `addMarkerForFrame()`, `removeMarker()`, `removeMarkerForFrame()`, `clearMarkers()` |
| メディア | `videoSize`, `aspectRatio`, `frameRate`, `videoCodecName`, `audioCodecName`, `pixelFormat`, `audioFormat`, `hasAudio`, `hasVideo` |
| ループ | `loops`, `loopStart`, `loopEnd`, `setLoopRange()`, `setLoopRangeForFrames()`, `clearLoopRange()` |
| キャプチャ | `captureFrame()` |
| エラー | `status`, `error`, `errorString`; `error` は open、stream-info、decoder、seek、read、decode、scaler、audio、media 失敗を区別 |

## 使い分けメモ

`previewCacheLimit` はスクラブプレビュー用フレームの保持数を制限します。UI 側で不要に
なった場合は `clearPreviewCache()` で明示的に破棄できます。`requestThumbnail()` と
`requestThumbnailForFrame()` は表示中の再生位置を変えずに画像を生成します。UI 側で
リクエストと結果を対応させたい場合は request id を渡し、省略した場合はプレイヤーが
自動採番します。

マーカーはタイムライン表示、レビュー箇所、戻りたい位置のための source 単位の
ミリ秒位置です。値はソート・重複排除され、新しい source を設定するとクリアされます。
`setLoopRange()` と `setLoopRangeForFrames()` はループ開始/終了を一度に更新し、
`clearLoopRange()` は範囲ループを無効化します。

## 補足

音声は FFmpeg でデコードし、libswresample で Qt 向けの PCM に変換して `QAudioSink`
から出力します。スクラブ中のプレビュー専用デコーダは映像のみを扱い、音声は通常再生側で
出力します。

## ライセンス

MIT

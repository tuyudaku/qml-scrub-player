# QMLScrubPlayer

QMLScrubPlayer is a small Qt 6.10 module for QML video playback. It decodes video
with FFmpeg and presents frames to Qt Quick through `QVideoSink`, so QML can keep
using `VideoOutput` while the C++ side owns low-latency seek and decode control.

## Features

- QML type: `QmlScrubPlayer`
- FFmpeg-backed video decoding
- FFmpeg-backed audio decoding with `QAudioSink` output
- `VideoOutput` integration through `player.videoSink`
- Playback controls: `play()`, `pause()`, `stop()`, `seek(position)`,
  `seekToFrame(frame)`, `stepForward(frames)`, `stepBackward(frames)`
- Frame/time conversion helpers: `positionForFrame(frame)`,
  `frameForPosition(position)`, `timecodeForFrame(frame)`,
  `timecodeForPosition(position)`
- Scrubbing control: `previewSeek(position)` and `previewSeekToFrame(frame)` for
  lightweight keyframe previews
- Preview cache controls: `previewCacheSize`, `previewCacheLimit`,
  `clearPreviewCache()`
- Thumbnail requests with `requestThumbnail()`, `requestThumbnailForFrame()`,
  and `thumbnailReady`
- Timeline markers with `markers`, `addMarker()`, `removeMarker()`, and
  `clearMarkers()`
- Range looping with `loopStart`, `loopEnd`, and `loops`
- Loop range helpers: `setLoopRange()`, `setLoopRangeForFrames()`, and
  `clearLoopRange()`
- Current-frame capture with `captureFrame()`
- Properties for `source`, `playing`, `duration`, `position`, `currentFrame`,
  `remainingTime`, `progress`, `frameCount`, `volume`, `muted`, `loops`,
  `playbackRate`, `timecode`, `durationTimecode`, `playbackState`, `status`,
  `error`, and `errorString`
- Media information available from QML/C++: `videoSize`, `aspectRatio`,
  `frameRate`, `videoCodecName`, `audioCodecName`, `pixelFormat`,
  `audioFormat`, `seekable`, `hasAudio`, `audioChannelCount`,
  `audioSampleRate`, `hasVideo`
- UI capability flags: `canPlay`, `canPause`, `canSeek`
- Decoder-thread seeking with `av_seek_frame()` and codec buffer flush
- Dedicated preview decoder for scrub-bar dragging
- Keyframe-index assisted preview seeking
- Hardware decode where FFmpeg supports it:
  - macOS: VideoToolbox
  - Windows: D3D11VA, with DXVA2 fallback
- CMake install/export support for GitHub distribution
- Minimal QML example app

## Requirements

- Qt 6.10 or newer
- CMake 3.21 or newer
- Ninja, when using the provided CMake preset
- FFmpeg development libraries, provided automatically by vcpkg manifest mode
  when using the vcpkg preset

Qt is intentionally not part of the vcpkg manifest. Install Qt with the Qt
installer or your preferred package manager, then pass its path through
`CMAKE_PREFIX_PATH`.

## Build

### With vcpkg manifest mode

Install vcpkg, set `VCPKG_ROOT`, and configure with the bundled preset:

```sh
cmake --preset vcpkg -DCMAKE_PREFIX_PATH=/path/to/Qt/6.10/<platform>
cmake --build --preset vcpkg
```

vcpkg will install FFmpeg from `vcpkg.json` automatically. This is the
recommended route for Windows and for users who do not already have FFmpeg
development libraries installed.

### With system FFmpeg

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/<platform>
cmake --build build
```

On macOS with a default Qt installer layout, for example:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=$HOME/Qt/6.10.2/macos
cmake --build build
```

If FFmpeg was installed through Homebrew, `pkg-config` should find it
automatically. If you are not using vcpkg or pkg-config, set `FFMPEG_ROOT` to a
directory that contains FFmpeg `include/` and `lib/`.

## Run The Example

```sh
cmake --build build --target qmlscrubplayer_basic
./build/examples/basic/qmlscrubplayer_basic
```

Run the generated `qmlscrubplayer_basic` executable from the corresponding build
directory.

## Run Tests

```sh
cmake --build build --target qmlscrubplayer_tests
ctest --test-dir build --output-on-failure
```

## Use From QML

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

For responsive scrub bars, call `previewSeek(position)` while dragging and
`endPreviewSeek(position)` when the user releases the handle. Frame-based UIs
can use `previewSeekToFrame(frame)` and `endPreviewSeekToFrame(frame)`.

## Use With FetchContent

FetchContent is the recommended way to consume QMLScrubPlayer from another CMake
project while the library is still evolving.

```cmake
include(FetchContent)

FetchContent_Declare(
    QMLScrubPlayer
    GIT_REPOSITORY https://github.com/tuyudaku/qml-scrub-player.git
    GIT_TAG        v0.1.0 # Prefer a release tag or pinned commit.
)

FetchContent_MakeAvailable(QMLScrubPlayer)

target_link_libraries(my_app PRIVATE QMLScrubPlayer::QMLScrubPlayer)
```

Qt must still be discoverable through `CMAKE_PREFIX_PATH`, and FFmpeg must be
discoverable through pkg-config, `FFMPEG_ROOT`, or another CMake package route.
If your application imports `QMLScrubPlayer` from QML at runtime, make sure the
generated or installed QML import directory is visible to Qt.

## Install

```sh
cmake --install build --prefix /path/to/install
```

The install tree includes the C++ library, public header, CMake package files,
QML module metadata, and project documentation under the install prefix.

Consumer projects can then use:

```cmake
find_package(QMLScrubPlayer CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE QMLScrubPlayer::QMLScrubPlayer)
```

Make sure the installed QML import path is available to your application at
runtime. During development, adding the install prefix's QML directory to
`QML_IMPORT_PATH` is usually enough.

## Use From C++

`QmlScrubPlayer` is also a public C++ class. You can create it directly, read
playback state, and connect to Qt signals.

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

Available C++ state includes `source()`, `isPlaying()`, `duration()`,
`position()`, `remainingTime()`, `progress()`, `timecode()`,
`durationTimecode()`, `currentFrame()`, `frameCount()`,
`videoSize()`, `aspectRatio()`, `frameRate()`, `videoCodecName()`,
`audioCodecName()`, `pixelFormat()`, `audioFormat()`, `isSeekable()`,
`hasAudio()`, `audioChannelCount()`, `audioSampleRate()`, `hasVideo()`,
`canPlay()`, `canPause()`, `canSeek()`, `volume()`, `isMuted()`,
`playbackState()`, `status()`, `error()`, and `errorString()`. The same control
methods used from QML are available from C++: `play()`, `pause()`, `stop()`,
`seek()`, `seekToFrame()`, `setLoopRange()`, `setLoopRangeForFrames()`,
`clearLoopRange()`, `stepForward()`, `stepBackward()`,
`positionForFrame()`, `frameForPosition()`, `timecodeForFrame()`,
`timecodeForPosition()`, `captureFrame()`, `previewSeek()`, `previewSeekToFrame()`,
`endPreviewSeek()`, and `endPreviewSeekToFrame()`.

## API Overview

| Area | API |
| --- | --- |
| Playback | `play()`, `pause()`, `stop()`, `playing`, `playbackState`, `canPlay`, `canPause` |
| Timeline | `duration`, `position`, `remainingTime`, `progress`, `timecode`, `durationTimecode`, `seek()`, `canSeek`, `seekable` |
| Frames | `currentFrame`, `frameCount`, `seekToFrame()`, `stepForward()`, `stepBackward()`, `positionForFrame()`, `frameForPosition()`, `timecodeForFrame()`, `timecodeForPosition()` |
| Scrubbing | `previewSeek()`, `previewSeekToFrame()`, `endPreviewSeek()`, `endPreviewSeekToFrame()`, `previewCacheSize`, `previewCacheLimit`, `clearPreviewCache()` |
| Thumbnails | `requestThumbnail()`, `requestThumbnailForFrame()`, `thumbnailReady` |
| Markers | `markers`, `addMarker()`, `addMarkerForFrame()`, `removeMarker()`, `removeMarkerForFrame()`, `clearMarkers()` |
| Media | `videoSize`, `aspectRatio`, `frameRate`, `videoCodecName`, `audioCodecName`, `pixelFormat`, `audioFormat`, `hasAudio`, `hasVideo` |
| Looping | `loops`, `loopStart`, `loopEnd`, `setLoopRange()`, `setLoopRangeForFrames()`, `clearLoopRange()` |
| Capture | `captureFrame()` |
| Errors | `status`, `error`, `errorString`; `error` distinguishes open, stream-info, decoder, seek, read, decode, scaler, audio, and media failures |

## Usage Notes

Use `previewCacheLimit` to cap how many scrub-preview frames are retained in
memory, and `clearPreviewCache()` when the surrounding UI no longer needs them.
`requestThumbnail()` and `requestThumbnailForFrame()` generate a preview image
without changing the visible playback position; pass a request id if the UI needs
to match responses to requests, or omit it to let the player assign one.

Markers are source-local millisecond positions intended for timeline UI, review
points, or quick return points. They are sorted, deduplicated, and cleared when a
new source is set. `setLoopRange()` and `setLoopRangeForFrames()` update the loop
start/end as one operation; `clearLoopRange()` disables the active range.

## Notes

QMLScrubPlayer decodes audio through FFmpeg, resamples it with libswresample, and
outputs it with `QAudioSink`. Audio is currently tied to normal playback; scrub
preview decoding remains video-only.

## Repository Layout

```text
src/              QMLScrubPlayer QML module
examples/basic/   Minimal QML video player example
cmake/            Package config template
```

## License

MIT

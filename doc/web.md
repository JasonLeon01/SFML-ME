# Building SFML for the browser

The Web platform uses Emscripten 7.0.0, WebGL 2, and static libraries. It supports
one SFML window on one canvas per page, the Graphics module, and the Audio module
with the browser's default playback and recording devices. The Network module is
not supported. No additional SFML API is required.

## Build and run

Install and activate Emscripten 7.0.0, then enter its environment (`emsdk_env.bat`
on Windows or `source emsdk_env.sh` on Linux/macOS). The `EMSDK` environment
variable must identify the SDK directory. CMake 3.28+, Ninja and Python are also
required. An existing Node installation can be selected through the SDK's
`NODE_JS` configuration; this does not require replacing the system Node.

```sh
cmake --preset web
cmake --build build/web --parallel
cmake --install build/web
npx --yes http-server build/web/bin -a 127.0.0.1 -p 8080 -c-1
```

Open `http://127.0.0.1:8080/tennis.html`. The example uses the existing tennis C++
source without a browser-specific main loop. Click the canvas to enable audio,
press Space to start, use the arrow keys to play, and Escape to close the SFML
window. The canvas is hidden on close. When tennis returns from `main()`, its Web
runtime shuts down and the page shows an exit message with a Restart button.
Closing the SFML window does not close the browser tab; library users can still
recreate a window on the same canvas. Tennis enables `EXIT_RUNTIME` only for its
own executable, without imposing application shutdown on other SFML consumers.

Use `-DCMAKE_BUILD_TYPE=Debug` for a debug build (`tennis-d.html`). Only the tennis example is built
for Web; examples requiring terminal input, extra windows, or networking are not
included. The existing all-module Catch2 suite remains a native test suite.

## Applications and resources

Compile the application with the same Emscripten SDK. For an installed SFML, set
`SFML_STATIC_LIBRARIES=ON` and `SFML_DIR` to the installation's `lib/cmake/SFML`
directory, then link the usual `SFML::Graphics` and `SFML::Audio` targets.
The targets propagate WebGL 2, Asyncify, exception handling and memory-growth
requirements. Do not override these with incompatible application linker flags.

The host page supplies `Module.canvas`, defaulting to an element with ID `canvas`.
See `examples/web/shell.html` for a minimal host. `sf::WindowHandle` is the CSS
selector of this canvas. Another canvas or a second active SFML window is rejected.
Textures may be loaded before opening the window, and resources survive closing
and reopening that window. Multiple `sf::RenderTexture` objects are supported.

Use Emscripten's `--preload-file path@virtual-path` on the application's final
link to package images, fonts and audio. Files are loaded before `main()`, so the
existing SFML file-loading APIs work unchanged. Memory-loading APIs and C/C++
file writes operate on the virtual filesystem. Its contents are lost on reload.
Deploy the HTML, JavaScript, Wasm and data files together through HTTP(S), serving
Wasm as `application/wasm`; opening the HTML as a local file is not supported.

Cookies, localStorage and sessionStorage belong to the host page. Use their normal
browser APIs there, and use Emscripten's existing JavaScript/C++ interop or `FS`
operations when the application needs those values. SFML does not map these stores
to files or automatically persist virtual files. Browser storage availability and
origin rules still apply. Host resource downloads are independent of SFML Network.

## Runtime behavior

- Keep the normal `while (window.isOpen())` loop. `display()`, `waitEvent()` and
  `sf::sleep()` yield through Asyncify so the browser can render and dispatch
  events. A loop that never reaches any yielding operation will still block the
  page. This build runs on the browser main thread and does not require pthreads,
  SharedArrayBuffer, or cross-origin isolation headers.
- Vsync uses browser animation frames. Disabling vsync still yields on display;
  it cannot make the browser present faster than its own compositor. Frame limits
  account for time already spent waiting. Background tabs are subject to browser
  throttling. Do not invoke a yielding SFML operation from an audio callback or
  reenter it from a JavaScript callback while an Asyncify wait is pending.
- Window sizes and mouse coordinates use drawing-buffer pixels. CSS size and
  device-pixel ratio changes update the buffer and produce resize events.
  Keyboard/scancode mappings cover common desktop keys; layout discovery and IME
  composition are not provided. Focus loss clears pressed-key/button state.
- Fullscreen and pointer lock are browser-controlled and may be deferred until a
  user gesture. Pointer locking does not provide desktop cursor warping. Native
  window positioning, icons, custom pixel cursors, touch, sensors and joysticks
  are not implemented in this first desktop Web version. Clipboard access uses
  browser permissions; denial returns an empty string or leaves the clipboard
  unchanged.
- WebGL 1 and geometry shaders are not supported. GLSL shaders must target GLES3 /
  WebGL 2. Actual depth, stencil and antialiasing settings are reported by the
  context; the browser controls the default framebuffer. A lost context stops
  drawing and queues the existing `Closed` event. Reload the page to recover GPU
  resources; transparent context restoration is not implemented.
- Audio is unlocked by user interaction. Sound, Music, SoundStream, Listener,
  effects and the existing audio codecs use the existing mixer and codecs.
  This main-thread implementation uses miniaudio's ScriptProcessorNode path;
  browsers may report a deprecation warning. Long uninterrupted C++ work can
  interrupt playback, so continue yielding regularly.
- Recording uses the default microphone and requires localhost or HTTPS plus
  browser permission. `SoundRecorder::start()` waits for the permission decision
  while allowing browser events to run, and returns false on denial/failure.
  Stop/destruction releases microphone tracks. Only default input/output devices
  are exposed; unknown device selections fail. Null playback remains available
  through a silent Web Audio output, with the same browser activation rules.

## Formatting and analysis

Use clang-format **17**, as required by `cmake/Format.cmake`. Web analysis uses
clang-tidy **21.1.6**, which can parse the SDK's libc++ headers. Supply its path
with `-DCLANG_TIDY_EXECUTABLE=...` and make `run-clang-tidy` available on PATH.
The Web CI job passes `CLANG_TIDY_CHECKS` filters for four compatibility checks:
`portability-avoid-pragma-once`, `cppcoreguidelines-use-enum-class`,
`bugprone-dynamic-static-initializers`, and
`readability-use-concise-preprocessor-directives`. These preserve the project's
existing header conventions and public API under Clang 21. Other checks and
warnings-as-errors remain enabled. The option defaults to empty, so native jobs
continue using the shared `.clang-tidy` configuration unchanged.

```sh
cmake --build build/web --target format
cmake --preset web -DCMAKE_UNITY_BUILD=ON -DCLANG_TIDY_EXECUTABLE=/path/to/clang-tidy
cmake --build build/web --target tidy
```

As in the native analyze jobs, analysis uses a Unity build. The tidy adapter supplies the Wasm target, SDK sysroot and compatibility include
paths in a separate compilation database. It retains the project's checks and
warnings-as-errors settings and analyzes the Web implementation. JavaScript bodies
inside Emscripten macros are locally excluded from C++ formatting to preserve their
syntax. Existing native build, format and analyze jobs remain in place.

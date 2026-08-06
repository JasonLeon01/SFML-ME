# OpenHarmony and HarmonyOS mobile backend

This SFML 3.1.0-ME backend targets OpenHarmony/HarmonyOS API 21 on phone and tablet. It is a distinct SFML platform and does not depend on the product's kernel identity: some OpenHarmony profiles use a Linux kernel, while HarmonyOS products may use another OHOS kernel. Platform-specific code must test `SFML_SYSTEM_HARMONY` before any Linux selection.

## Build contract

The DevEco OHOS CMake toolchain defines `__OHOS__`. SFML then exposes:

- `SFML_SYSTEM_HARMONY` for the base platform;
- `SFML_HARMONY_MOBILE` for the implemented phone/tablet device form;
- `SFML_HARMONY_2IN1` as a reserved name which is not defined by this backend.

The corresponding CMake selection is `SFML_OS_HARMONY` with `SFML_HARMONY_DEVICE_FORM=MOBILE`. Selecting `2IN1` is a configure-time error. The native SDK minimum is an internal API 21 build contract rather than a configurable `SFML_HARMONY_API_LEVEL`; HAP target and compatible API levels remain packaging metadata. The official DevEco toolchain selects and validates `OHOS_ARCH`, and SFML requires its `c++_shared` runtime. The backend accepts the toolchain's `arm64-v8a`, `armeabi-v7a`, and `x86_64` names, but the consumer-device example HAP packages only `arm64-v8a`. `x86_64` is an optional emulator/toolchain cross-build, and DevEco 6.0.1's HarmonyOS Stage packager rejects `armeabi-v7a`, which can only be validated by a separate OpenHarmony NDK build. Native code uses position-independent code because static SFML libraries and dependencies are normally folded into one HAP NAPI shared object.

A standalone install follows SFML's Android-style ABI layout: libraries and the package configuration are placed below `lib/<OHOS_ARCH>`. Point consumers at the exact package directory with `-DSFML_DIR=<install-prefix>/lib/<OHOS_ARCH>/cmake/SFML`; for example, an arm64 consumer uses `lib/arm64-v8a/cmake/SFML`. This keeps multiple installed ABIs separate while preserving relocatable exported targets.

SFML does not pretend to be desktop Linux. POSIX implementation files are reused selectively for threads, monotonic time, the filesystem, dynamic loading, and BSD sockets. Bundled third-party libraries receive only the musl/POSIX feature definitions they require. ALSA, PulseAudio, JACK, X11, Wayland, GLX, UDev, GLES1, and desktop OpenGL are not backend dependencies.

## Stage host and entry point

Include `SFML/Main.hpp` in the native application. On Harmony it maps the user's `main` to `sfmlMain`. Link `SFML::Main`; the source-side Stage glue shown in `examples/harmony/entry/src/main/cpp/napi_init.cpp` connects the NAPI module to SFML without adding Harmony-only public headers.

The exported Stage host functions are `initializeHost`, `onForeground`, `onBackground`, `onXComponentDestroy`, `onDestroy`, `submitText`, `submitKeyText`, and `submitTextEdit`. ArkTS calls `initializeHost(resourceManager, windowId, deviceType, callbacks)` only after the Stage window ID is greater than zero. The callback object must provide `setPointerVisible(boolean)`, `setSystemPointer(number)`, `setCustomPointer(Uint8Array, width, height, hotspotX, hotspotY)`, `setVirtualKeyboardVisible(boolean)`, `configureWindow(fullscreen, keepScreenOn)`, and `requestExit()`; native worker threads reach these functions through NAPI thread-safe functions, and ArkTS marshals the operations to InputKit/ArkUI. Because XComponent `onLoad` can precede asynchronous Stage-window discovery, hosts must retain the native context and retry initialization when the window ID arrives. The native XComponent is unwrapped from `OH_NATIVE_XCOMPONENT_OBJ`; its surface and input callbacks are registered internally. The application worker starts exactly once when both the host and a non-zero native surface are ready.

Only one surface-owning `sf::Window` or `sf::RenderWindow` is allowed in a process. A second native window fails deterministically. `sf::WindowHandle` is `void*` and denotes the current `OHNativeWindow*` belonging to the XComponent.

Surface loss destroys only the EGLSurface. The EGLContext and shared objects remain alive until application teardown; a later surface callback rebuilds the EGLSurface and queues resize/focus events. Duplicate and out-of-order Stage/XComponent lifecycle callbacks are idempotent.

## Graphics contract

Harmony mobile always uses EGL with the OpenGL ES 2.0 API surface and ESSL 1.00. The native link dependencies are `libEGL.so` and `libGLESv2.so`; neither GLES1 nor GLES3 is linked. Requests above 2.0 in `sf::ContextSettings` are normalized to an ES2 client request, while `getSettings()` reports the context version actually returned by the driver. A backward-compatible ES 3.2 driver may therefore be reported as 3.2 even though SFML exposes only GLES2 headers, imports, and rendering paths. Raw GL code should resolve entry points with `sf::Context::getFunction`, including GLES2 core functions, so that calls use the same vendor dispatch selected by EGL on Harmony devices.

OpenGL ES 3.2 is backward compatible with the GLES2 programmable pipeline, but it does not restore GLES1 fixed-function entry points such as `glMatrixMode`. Applications must use Shader-based rendering. SFML users are not given a raw GLES3 context in this first backend.

The inherited 3.1.0-ME Shader contract uses attributes `sf_Vertex`, `sf_Color`, and `sf_MultiTexCoord0`; matrices `sf_ModelViewMatrix`, `sf_ProjectionMatrix`, `sf_ModelViewProjectionMatrix`, and `sf_TextureMatrix`; varyings `sf_FrontColor` and `sf_TexCoord0`; and uniforms `sf_Texture` and `sf_TextureEnabled`. Harmony vertex stages use high precision. Attribute locations 0, 1, and 2 are used for position, color, and texture coordinates.

Window multisampling follows the available EGL configurations. RenderTexture supports color, depth, stencil, separate depth/stencil, FBO readback, and copy-to-image, but multisampled RenderTexture is intentionally rejected and `getMaximumAntiAliasingLevel()` returns zero. sRGB textures and window surfaces are enabled only when the required GLES/EGL extensions are present; unsupported requests fall back to linear storage and report the actual setting.

## Mobile window behavior

The XComponent determines the window size. Setting the title, position, size, minimum/maximum size, or mouse position is a no-op. Fullscreen means occupying the primary Stage surface. `VideoMode::getDesktopMode()` reads the current surface size, while the single entry returned by `VideoMode::getFullscreenModes()` reflects the current surface at each query. Harmony does not synthesize a second, rotated mode, an early pre-surface query does not permanently cache the temporary 1x1 fallback, and later queries do not mutate mode arrays returned earlier. Additional windows are unsupported.

Input events cross a thread-safe queue. The backend maps touch and multitouch, pointer/mouse buttons and axes, keyboard, focus, and committed Unicode text. IME composition remains platform-owned. The native OHInputMethod editor is authoritative when attachment succeeds. Only when it is unavailable does the ArkTS TextInput fallback compare its previous committed value with each change and call `submitTextEdit(backwardDeletions, forwardDeletions, insertedText)`. Native attachment and fallback submission are mutually excluded, and each fallback delete/replace transaction enters the event queue under one lock, so composition updates do not leak and committed text is not duplicated. Clipboard uses the platform pasteboard APIs. Pointer visibility, system styles, custom RGBA cursors, and virtual keyboard requests use host callbacks because they must be marshalled to ArkUI/InputKit. Programmatic pointer-position injection is unsupported.

Sensors use the native sensor service. Game controllers use the API 21 game-controller service when available. Vulkan graphics uses `VK_OHOS_surface` with the same `OHNativeWindow*`.

## Audio, networking, and resources

Miniaudio remains SFML's decoder, mixer, and spatial-audio engine but is built with device I/O and desktop Linux backends disabled. OHAudio render callbacks pull PCM from the no-device engine. Harmony's media/game output API follows the system-selected route and does not allow an application to select arbitrary output hardware, so `PlaybackDevice::getAvailableDevices()` exposes that current route as its selectable entry and route changes are reported through the normal notification callback. Recording uses an OHAudio capturer; its device list contains only API 21 media-input descriptors accepted by `SoundRecorder::setDevice()`. The realtime capture callback only transfers data to a preallocated ring buffer and a worker invokes `onProcessSamples` outside the callback.

Networking uses BSD sockets for IPv4/IPv6, DNS, TCP, UDP, nonblocking sockets, and selectors. TLS and SSH use the bundled MbedTLS and libssh2 builds.

`rawfile:/path` explicitly opens a HAP rawfile through the resource manager. Other paths first address the application sandbox. If opening a relative sandbox path fails, SFML retries it as a rawfile. Rawfile access fails with a clear diagnostic until ArkTS has registered a native resource manager.

## Deferred scope

2in1/PC support, desktop OpenGL, multiple windows, GLES1 emulation, raw GLES3 contexts, and RenderTexture ES2 multisampling are not part of this mobile backend.

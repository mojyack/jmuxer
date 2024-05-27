# jmuxer
Jitsi Meet multiplexer
# Dependencies
* gstjitsimeet(https://github.com/mojyack/gstjitsimeet)
# Build
```
git clone --recursive https://github.com/mojyack/jmuxer.git
meson setup build --buildtype=release
ninja -C build
```
# Example
```
export GST_PLUGIN_PATH=$PATH/TO/GSTJITSIMEET/BUILDDIR
build/jmuxer -i jitsi.example/src/receiver -o jitsi.example/sink/sender
```

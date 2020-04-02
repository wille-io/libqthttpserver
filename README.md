# libqthttpserver
The shared library version of Qt Lab's qthttpserver module. Uses CMake. Just make &amp; make install to link with your project. So you don't need to compile your own Qt version to work with qthttpserver.
Forked from: [qthttpserver](https://github.com/qt-labs/qthttpserver)

Contains my writeReady fix from [here](https://codereview.qt-project.org/c/qt-labs/qthttpserver/+/292953).

## Needed libraries
[http_parser](https://github.com/nodejs/http-parser)

## Downsides
There is a reason that qthttpserver comes as a Qt module that you have to compile together with Qt itself: compatibility.
Fortunately, some workarounds were added that should not affect functionality.
Unfortunately, older Qt versions don't support `makeIndexSequence` for example, so routing is disabled for e.g. Ubuntu 18.04, until someone provides a good workaround for `makeIndexSequence`.

## Tested with
- Ubuntu 18.04 (no routing available)
- Fedora 31 (no workarounds needed)

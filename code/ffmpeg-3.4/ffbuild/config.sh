# Automatically generated by configure - do not modify!
shared=yes
build_suffix=
prefix=./mac
libdir=${prefix}/lib
incdir=${prefix}/include
rpath=
source_path=.
LIBPREF=lib
LIBSUF=.a

extralibs_avutil=" -lm"
extralibs_avcodec="-framework OpenGL -framework OpenGL -framework Foundation -framework CoreVideo -framework CoreMedia -L/usr/local/lib -lSDL2 -framework CoreFoundation -framework VideoToolbox -framework CoreMedia -framework CoreVideo -framework VideoDecodeAcceleration -L/usr/local/lib -lSDL2 -liconv -Wl,-framework,CoreFoundation -Wl,-framework,Security -L/usr/local/lib -lSDL2 -lm  -llzma -lbz2 -lz -pthread -pthread -framework CoreServices -framework CoreGraphics -framework VideoToolbox -framework CoreImage -framework AVFoundation -framework AudioToolbox -framework AppKit "
extralibs_avformat="-framework OpenGL -framework OpenGL -framework Foundation -framework CoreVideo -framework CoreMedia -L/usr/local/lib -lSDL2 -framework CoreFoundation -framework VideoToolbox -framework CoreMedia -framework CoreVideo -framework VideoDecodeAcceleration -L/usr/local/lib -lSDL2 -liconv -Wl,-framework,CoreFoundation -Wl,-framework,Security -L/usr/local/lib -lSDL2 -lm  -llzma -lbz2 -lz -pthread -pthread -framework CoreServices -framework CoreGraphics -framework VideoToolbox -framework CoreImage -framework AVFoundation -framework AudioToolbox -framework AppKit "
extralibs_avdevice="-framework OpenGL -framework OpenGL -framework Foundation -framework CoreVideo -framework CoreMedia -L/usr/local/lib -lSDL2 -framework CoreFoundation -framework VideoToolbox -framework CoreMedia -framework CoreVideo -framework VideoDecodeAcceleration -L/usr/local/lib -lSDL2 -liconv -Wl,-framework,CoreFoundation -Wl,-framework,Security -L/usr/local/lib -lSDL2 -lm  -llzma -lbz2 -lz -pthread -pthread -framework CoreServices -framework CoreGraphics -framework VideoToolbox -framework CoreImage -framework AVFoundation -framework AudioToolbox -framework AppKit "
extralibs_avfilter="-framework OpenGL -framework OpenGL -framework Foundation -framework CoreVideo -framework CoreMedia -L/usr/local/lib -lSDL2 -framework CoreFoundation -framework VideoToolbox -framework CoreMedia -framework CoreVideo -framework VideoDecodeAcceleration -L/usr/local/lib -lSDL2 -liconv -Wl,-framework,CoreFoundation -Wl,-framework,Security -L/usr/local/lib -lSDL2 -lm  -llzma -lbz2 -lz -pthread -pthread -framework CoreServices -framework CoreGraphics -framework VideoToolbox -framework CoreImage -framework AVFoundation -framework AudioToolbox -framework AppKit "
extralibs_avresample="-lm"
extralibs_postproc=""
extralibs_swscale="-lm"
extralibs_swresample="-lm "
avcodec_deps="swresample avutil"
avdevice_deps="avfilter swscale avformat avcodec swresample avutil"
avfilter_deps="swscale avformat avcodec swresample avutil"
avformat_deps="avcodec swresample avutil"
avresample_deps="avutil"
avutil_deps=""
postproc_deps="avutil"
swresample_deps="avutil"
swscale_deps="avutil"

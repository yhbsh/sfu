default: x264 x265 avf_x264 avf_x265

x265: x265.c common.h
	clang -O3 -o x265 x265.c -lx265 -lc++

x264: x264.c common.h
	clang -O3 -o x264 x264.c -lswscale -lavutil -lx264 -framework AVFoundation -framework Foundation -framework CoreVideo -framework CoreMedia -framework CoreGraphics

avf_x264: avf_x264.m common.h
	clang -O3 -o avf_x264 avf_x264.m -lswscale -lavutil -lx264 -framework AVFoundation -framework Foundation -framework CoreVideo -framework CoreMedia -framework CoreGraphics

avf_x265: avf_x265.m common.h
	clang -O3 -o avf_x265 avf_x265.m -lc++ -lswscale -lavutil -lx265 -framework AVFoundation -framework Foundation -framework CoreVideo -framework CoreMedia -framework CoreGraphics

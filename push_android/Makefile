SDK = $(HOME)/Library/Android/sdk
NDK = $(SDK)/ndk/29.0.14033849
JAR = $(SDK)/platforms/android-36/android.jar
BUILD = build

LOCAL_ANDROID := $(HOME)/.local/android

STATIC_LIBS_arm64_v8a := $(LOCAL_ANDROID)/arm64-v8a/lib/libavutil.a \
                         $(LOCAL_ANDROID)/arm64-v8a/lib/libx264.a \
                         $(LOCAL_ANDROID)/arm64-v8a/lib/libx265.a \
                         $(LOCAL_ANDROID)/arm64-v8a/lib/libyuv.a \

CFLAGS = -shared -fPIC -O3 -I$(HOME)/.local/android/arm64-v8a/include
LDFLAGS = -landroid -llog -lm -lmediandk -lcamera2ndk -lc++_shared -lglesv3 -legl

JAVA_SRC := $(wildcard *.java)
CLASS_DIR := $(BUILD)/com/livsho/android

all: $(BUILD)/app.apk

$(BUILD):
	mkdir -p $(BUILD)/lib/arm64-v8a

CXX_SHARED := $(NDK)/toolchains/llvm/prebuilt/darwin-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so

$(BUILD)/classes.dex: $(JAVA_SRC) | $(BUILD)
	javac -O -cp $(JAR) -d $(BUILD) $(JAVA_SRC)
	d8 --lib $(JAR) --release $(CLASS_DIR)/*.class --output $(BUILD)

$(BUILD)/lib/arm64-v8a/libengine.so: main.c acm.h renderer.h ~/.local/include/rtp.h | $(BUILD)
	$(NDK)/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android28-clang $(CFLAGS) $(LDFLAGS) -o $@ main.c $(STATIC_LIBS_arm64_v8a)

$(BUILD)/app.unsigned.apk: $(BUILD)/classes.dex $(BUILD)/lib/arm64-v8a/libengine.so
	cp -v $(CXX_SHARED) $(BUILD)/lib/arm64-v8a
	aapt package -f -M AndroidManifest.xml -I $(JAR) -F $(BUILD)/app.unsigned.apk
	cd build && aapt add app.unsigned.apk classes.dex >& /dev/null && cd ..
	cd build && aapt add app.unsigned.apk lib/arm64-v8a/libengine.so >& /dev/null && cd ..
	cd build && aapt add app.unsigned.apk lib/arm64-v8a/libc++_shared.so >& /dev/null && cd ..

$(BUILD)/app.apk: $(BUILD)/app.unsigned.apk
	apksigner sign --ks ~/.gradle/debug.keystore --ks-key-alias androiddebugkey --ks-pass pass:android --out $(BUILD)/app.apk $(BUILD)/app.unsigned.apk

install: $(BUILD)/app.apk
	@adb install -r $(BUILD)/app.apk > /dev/null

launch: install
	@adb shell am start -n "com.livsho.android/.App" > /dev/null

clean:
	rm -rf $(BUILD)

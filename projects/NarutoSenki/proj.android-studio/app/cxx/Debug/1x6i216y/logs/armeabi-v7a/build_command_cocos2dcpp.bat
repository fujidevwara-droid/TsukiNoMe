@echo off
"C:\\Android\\android-ndk-r21e\\ndk-build.cmd" ^
  "NDK_PROJECT_PATH=null" ^
  "APP_BUILD_SCRIPT=C:\\Users\\pk\\Downloads\\UMNSA\\projects\\NarutoSenki\\proj.android-studio\\app\\jni\\Android.mk" ^
  "NDK_APPLICATION_MK=C:\\Users\\pk\\Downloads\\UMNSA\\projects\\NarutoSenki\\proj.android-studio\\app\\jni\\Application.mk" ^
  "APP_ABI=armeabi-v7a" ^
  "NDK_ALL_ABIS=armeabi-v7a" ^
  "NDK_DEBUG=1" ^
  "APP_PLATFORM=android-21" ^
  "NDK_OUT=C:\\Users\\pk\\Downloads\\UMNSA\\projects\\NarutoSenki\\proj.android-studio\\app\\build\\intermediates\\cxx\\Debug\\1x6i216y/obj" ^
  "NDK_LIBS_OUT=C:\\Users\\pk\\Downloads\\UMNSA\\projects\\NarutoSenki\\proj.android-studio\\app\\build\\intermediates\\cxx\\Debug\\1x6i216y/lib" ^
  -j8 ^
  "NDK_MODULE_PATH=C:\\Users\\pk\\Downloads\\UMNSA;C:\\Users\\pk\\Downloads\\UMNSA/cocos2dx/platform/third_party/android/prebuilt;C:\\Users\\pk\\Downloads\\UMNSA/external" ^
  cocos2dcpp

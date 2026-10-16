# Android 平台边界

本目录及 `java/` 内的 Android 适配代码受 MPL-2.0 约束。核心 CMake、JNI 实现、`MainActivity` 和 `NativeBridge` 的修改均保留在公开的 `master_android` 分支，不放入商业私有仓。

产品主仓通过 Git submodule 固定本仓提交：

```text
product/
  app/src/main/java/.../ProductActivity.java  # 独立产品子类与商业 SDK
  third_party/mgba/                         # 本仓库
```

Android Gradle 模块集成方式（相对于产品根目录）：

```groovy
android {
    sourceSets.main.java.srcDir rootProject.file("third_party/mgba/src/platform/android/java")
    externalNativeBuild.cmake.path rootProject.file("third_party/mgba/CMakeLists.txt")
    defaultConfig.externalNativeBuild.cmake.arguments "-DBUILD_ANDROID=ON", "-DLIBMGBA_ONLY=ON"
}
```

完整构建还需按目标关闭桌面前端和未使用的可选依赖，使用 NDK toolchain。本目录不依赖商业仓即可编译原生库：

```text
cmake -S . -B build-android -DCMAKE_TOOLCHAIN_FILE=<NDK>/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DBUILD_ANDROID=ON -DLIBMGBA_ONLY=ON -DBUILD_SHARED=OFF -DBUILD_STATIC=ON -DBUILD_QT=OFF -DBUILD_SDL=OFF -DUSE_FFMPEG=OFF -DUSE_LIBZIP=OFF -DUSE_PNG=OFF
cmake --build build-android --target mgba-android
```

`MainActivity` 是可继承的公开 Android 前端，不导入私有产品类或 SDK。产品 Activity 可在独立文件中扩展生命周期；若修改这里已有文件，仍按 MPL 提供修改后的文件源码。APK 必须包含 MPL 文本和对应源码获取说明。仓库边界本身不改变文件许可证。

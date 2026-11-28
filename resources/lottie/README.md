# Lottie 动画素材目录

把这个目录当作 Lottie 的“投放口”：美术给的 `.json` 动画（Lottie / Bodymovin 导出）直接放进来即可。

## 怎么用

1. 编译时打开 rlottie 支持（默认关闭）：

   ```bat
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
         -DCMAKE_PREFIX_PATH=C:\Qt\6.7.2\msvc2019_64 ^
         -DCUTECAT_WITH_RLOTTIE=ON
   ```

   需要先把 rlottie 编译好，并让 `find_package(rlottie)` 能找到它。

2. 把动画文件放进本目录，例如 `resources/lottie/idle.json`，然后在
   `CMakeLists.txt` 的 `qt_add_resources` 里把它加进资源列表，
   或直接改 `%APPDATA%\CuteCat\config.json`：

   ```json
   {
     "appearance": {
       "animation": "lottie",
       "lottie": "C:/路径/idle.json"
     }
   }
   ```

3. 托盘菜单 →「重新加载形象」即可生效。

## 当前状态

默认构建不带 rlottie，`appearance.animation` 保持 `image`，
桌宠使用 `resources/pet/cat_idle.png`（默认形象）+ 程序化呼吸/点击反应。
Lottie 加载失败时也会自动退回这套图片皮肤，不会启动不了。
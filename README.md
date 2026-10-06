# TTPlayer ENC

独立构建的 `ttp_enc.dll`，实现千千静听的 MP3、WMA 和可选 mp3PRO 编码插件接口。项目结构、日期版本、Release 构建和 Actions 与 `ttp_aac`、`ttp_flac`、`ttp_ogg`、`ttp_webm` 保持一致，不需要同时构建播放器或其它插件。

## 编码器

| 输出 | 实现 | 模式 |
| --- | --- | --- |
| MP3 | 静态链接 LAME **4.0** | CBR、ABR、V0–V9，以及原有八个预设 |
| WMA | 动态加载系统 `wmvcore.dll` | CBR、两遍比特率 VBR、质量 VBR、16 位无损 |
| mp3PRO | 用户提供的 Cool Edit Pro `fhtpro.flt` | 保留滤镜的编码选项窗口，以本地文件保存配置 |

截至 2026-10-06，LAME 官方公布的最新版本为 [4.0](https://lame.sourceforge.io/download.php)。构建固定下载此版本并校验 SHA-256，不跟随移动的 latest 链接。WMA 使用当前系统实际安装的编码器；mp3PRO 继续使用用户已有的专有滤镜，项目未重建或捆绑其算法内核。

## 默认配置与扩展选项

无已有配置时，MP3 默认为 **CBR 192 Kbps、联合立体声**；WMA 默认为 **质量 VBR 75**。保留原版字体、独立参数下拉框和原有参数顺序；MP3 的 VBR 分为“原版预设”（八个旧预设）和“质量等级”（V0–V9）两个独立单选项及下拉框，WMA 的 CBR、两遍比特率 VBR、质量 VBR 和 16 位无损在同一窗口选择。已移除独立“扩展…”窗口，已有有效配置保留。

MP3 保持指定输出采样率。WMA 只接受系统编码器实际支持的参数组合，不会自动替换成近似采样率或低比特率；不支持时需调整播放器转换参数。例如本次 XP/Win7 测试中，质量 75 支持 44.1 kHz，不支持 48 kHz。

MP3/mp3PRO 在播放器中优先使用宿主标准标签接口；独立调用且没有该接口时回退为 ID3v2.3。原版播放器中的 ID3v2、ID3v1＋APEv2 写入策略已经实际验证。

## 安装与运行

关闭播放器，将发行包中的 `AddIn/ttp_enc.dll` 放入播放器的 `AddIn` 目录，替换前自行保留原文件备份。

- MP3 编码只需要本项目 DLL，不需要外置 `lame_enc.dll`。
- 使用 mp3PRO 时，把 **`fhtpro.flt` 放在同一个 AddIn 目录**。无需导入 `.reg`，无需 `flt.dat`，无需安装 Cool Edit Pro。`mp3PRO.dll` 是另一种解码插件，不能代替编码滤镜。
- 缺少 `fhtpro.flt` 时只枚举 MP3 和 WMA；缺少系统 WMA 组件时 WMA 初始化返回错误，MP3 仍可使用。
- 同一 x86 DLL 面向 XP SP3、Windows 7、Windows 10/11；CPU 需要 SSE2。VC-LTL 和 YY-Thunks 在构建时接入，不需要用户另装 VC++ 2012/现代 VC++ 运行库。WMA 需要系统已有 Windows Media 组件。

配置保存在 DLL 同目录的 `ttp_enc.ini` 与 `coolmp3.ini`。目录不可写时使用 `%APPDATA%\TTPlayer\ttp_enc`，不请求管理员权限。不要把运行时配置文件加入发行包。

## 独立构建

需要 Visual Studio 2026 C++ 工具链、Windows SDK、CMake 3.24+、Python 3 和 PowerShell。Python 只用于构建后的 PE 导入检查，不是运行依赖。

```powershell
cd ttp_enc
./build.ps1 -Package
```

默认版本为北京时间 `yyyy.MM.dd`，同日发行后续版本为 `yyyy.MM.ddpN`。需要指定版本时：

```powershell
./build.ps1 -Package -PackageVersion 2026.10.06p1
```

输出：

```text
build/Release/ttp_enc.dll
build/Release/ttp_enc-yyyy.MM.dd[pN].zip
build/Release/SHA256SUMS.txt
```

ZIP 仅含 `AddIn/ttp_enc.dll` 和内部 `SHA256SUMS.txt`；外部校验文件校验 ZIP。作者为 Sonic853，DLL 文件版本、产品版本和包版本同步。LAME 许可证与使用声明同时嵌入 DLL 的 RCDATA 201/202，源码仓库保留完整声明。

Actions 通过 `workflow_dispatch` 手动触发。勾选 **Release a Version (GitHub)** 后，在编译前分配日期版本并发布 GitHub Release；不勾选则只上传构建产物。该工作流只构建 `ttp_enc`，不构建其它插件，不运行或上传私有测试。

## 精度与兼容范围

- ABI、工厂顺序、扩展名、原有配置键和编码调用顺序根据原版伪代码及二进制恢复。
- 支持单／双声道 PCM 8/16/24/32 位和 float32/64 输入。当前公共编码路径转换为 **16 位 PCM**；WMA 无损是对此 16 位数据无损，不保留 24/32 位源文件的全部精度。
- 新 LAME 的压缩结果与旧版内置 LAME 不保证逐字节相同。
- 两遍 WMA 用临时文件保存 PCM 分块，内存占用不随歌曲时长增加。第二遍在结束编码时完成，因此该阶段会继续占用时间和临时磁盘空间。
- mp3PRO 滤镜具有全局状态，同一进程同时只能启动一个该滤镜的编码会话；其它请求返回忙碌错误。MP3/WMA 不受此限制。
- 滤镜支持的采样率及编码组合受其原有实现限制。建议通过播放器转换设置使用 44.1/48 kHz；默认立体声 mp3PRO 为 96 Kbps，单声道适配为 48 Kbps，避免旧滤镜悄悄退回普通 MP3。

详见 [本次原版行为修复](docs/PARITY_FIXES.md)、[恢复设计](docs/RECONSTRUCTION.md)、[验证记录](docs/VALIDATION.md) 和 [第三方来源](third_party/ORIGIN.md)。

## 源码与重链接

本项目自有适配代码采用 [MIT](LICENSE)；静态链接的 LAME 采用其 [LGPL 许可证](third_party/lame/COPYING)。完整适配源码与 CMake 脚本保留在仓库，构建时下载未修改的固定版 LAME 源码。可用以下方式替换 LAME 源码并重新生成完整 DLL：

```powershell
./build.ps1 -BuildDirectory build-relink -Package `
  -CMakeArguments '-DFETCHCONTENT_SOURCE_DIR_TTPLAYER_LAME=C:/src/lame-4.0'
```

该覆盖目录须包含 LAME 4.0 的完整源码；无需原版 `ttp_enc.dll`、反编译器或其它插件项目。Release 说明链接到该次构建提交及 LAME 来源、许可证和重链接说明。专有 `fhtpro.flt`、`flt.dat` 与原版二进制均不在仓库和发行包内。

## 日期版本与 Release 体积优先构建

DLL 的文件版本和产品版本使用北京时间 `yyyy.MM.dd`，同日发布补丁使用 `pN`；
例如 `2026.10.06p1` 对应固定数字版本 `2026.10.6.1`。Actions 在编译前确定最终版本，
DLL、发行包和发布标签使用同一版本。各项目继续独立构建。

Release 的统一配置见 [cmake/size_release.cmake](cmake/size_release.cmake)：
`/O1 /Os /Gy /Gw /GF`、跨模块优化和链接去除未引用代码／折叠相同代码，关闭 Release 调试信息。
本项目经 `/Ob0`、`/Ob1`、`/Ob2` 对比，默认选择 `/Ob1`；
可用 `-DTTP_SIZE_INLINE_LEVEL=0|1|2` 重新测量不同内联策略。
保留正常浮点语义、异常处理及 VC-LTL／YY-Thunks 的 XP／Win7 兼容配置。
Actions 不编译、不运行测试；本次新增的测试仅位于本地 `rebuild/tests/dll_size_versions`，不进入发行包。

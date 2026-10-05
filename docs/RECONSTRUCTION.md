# ttp_enc 恢复设计

## 原版证据与实现边界

分析对象为原版 32 位 `AddIn/ttp_enc.dll`（163,976 字节，映像基址 `60250000`）、用户提供的 `fhtpro.flt` 及其解包后的分析副本。反编译源码、原版二进制、测试代码和运行证据只保存在工作区 `rebuild/tests`，不作为本项目发行内容。

| 原版函数 | 行为 | 重建对应 |
| --- | --- | --- |
| `60253278` | 枚举 MP3、WMA、可用时的 mp3PRO | `src/plugin.cpp` 工厂顺序 0/1/2 |
| `6025235D` | HKLM Syntrillium CEPro2 下先查 MAINDIR，再查 INIDIR | 不再依赖安装注册表，直接使用 DLL 同目录 |
| `6025231C` | 拼接 fhtpro.flt，检查是否存在 | `pro_available()` |
| `602537B7` | 模块初始化时缓存滤镜可用状态 | 枚举时检查文件，实际编码时加载 |
| `6025275B` | 加载滤镜并获取输出及选项函数 | `load()` |
| `60252816` / `60252915` | 打开滤镜、传入 PCM | `ProSession::start/write` |
| `6025252F` | 关闭编码并处理文件标签 | `Finish` 与析构清理；关闭滤镜后通过宿主标准标签接口回写，无宿主接口时使用 ID3 回退 |
| 主程序 `004CD30C` | 查询流编码接口，否则回退文件路径接口 | MP3 支持两种入口；WMA/mp3PRO 支持路径入口 |

原版 MP3 不是外部 `lame_enc.dll` 转发器，而是内置 LAME；界面版本字符串与内核版本函数还存在差别。因此此次采用官方 LAME 4.0 源码重建 MP3 内核，不复刻旧内核输出。WMA 延续系统 Windows Media 编码器路线。`mp3PRO.dll` 的导出属于 Winamp 输入插件，不能提供编码功能。

## ABI 与生命周期

`include/ttp_enc_abi.h` 定义 x86 `__stdcall` 接口与 GUID。返回字符串使用 `CoTaskMemAlloc`；对象使用 COM 风格引用计数及同一 IUnknown 身份。

| 接口 | IUnknown 后的虚表顺序 |
| --- | --- |
| AddIn | Enum |
| EncoderCreator | Create、Name、Extension、Configurable、Configure |
| Encoder | Open、Start、Finish、Write、Extension |
| StreamEncoder | 继承 Encoder 后追加 OpenStream |
| Metadata | Count、At、Get、Set |
| Buffer | SetLength、Capacity、Data |

调用顺序为创建 → Open → 设置元数据 → Start → 多次 Write → Finish → Release。析构会收尾尚未结束的正常编码；重复 Finish 返回 S_FALSE。失败初始化释放已获取的滤镜占用。输入 PCM 只读，转换在有界临时缓冲区进行。

MP3/mp3PRO 优先使用宿主的 `CreateStdContent` 与 `CreateStreamOnFile` 接口，遵循宿主配置的标签写入策略；没有宿主标准标签接口时才回退为 Unicode ID3v2.3。MP3 的 CBR 不生成 Info 帧，非 CBR 在结束时回写 LAME/Xing 帧。mp3PRO 在关闭滤镜后通过宿主文件接口重新打开输出以写入标签；独立宿主下使用同目录临时文件流式复制并替换。原版直接写盘文件流未实现 `Commit`，对此按兼容语义处理，真实提交错误仍向上传递。WMA 使用系统 ASF 元数据接口。标题、歌手、专辑、专辑艺术家、流派、年份、音轨、注释以及作曲、歌词、ISRC、副标题和版权链接等映射见 [PARITY_FIXES.md](PARITY_FIXES.md)。

## 原配置键

保留原版的配置命名，避免曾误用 Type 导致设置被忽略：

| INI 节／键 | 值 |
| --- | --- |
| Quality/Mode | 0 CBR，1 ABR，2 预设 |
| Quality/Preset | 比特率，或原版 1000–1007 预设；新增 1100–1109 对应 V0–V9 |
| Stereo/Joint | 联合立体声 |
| WMA/VBR | 0 CBR，1 两遍比特率 VBR，2 质量 VBR；新增 3 为 16 位无损 |
| WMA/Preset | 对应比特率或质量 |
| mp3PRO/Options | coolmp3.ini 的配置编号，原默认 7 |

WMA 模式 1 不能简化为单遍质量 VBR。先用 IWMCodecInfo3 枚举两遍格式，再在 BeginWriting 后查询及启动预处理；临时文件保留每次 PCM 分块长度。第二遍以相同分块和时间戳重放，用独立 INSSBuffer 提交给系统写入器。[微软两遍编码说明](https://learn.microsoft.com/en-us/windows/win32/wmformat/using-two-pass-encoding)

## mp3PRO 免安装注册表

采用直接适配已有滤镜的方式：

1. 从 `ttp_enc.dll` 所在目录加载 `fhtpro.flt`。
2. 仅在该滤镜模块内替换配置文件 API 的导入指针，让 `coolmp3.ini` 使用本项目可写目录。
3. 为滤镜的输出文件名传入内部占位符，其 CreateFileA 导入映射到真实 Unicode 路径的 CreateFileW。支持中文及非 ANSI 字符。
4. 监测该输出句柄的 WriteFile 失败和短写，错误会传回播放器。
5. 卸载时恢复已替换的导入指针，避免其它持有同一滤镜的调用者留下悬空指针。

用户的滤镜经过 UPX 压缩：加载后的导入目录只保留壳的入口。为此还检查已提交可读执行页中的 x86 间接导入调用／跳转，仅接受模块范围内、当前值精确等于目标 Win32 API 的指针槽；不改动编解码算法指令，不使用某版 DLL 的硬编码地址。

滤镜本身加载时仍可能读取其历史注册表设置；本项目无需创建这些键，也不拦截整个进程的注册表 API。`flt.dat` 不参与已验证的编码路径。文件型兼容仅作用于当前滤镜的配置和输出，不改变播放器及其它插件的文件行为。

缺少配置时写入经过验证的 mp3PRO 默认预设，按输入声道／采样率建立本次会话条目，不覆盖用户选择的永久编号。滤镜只能安全地同时编码一路，配置窗口与编码会话也不能并行进入该滤镜。

## XP 修复与现代构建

使用与其它重建插件一致的 VC-LTL 5.3.1、YY-Thunks 1.2.2，PE 子系统版本 5.01；Release 后自动检查 XP／Win7 导入清单。

实际 XP 虚拟机发现：动态加载的 DLL 中，带 MSVC 线程安全保护的局部静态对象初始化未可靠执行，导致配置目录为空，WMA 永远采用默认配置，mp3PRO 路径异常。已将设置目录改为模块级对象并用互斥锁显式惰性初始化，滤镜状态使用模块级对象，修复后四种 WMA 模式及 mp3PRO 均通过实机测试。

这说明仅通过 PE 导入审计不能证明 XP 上的完整运行行为，必须补充真实编码和配置保存测试。

## 尚不声称的兼容性

已完成原版 EXE 的自动配置窗口测试、三类编码器真实转换、批量转换与标签策略回读，并完成独立 ABI 驱动和重建版宿主集成测试；不声称所有人工操作、异常细节、界面像素或新旧 LAME 输出完全等价。输入精度及旧滤镜限制见 README，具体测试范围见 [VALIDATION.md](VALIDATION.md)。

## 原版行为补充修复

默认值、窗口资源、参数匹配和真实宿主兼容性修复详见 [PARITY_FIXES.md](PARITY_FIXES.md)。MP3 在同一窗口将 VBR 拆分为原版预设、V0–V9 质量等级两组，WMA 扩大原窗口以直接显示四种模式；现代 LAME、扩展模式和独立调用回退继续保留，不能称为逐字节或所有异常调用完全等价。

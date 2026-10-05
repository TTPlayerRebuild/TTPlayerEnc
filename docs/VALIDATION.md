# 编码插件验证记录

日期：2026-10-06。本文记录原版行为修复后的验证。测试代码、原版二进制、伪代码、音频和日志只保存在工作区 `rebuild/tests`，不提交到 ttp_enc，Actions 不运行或上传这些测试。

## 最新：MP3 VBR 分组回归

MP3 的 VBR 已拆为“原版预设”和“质量等级”两行，分别提供八个旧预设与 V0–V9；WMA 四种模式在同一窗口中直接选择。移除独立扩展窗口，保留原默认配置和配置键。

- Windows 11、XP、Win7 各通过 19 项窗口操作检查：12 项选择/切换/保存/取消，7 项保存后重新打开回读。覆盖两个 VBR 分组的独立选项保留，以及既有 Mode/Preset 配置的分组恢复。
- 已检查 MP3/WMA 配置窗口截图。MP3/WMA 的布局为容纳四行模式有意扩大，不再要求原资源矩形完全相同。
- Windows 11 的 44.1 kHz 双声道质量 VBR 检查：98 编码成功，100 返回 80070057；不将其映射为无损。
- 重新构建并打包；包内 DLL 与本轮 XP/Win7 窗口测试使用的 DLL 完全相同。
- 私有证据：grouped_vbr_dialog_results.json、XP-grouped-vbr.log、Win7-grouped-vbr.log、grouped_vbr_package_validation.json、wma_quality_check.json。

以下编码与宿主记录属于此前原版行为修复验证；本次只修改选项窗口和选项选择逻辑。

## 此前：本地与原版对照

环境：Windows 11，VS 2026 x86 Release，VC-LTL 5.3.1 / YY-Thunks 1.2.2。

| 验证 | 结果 |
| --- | --- |
| 13 组编码与解码 | MP3 CBR/ABR/VBR、流/文件入口、析构收尾；WMA CBR/质量 VBR/两遍 VBR/16 位无损；mp3PRO 立体声/单声道/float64/忙碌冲突/析构收尾，通过 |
| Unicode 和输入所有权 | 中文、emoji 文件名和标签可用；Write 前后输入缓冲不变 |
| 23 项扩展检查 | 配置保存/取消 4 项、旧 MP3 预设编码及解码 16 项、重建版实际转换流程 3 项，通过 |
| 11 项窗口回归 | 默认选择、各模式参数保留、控件启用状态、隐藏 WMA 行、扩展保存/取消、主窗口取消、无损模式，通过 |
| 四份编译后窗口资源 | 中文/英文 MP3/WMA 的尺寸、标题、字体和原控件矩形、样式与原 DLL 对照通过；额外“扩展…”入口单独保留 |
| 七组成对接口测试 | 同时驱动原版和重建 DLL：无配置 MP3、CBR128、48 kHz ABR96、float64、Standard、无配置 WMA、不支持的 WMA 采样率 |
| 参数对齐 | 默认 MP3 192 Kbps；CBR 帧标志相同；ABR96 输出保持 48 kHz；默认 WMA 质量75；不支持的组合均返回 80070057 |

成对测试中的两个“不支持采样率”调用预期失败，不能误记为成功编码。原版与现代 LAME 的 VBR 大小、压缩算法和尾部刷新不要求逐字节相同。MP3/WMA 基础窗口已截图检查。

## 此前：原版与重建版宿主

在私有复制的播放器目录中运行未修改的原版 TTPlayer.exe，通过真实“转换格式”窗口枚举并调用新插件：

- MP3、WMA 配置窗口可从原版宿主打开和取消。
- MP3、WMA、mp3PRO 三类输出均完成，并通过 FFmpeg 解码、标题/艺术家回读。
- MP3 分别使用宿主 ID3v2 策略和 ID3v1＋APEv2 策略，检查了实际输出标签布局。
- mp3PRO 通过宿主默认 ID3v1＋APEv2 策略检查。
- 两首文件的 MP3 批量转换完成；上述五次运行共六个输出均已解码。

真实宿主测试发现并修复两个独立接口驱动未覆盖的问题：原版文件流的 Commit 未实现，不能因此跳过标签写入；mp3PRO 回写标签须通过宿主 CreateStreamOnFile 创建对应文件流。

重建版宿主通过已有 conversion_recovery_tests.exe 的 ConvertPlaylistTrack 验证三个编码器。没有替换用户安装目录中的原版插件。

## 此前：XP／Win7 编码回归

此前修复的 DLL 在两台 VirtualBox 虚拟机的隔离目录 C:\EncParity20261006 中各通过 **22 项**检查：

| 项目 | XP | Win7 |
| --- | --- | --- |
| MP3 CBR、48 kHz ABR、VBR | 通过 | 通过 |
| WMA CBR、质量 VBR、两遍 VBR、16 位无损 | 通过 | 通过 |
| mp3PRO 双声道、单声道 | 通过 | 通过 |
| WMA 补充标签 | 通过 | 通过 |
| 11 项配置窗口操作 | 通过 | 通过 |
| 拒绝不支持的 48 kHz／质量75 | 通过 | 通过 |

两台系统的原版 DLL 对 48 kHz／质量75 也返回 80070057；44.1 kHz／质量75 编码成功。这是系统枚举格式的限制，不能以静默降低采样率代替。

两台虚拟机的 **20 个音频输出**全部复制回本地并通过完整解码、有效 PCM 的 RMS 范围和中文/emoji 标题检查。另验证 ABR 的 48 kHz 输出，以及 WMA 的 Composer、Lyrics、ISRC、Subtitle 标准属性和版权 URL 实际记录。CopyrightURL 由 SDK 写入 ASF Content Branding Object，不出现在 FFprobe 普通标签列表中。

该轮编码测试使用同一 DLL；本轮合并界面后的 DLL 另外通过上述 19 项窗口测试。此前对原滤镜的 SBR/mp3PRO 格式回读、保留滤镜引用后的卸载恢复测试仍记录在私有证据中；本次普通 MP3 解码检查本身不能验证 SBR 增强层。

## 发行包检查

ttp_enc-2026.10.06.zip：

- DLL：310,272 字节；ZIP：168,931 字节。
- DLL SHA-256：d2899306fcca236f1e7b35a8e52805eb241d52e49873e95c57a2e93854b504ed。
- ZIP SHA-256：68a873133cf656312b350b67c9a2830efb0b3ec72f6db18b11652ecb16d23a15。
- 唯一导出 ttpGetSoundAddIn；x86，子系统版本 5.01。
- XP／Win7 静态导入审计通过：6 个 DLL、145 个导入，无外置现代 VC++ Runtime DLL 依赖。
- ZIP 精确包含 AddIn/ttp_enc.dll 与 SHA256SUMS.txt，内外 SHA-256 已重新计算。
- RCDATA 201/202 与仓库 LAME COPYING/LICENSE 原文一致。
- 文件/产品版本 2026.10.06，固定版本 2026.10.6.0，构建脚本检查通过。

## 范围和限制

没有独立 Windows 10 实机回归，也没有穷尽所有 DPI、系统语言、第三方滤镜版本、超长音频、磁盘耗尽和掉电恢复场景。WMA 无损仅针对编码前的 16 位样本。现代 LAME、扩展模式和兼容性改进是有意保留的差异；详见 [原版行为修复](PARITY_FIXES.md)。

## 私有证据

工作区 rebuild/tests/enc_rebuild：

- results.json、decode_results.json、extended_results.json、dialog_results.json。
- binary_parity.json、parity_results.before-fix.json、parity_results.json。
- original-host/*/result.json：真实原版宿主五次验证及输出回读。
- XP-parity-final.log、Win7-parity-final.log、两台虚拟机的 parity-output 目录。
- parity_output_validation.json、parity_package_validation.json。
- 早期滤镜证据：pro_readback.json、retained_filter.json、audibility_results.json。

这些路径不是仓库的构建依赖。私有测试不参与 Actions，也不进入发行包。

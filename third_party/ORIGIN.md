# 第三方来源

本目录只保留声明和许可证，不提交上游完整源码。下载固定版本、校验并编译的逻辑在 `cmake/lame.cmake`、`cmake/legacy_windows.cmake`。

| 依赖 | 固定版本 | 下载 SHA-256 |
| --- | --- | --- |
| LAME | 4.0 | `3df5124d5ad3a98312ffd7ba6a9b36230e4f8a3e66d3ce0f425e336c32d216eb` |
| VC-LTL | 5.3.1 | `7a18799ed3aa84a225610a5447a56bc534c5c98ccb8dec05caba0e3f633431ad` |
| YY-Thunks | 1.2.2 | `518ed7ef4825e8a41997fbccfa2c8090cf31a6038fd51520a2e49886f947f9fc` |

## LAME

- [官方网站](https://lame.sourceforge.io/)；[官方版本下载页](https://lame.sourceforge.io/download.php)。截至 2026-10-06，官方最新版本为 4.0。
- 固定归档：[lame-4.0.tar.gz](https://downloads.sourceforge.net/project/lame/lame/4.0/lame-4.0.tar.gz)，1,496,810 字节。
- [COPYING](lame/COPYING)、[LICENSE](lame/LICENSE) 原文保留；使用 LGPL 授权的 libmp3lame。
- 使用上游 `configMS.h`，仅构建编码库，不构建命令行程序或解码器。上游源码未修改。
- 静态链接进 `ttp_enc.dll`；重链接说明见 [README](../README.md#源码与重链接)。适配代码和完整构建规则在本仓库，可指定本地 LAME 源码覆盖下载。

## Windows 兼容依赖

- [VC-LTL 5.3.1](https://github.com/Chuyu-Team/VC-LTL5/releases/tag/v5.3.1)，[许可证](../docs/licenses/VC-LTL-LICENSE.txt)。
- [YY-Thunks 1.2.2](https://github.com/Chuyu-Team/YY-Thunks/releases/tag/v1.2.2)，[许可证](../docs/licenses/YY-Thunks-LICENSE.txt)。
- 本项目使用原有固定兼容依赖版本，与相邻项目的构建保持一致。“最新编码器”指 MP3 编码内核；上述库不是编码器。

## 运行时可选组件

WMA 使用 Windows 自带的 Windows Media 编码器，不重新分发系统 DLL。mp3PRO 使用用户自行提供的 `fhtpro.flt`；已测试的 Cool Edit Pro 2.1.3097.0 文件 SHA-256 为 `1dc878b79b2a6729a0cc773731028ba49721dd6a70902963cf8bb21780388827`。该文件、`flt.dat`、`mp3PRO.dll` 和原版 `ttp_enc.dll` 均不在本项目源码或发行包中。

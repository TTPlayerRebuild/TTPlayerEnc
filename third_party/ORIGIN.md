# 第三方来源

本目录只保留声明和许可证，不提交上游完整源码。下载固定版本、校验并编译的逻辑在 `cmake/lame.cmake`、`cmake/legacy_windows.cmake`。

| 依赖 | 固定版本 | 下载 SHA-256 |
| --- | --- | --- |
| LAME | 3.93.1 | `cc19feb74b7b194667719af438f7cef361acc24f9c3f7c348f12e08d071fa65d` |
| LAME | 3.101 | `7578af6eebd578b2bd64e468fac4ae1f03670a7e028166e67f855674b9b6aeac` |
| LAME | 4.0 | `3df5124d5ad3a98312ffd7ba6a9b36230e4f8a3e66d3ce0f425e336c32d216eb` |
| LAME | 4.1beta1 | `e4fa61f7752b1b5cb71eb4c170dab0e713279ec542f844a771b545aa537b45f4` |
| VC-LTL | 5.3.1 | `7a18799ed3aa84a225610a5447a56bc534c5c98ccb8dec05caba0e3f633431ad` |
| YY-Thunks | 1.2.2 | `518ed7ef4825e8a41997fbccfa2c8090cf31a6038fd51520a2e49886f947f9fc` |

## LAME

- [官方网站](https://lame.sourceforge.io/)；[官方版本下载页](https://lame.sourceforge.io/download.php)。默认保持 4.0，另可选择历史版 3.93.1、3.101 或测试版 4.1beta1。
- 3.93.1 使用官方目录 `/lame/3.93.1` 的 [lame-3.93.1.tar.gz](https://sourceforge.net/projects/lame/files/lame/3.93.1/lame-3.93.1.tar.gz/download)，1,037,603 字节，SHA-256 与该下载页公布值一致，替代此前的 3.93。该年代 `configMS.h` 的通用 VERSION 字符串陈旧，`version.h` 和实际版本函数／MP3 标记也未体现补丁号（仍为 3.93）；核对补丁版需结合归档哈希与 `configure.in` 的 `AM_INIT_AUTOMAKE(lame,3.93.1)`，不改写上游版本字符串。
- 固定归档：[lame-3.101.tar.gz](https://downloads.sourceforge.net/project/lame/lame/3.101/lame-3.101.tar.gz?download=1)，1,466,586 字节；[lame-4.0.tar.gz](https://downloads.sourceforge.net/project/lame/lame/4.0/lame-4.0.tar.gz?download=1)，1,496,810 字节；[lame-4.1beta1.tar.gz](https://downloads.sourceforge.net/project/lame/lame/4.1beta1/lame-4.1beta1.tar.gz?download=1)，2,294,115 字节。
- 四个版本统一复用仓库的 [COPYING](lame/COPYING)、[LICENSE](lame/LICENSE)，不另设 `lame393` 副本；声明仅保留在仓库，不嵌入 DLL 或加入 ZIP。各官方源码归档仍保留各自原始声明，下载目录不修改；本项目使用 LGPL 授权的编码库，不构建受不同许可约束的旧解码器。
- 使用上游 `configMS.h`，仅构建编码库，不构建命令行程序或解码器。上游源码未修改；通过生成的预处理头为各版本的公共及内部 C 符号添加独立前缀（清单见 `cmake/lame_symbols.cmake`），不共享版本间的编码状态或全局表。
- 四个 LAME 库单独使用 `/O2 /Ob2 /Ot`，保留 `/fp:precise`、LTCG 和 SSE2 基线；适配层仍以体积优先构建。3.101／4.0 加入原归档的 `vector/xmm_quantize_sub.c` 并定义 `HAVE_XMMINTRIN_H`、`MIN_ARCH_SSE`，启用上游 SSE 例程；3.93.1 尚无这些 intrinsic 源文件，仅使用该版编码源，不引入 NASM 或较新版内核代替。
- 3.93.1 没有 `lame_get_lametag_frame`。`cmake/lame_3931.cmake` 保留原 FILE 标签函数，并在生成的 VbrTag.c 编译副本中追加由其派生的内存函数，继续遵守原 LGPL 声明；量化、心理声学和比特流编码源码不变，下载树不原地修改。`src/lame_3931.cpp` 记录实际首帧头和输出字节数供内存标签使用，不在正常编码路径创建标签临时文件。
- 4.1beta1 包含上游 SSE2、AVX2 和 AVX-512 内核，基线编译仍为 SSE2；更宽指令由上游在运行时检查 CPU 与系统寄存器保存能力后选择，不提高原有最低 CPU／系统要求。
- 四个版本静态链接进同一个 `ttp_enc.dll`；重链接说明见 [README](../README.md#源码与重链接)。适配代码和完整构建规则在本仓库，可分别指定本地 LAME 源码覆盖下载。

## Windows 兼容依赖

- [VC-LTL 5.3.1](https://github.com/Chuyu-Team/VC-LTL5/releases/tag/v5.3.1)，[许可证](../docs/licenses/VC-LTL-LICENSE.txt)。
- [YY-Thunks 1.2.2](https://github.com/Chuyu-Team/YY-Thunks/releases/tag/v1.2.2)，[许可证](../docs/licenses/YY-Thunks-LICENSE.txt)。
- 本项目使用原有固定兼容依赖版本，与相邻项目的构建保持一致。“最新编码器”指 MP3 编码内核；上述库不是编码器。

## 运行时可选组件

WMA 使用 Windows 自带的 Windows Media 编码器，不重新分发系统 DLL。mp3PRO 使用用户自行提供的 `fhtpro.flt`；已测试的 Cool Edit Pro 2.1.3097.0 文件 SHA-256 为 `1dc878b79b2a6729a0cc773731028ba49721dd6a70902963cf8bb21780388827`。该文件、`flt.dat`、`mp3PRO.dll` 和原版 `ttp_enc.dll` 均不在本项目源码或发行包中。

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$PreviousVersion,
    [Parameter(Mandatory = $true)][string]$Repository,
    [Parameter(Mandatory = $true)][string]$Commit,
    [string]$ArtifactDirectory = 'artifact',
    [string]$ServerUrl = 'https://github.com'
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'version.ps1')
$version = (Get-EncBuildVersion $Version).Name
if ($PreviousVersion -and -not (Read-EncVersionTag $PreviousVersion)) { throw 'Invalid previous release version.' }
if ($Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or $Commit -notmatch '^[0-9a-fA-F]{40}$') {
    throw 'Invalid repository or commit.'
}
$archiveName = "ttp_enc-$version.zip"
$hash = (Get-FileHash -LiteralPath (Join-Path $ArtifactDirectory $archiveName) -Algorithm SHA256).Hash.ToLowerInvariant()
$expected = (Get-Content -LiteralPath (Join-Path $ArtifactDirectory 'SHA256SUMS.txt') -Encoding UTF8 -Raw).Trim()
if ($expected -cne "$hash  $archiveName") { throw 'ENC package SHA-256 verification failed.' }
$repoUrl = "$ServerUrl/$Repository"
$logUrl = if ($PreviousVersion) { "$repoUrl/compare/$PreviousVersion...$version" } else { "$repoUrl/commits/$version" }
$notes = @"
**更新记录**: $logUrl

解压 ttp_enc-$version.zip，关闭播放器后，将 AddIn/ttp_enc.dll 放入安装目录。
支持 MP3（内置 LAME 3.93.1 / 3.101 / 4.0 / 4.1beta1，默认 4.0）、WMA，以及可选的 mp3PRO 编码。在 MP3 编码选项的“LAME 版本”下拉框切换内核；3.93 已升级为 3.93.1，旧配置值 393 兼容读取并在保存时改为 3931，4.1beta1 是可选测试版。3.93.1 的上游 MP3 标记仍为 LAME3.93，不靠标记辨别补丁版本。mp3PRO 需用户另行提供 fhtpro.flt，放在同一 AddIn 目录；无需导入注册表或 flt.dat。
同一 x86 DLL 支持 XP SP3、Win7 和新系统，CPU 需要 SSE2；WMA 使用系统已有的 Windows Media 编码器。
LAME 内核采用速度优先编译，3.101／4.0 启用上游 SIMD，PCM 转换和会话缓冲已优化；默认仍为 4.0、算法 quality=2，不以降低质量换速度。
ZIP 只包含 AddIn/ttp_enc.dll 和 SHA256SUMS.txt。四个 LAME 版本共用的 COPYING／LICENSE 仅保留在仓库，不嵌入 DLL 或加入 ZIP。完整构建源码、依赖版本、重链接方法和许可证保留在下面的源代码仓库；再分发者仍需满足 LGPL 要求。

[源码、构建与重链接说明]($repoUrl/tree/$Commit)
[LAME 各版本来源与校验值]($repoUrl/blob/$Commit/third_party/ORIGIN.md)
[LAME LGPL 许可证]($repoUrl/blob/$Commit/third_party/lame/COPYING)
[LAME 使用说明]($repoUrl/blob/$Commit/third_party/lame/LICENSE)
[VC-LTL 许可证]($repoUrl/blob/$Commit/docs/licenses/VC-LTL-LICENSE.txt)
[YY-Thunks 许可证]($repoUrl/blob/$Commit/docs/licenses/YY-Thunks-LICENSE.txt)
"@
$notes | Set-Content -LiteralPath (Join-Path $ArtifactDirectory 'release-notes.md') -Encoding UTF8

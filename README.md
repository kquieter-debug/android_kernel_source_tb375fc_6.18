# Lenovo TB375FC (联想小新Pad Pro 12.7 2025) 移植 android-17-6.18-lts 内核源码

[English](README_en.md) | **简体中文**

> 本仓库派生自 Google Android 通用内核（GKI）`android-17-6.18-lts`   
> 目标是把 TB375FC 设备的 KMI 基线由 `android14-6.1` 提升到 `android17-6.18`
>
> 本仓库只包含内核源码本身，**不包含** 谷歌分发的 Kleaf 构建平台，也不含厂商驱动  
> 内核模块树详见我的仓库: [android_kernel_modules_tb375fc_6.18](https://github.com/kquieter-debug/android_kernel_modules_tb375fc_6.18)

## 尚未完成移植，请勿在生产环境使用

## 仓库说明

本仓库对应 Kleaf 工作区的 `/common` 子目录（工作区根目录在下文中记为 `6.18`），
是内核源码树本体  
厂商驱动以 Kleaf 外部模块（external module）方式构建，均位于 `common-modules/mediatek`，不应当作为内核源码 overlay 合入本树  
原始上游内核说明详见: [`README_android_kernel`](README_android_kernel) 与 [`README_linux_kernel`](README_linux_kernel) 

本仓库相对上游 GKI 的改动集中在 `BUILD.bazel`：新增 `kernel_aarch64_tb375fc_min`、
`kernel_aarch64_tb375fc_vendor`、`kernel_aarch64_tb375fc_p1` 三个 `kernel_build` 目标，用于按不同配置验证 6.18 内核的可启动性  
与这三者相关的配置文件分布如下：

- `device/tb375fc/tb375fc_device.config`（三条 6.18 移植决策）已随厂商驱动版本化，
  位于 `common-modules/mediatek/device/tb375fc/`；
- 设备树源文件位于厂商模块树的 `arch/arm64/boot/dts/mediatek/`（本机机型对应的设备树为
  `tb8792p1_64.dts`），当前 Kleaf 目标尚未定义 DTB 产物

## Kleaf 构建平台搭建

使用 repo 拉取谷歌分发的完整内核构建平台:

```
repo init -u https://android.googlesource.com/kernel/manifest -b common-android17-6.18-lts
```

在使用 repo sync 之前，需要在工作区根目录下创建 `.repo/local_manifests/replace_common.xml`，内容如下：

```
<?xml version="1.0" encoding="UTF-8"?>
<manifest>
  <remove-project name="kernel/common" />

  <project path="common"
           name="android_kernel_modules_tb375fc_6.18"
           remote="third-party-kernel"
           revision="main" />

  <remote name="third-party-kernel"
          fetch="https://github.com/kquieter-debug"
          review="https://github.com/kquieter-debug" />
</manifest>
```

以此来替换原有的谷歌内核树拉取对象  
然后可以执行同步 :

```
repo sync -j$(nproc)
```

或者

```
repo sync -j$(nproc) --no-tags --no-clone-bundle
```

来加速同步  

如果有内核模块同步的需要也同上文所述添加替换用清单

## 构建说明（Kleaf）

所有命令在工作区根目录执行（下称 `6.18`），该目录下应当能看到 `common/`、
`common-modules/`、`device/`、`build/`、`prebuilts/` 以及 `tools/bazel` 等

```bash
cd 6.18

# 1) 纯 GKI 内核（最快，用于确认工具链与基线本身可用）
tools/bazel build //common:kernel_aarch64 --config=fast

# 2) 早期启动验证目标：仅在 GKI 基础上打开 CONFIG_ARCH_MEDIATEK
tools/bazel build //common:kernel_aarch64_tb375fc_min --config=fast

# 3) 厂商配置内核：先 gki_defconfig，再合并 mgk_64_k61_defconfig 与设备增量
tools/bazel build //common:kernel_aarch64_tb375fc_vendor --config=fast

# 4) P1 变体：额外编出 in-tree 模块，并生成非空 Module.symvers
tools/bazel build //common:kernel_aarch64_tb375fc_p1 --config=fast
```

内核产物默认在 `bazel-bin/common/<目标名>/` 下，例如
`bazel-bin/common/kernel_aarch64_tb375fc_vendor/vmlinux`，包含
`Image`、`Image.lz4`、`System.map`、`vmlinux`、`vmlinux.symvers`

厂商模块的编译、安装布局、P1 子集与出包目标定义在厂商模块树中，本仓库不重复列出：

```bash
tools/bazel build //common-modules/mediatek:mgk_64_k61_modules_install --config=fast
tools/bazel build //common-modules/mediatek:mgk_p1_modules_install --config=fast
tools/bazel build //common-modules/mediatek:mgk_64_k61_images --config=fast
```

> **注意**：`kernel_aarch64_tb375fc_vendor` 的 `make_goals` 不含 `modules`，其输出的
> `Module.symvers` 恒为 0 字节；外部模块 modpost 需要用 `vmlinux.symvers` 顶替，
> 或改用带 `modules` 目标的 `kernel_aarch64_tb375fc_p1`

> **WSL 提示**：若 `tools/bazel` 因 musl 版 `py3-cmd` 与宿主 `LD_PRELOAD` 冲突而失败，
> 可改用 glibc 版本：
>
> ```bash
> prebuilts/build-tools/linux-x86/bin/py3-cmd build/kernel/kleaf/bazel.py build <target> --config=fast
> ```

## 构建说明（手动 kbuild + clang）

内核本体建议始终走 Kleaf：目标里的 defconfig 合并顺序、`Kconfig.ext`、厂商变量
（`DEVICE_MODULES_PATH` 等）都由 Kleaf 目标保证，手工复现容易得到一份 "看起来成功 但配置不对" 的内核

手动 kbuild 主要用途是**一次性收集外部模块的全部编译错误**（Kleaf 每次只暴露少量），
纯内核构建的要点只有三条：

1. 先构建 `//common:kernel_aarch64_tb375fc_vendor_modules_prepare` 解出预置输出目录；
2. 用 `vmlinux.symvers` 覆盖输出目录里的空 `Module.symvers`；
3. 预置 clang 必须优先于宿主编译器，并清掉 `MAKEFLAGS`/`MFLAGS`

## 初始官方基线

在开始移植前的官方提交哈希为：`ed4489e76f6fafbe67b99e8a9419215313df189e` (`Merge android17-6.18 into android17-6.18-lts`，2026-11-05 16:43:55 UTC)  
移植工作开始后会停滞上游补丁合并，直至确认内核可正常后才会开始接受上游补丁

## 更多信息

### 许可证

遵循上游使用 GNU General Public License v2 (GPLv2 License) — 详见 [`COPYING`](COPYING).

### 有关内核
原始的上游内核说明，参见 [README_linux_kernel](README_linux_kernel) 以及 [README_android_kernel](README_android_kernel)  

本内核源码派生自 Google Android 通用内核:
`https://android.googlesource.com/kernel/common`  
分支: `android-17-6.18-lts`

---

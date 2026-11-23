# Porting android-17-6.18-lts Kernel Sources for Lenovo TB375FC (Lenovo Xiaoxin Pad Pro 12.7 2025)

**English** | [简体中文](README.md)

> This repository is derived from the Google Android Common Kernel (GKI) `android-17-6.18-lts`  
> The goal is to raise the KMI baseline of the TB375FC from `android14-6.1` to `android17-6.18`
>
> This repository contains only the kernel sources themselves. It does **not** contain the
> Google-distributed Kleaf build platform, nor the vendor driver modules  
> See my repository for the kernel module tree: [android_kernel_modules_tb375fc_6.18](https://github.com/kquieter-debug/android_kernel_modules_tb375fc_6.18)

## Not yet fully ported — do not use in a production environment

## About this repository

This repository corresponds to the `/common` subdirectory of the Kleaf workspace (the
workspace root is referred to as `6.18` below) and is the kernel source tree itself  
Vendor drivers are built as Kleaf external modules and all live in `common-modules/mediatek`;
they must not be merged into this tree as a source overlay  
See [`README_android_kernel`](README_android_kernel) and [`README_linux_kernel`](README_linux_kernel)
for the original upstream kernel documentation

Changes relative to the upstream GKI are concentrated in `BUILD.bazel`: three `kernel_build`
targets, `kernel_aarch64_tb375fc_min`, `kernel_aarch64_tb375fc_vendor` and
`kernel_aarch64_tb375fc_p1`, are added to verify that the 6.18 kernel boots under different
configurations  
The configuration files related to these three targets are distributed as follows:

- `device/tb375fc/tb375fc_device.config` (the three 6.18 porting decisions) is versioned
  together with the vendor drivers and lives in `common-modules/mediatek/device/tb375fc/`;
- The device tree sources live in the vendor module tree under
  `arch/arm64/boot/dts/mediatek/` (the device tree for this device is `tb8792p1_64.dts`);
  the Kleaf targets do not define any DTB output yet

## Setting up the Kleaf build platform

Use repo to fetch the complete, Google-distributed kernel build platform:

```
repo init -u https://android.googlesource.com/kernel/manifest -b common-android17-6.18-lts
```

Before running repo sync, create `.repo/local_manifests/replace_common.xml` in the workspace
root with the following content:

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

This replaces the Google kernel tree that would otherwise be fetched  
Then the sync can be run:

```
repo sync -j$(nproc)
```

or

```
repo sync -j$(nproc) --no-tags --no-clone-bundle
```

to speed up the sync

If the kernel modules also need to be synced, add a replacement manifest in the same way as
described above

## Building (Kleaf)

All commands are run in the workspace root (referred to as `6.18` below); that directory should
contain `common/`, `common-modules/`, `device/`, `build/`, `prebuilts/`, `tools/bazel`, etc.

```bash
cd 6.18

# 1) Pure GKI kernel (fastest; confirms that the toolchain and the baseline itself work)
tools/bazel build //common:kernel_aarch64 --config=fast

# 2) Early-boot verification target: only enables CONFIG_ARCH_MEDIATEK on top of GKI
tools/bazel build //common:kernel_aarch64_tb375fc_min --config=fast

# 3) Vendor configuration kernel: gki_defconfig first, then mgk_64_k61_defconfig and the device delta
tools/bazel build //common:kernel_aarch64_tb375fc_vendor --config=fast

# 4) P1 variant: additionally builds the in-tree modules and produces a non-empty Module.symvers
tools/bazel build //common:kernel_aarch64_tb375fc_p1 --config=fast
```

Kernel outputs are located under `bazel-bin/common/<target name>/` by default, for example
`bazel-bin/common/kernel_aarch64_tb375fc_vendor/vmlinux`, and include `Image`, `Image.lz4`,
`System.map`, `vmlinux` and `vmlinux.symvers`

The targets for building the vendor modules, their install layout, the P1 subset and the image
package are defined in the vendor module tree and are not repeated in this repository:

```bash
tools/bazel build //common-modules/mediatek:mgk_64_k61_modules_install --config=fast
tools/bazel build //common-modules/mediatek:mgk_p1_modules_install --config=fast
tools/bazel build //common-modules/mediatek:mgk_64_k61_images --config=fast
```

> **Note**: the `make_goals` of `kernel_aarch64_tb375fc_vendor` do not include `modules`, so its
> `Module.symvers` output is always 0 bytes; modpost for external modules needs to be fed
> `vmlinux.symvers` instead, or the `kernel_aarch64_tb375fc_p1` target, which does have the
> `modules` goal, can be used

> **WSL note**: if `tools/bazel` fails because the musl build of `py3-cmd` conflicts with the
> host `LD_PRELOAD`, the glibc variant can be used instead:
>
> ```bash
> prebuilts/build-tools/linux-x86/bin/py3-cmd build/kernel/kleaf/bazel.py build <target> --config=fast
> ```

## Building (manual kbuild + clang)

The kernel proper should always be built through Kleaf: the defconfig merge order, `Kconfig.ext`
and the vendor variables (such as `DEVICE_MODULES_PATH`) are all guaranteed by the Kleaf targets,
and reproducing them by hand easily yields a kernel that "looks successful but is configured
wrongly"

The main purpose of the manual kbuild path is to **collect all compile errors of the external
modules in a single pass** (Kleaf only surfaces a few at a time); for a pure kernel build there
are only three points to keep in mind:

1. First build `//common:kernel_aarch64_tb375fc_vendor_modules_prepare` and extract the prepared
   output directory;
2. Overwrite the empty `Module.symvers` in the output directory with `vmlinux.symvers`;
3. The prebuilt clang must take precedence over the host compiler, and `MAKEFLAGS`/`MFLAGS` must
   be cleared

## Initial upstream baseline

The official commit hash before the porting work started is:
`ed4489e76f6fafbe67b99e8a9419215313df189e` (`Merge android17-6.18 into android17-6.18-lts`,
2026-11-05 16:43:55 UTC)  
Once the porting work starts, upstream patch merges are stalled; upstream patches will only be
accepted again once the kernel is confirmed to work

## More information

### License

Follows upstream and uses the GNU General Public License v2 (GPLv2 License) — see [`COPYING`](COPYING).

### About the kernel
For the original upstream kernel documentation, see [README_linux_kernel](README_linux_kernel)
and [README_android_kernel](README_android_kernel)  

These kernel sources are derived from the Google Android Common Kernel:
`https://android.googlesource.com/kernel/common`  
Branch: `android-17-6.18-lts`

---

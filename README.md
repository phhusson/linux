Mainline linux kernel for Orange Pi PC/PC2/PC3/One, TBS A711, PinePhone, PocketBook Touch Lux 3
-----------------------------------------------------------------------------------------------

This kernel tree is meant for:

- Orange Pi One
- Orange Pi PC
- Orange Pi PC 2
- Orange Pi 3
- PinePhone 1.0, 1.1 and 1.2(a/b)
- TBS A711 Tablet
- PocketBook Touch Lux 3
- Pinebook Pro

Features in addition to mainline:

- [Orange Pi One/PC/PC2] More aggressive OPPs for CPU
- [All] Mark one of DRM planes as a cursor plane, speeding up Xorg based desktop with modesetting driver
- [Orange Pi One/PC/PC2] Configure on-board micro-switches to perform system power off function
- [Orange Pi One/PC/PC2/3] HDMI audio
- [Orange Pi 3] Ethernet
- [TBS A711] HM5065 (back camera)
- [PinePhone] WiFi, Bluetooth, Audio, Modem power, HDMI out over USB-C, USB-C support, cameras, PMIC improvements, power management, fixes here and there
- [PocketBook Touch Lux 3] Display and Touchscreen support

Pre-built u-boot and kernels are available at https://xff.cz/kernels/

You may need some firmware files for some part of the functionality. Those are
available at: https://megous.com/git/linux-firmware

If you want to reproduce my pre-built kernels exactly, you'll need to uncomment
CONFIG_EXTRA_FIRMWARE_DIR and CONFIG_EXTRA_FIRMWARE in the defconfigs, and
point CONFIG_EXTRA_FIRMWARE_DIR to a directory on your computer where the
clone of https://megous.com/git/linux-firmware resides.

You can also leave those two config options commented out, and copy the contents
of https://megous.com/git/linux-firmware to /lib/firmware/ on the target device.

You can use this kernel to run a desktop environment on Orange Pi SBCs,
Arch Linux on your Pinephone, or to have a completely opensource OS on
a Pocketbook e-ink book reader.

Have fun!


Build instructions
------------------

These are rudimentary instructions and you need to understand what you're doing.
These are just core steps required to build the ATF/u-boot/kernel. Downloading,
verifying, renaming to correct directories is not described or mentioned. You
should be able to infer missing necessary steps yourself for your particular needs.

Get necessary toolchains from:

- https://releases.linaro.org/components/toolchain/binaries/latest/aarch64-linux-gnu/ for 64bit Orange Pi PC2 and Orange Pi 3, PinePhone
- https://releases.linaro.org/components/toolchain/binaries/latest/arm-linux-gnueabihf/ for 32bit Orange Pis, Pocketbook, TBS tablet

Extract toolchains and prepare the environment:

    CWD=`pwd`
    OUT=$CWD/builds
    SRC=$CWD/u-boot
    export PATH="$PATH:$CWD/Toolchains/arm/bin:$CWD/Toolchains/aarch64/bin"

For Orange Pi PC2, Orange Pi 3 or PinePhone:

    export CROSS_COMPILE=aarch64-linux-gnu-
    export KBUILD_OUTPUT=$OUT/.tmp/uboot-pc2
    rm -rf "$KBUILD_OUTPUT"
    mkdir -p $KBUILD_OUTPUT $OUT/pc2

Get and build ATF from https://github.com/ARM-software/arm-trusted-firmware:

    make -C "$CWD/arm-trusted-firmware" PLAT=sun50i_a64 DEBUG=1 bl31
    cp "$CWD/arm-trusted-firmware/build/sun50i_a64/debug/bl31.bin" "$KBUILD_OUTPUT"

Use sun50i_a64 for Orange Pi PC2 or PinePhone and sun50i_h6 for Orange Pi 3.

Build u-boot from https://megous.com/git/u-boot/ (opi-v2020.04 branch) with appropriate
defconfig (orangepi_one_defconfig, orangepi_pc2_defconfig, orangepi_pc_defconfig, orangepi_3_defconfig, tbs_a711_defconfig, pinephone_defconfig).

My u-boot branch already has all the necessary patches integrated and is configured for quick u-boot/kernel startup.

    make -C u-boot orangepi_pc2_defconfig
    make -C u-boot -j5
    
    cp $KBUILD_OUTPUT/.config $OUT/pc2/uboot.config
    cat $KBUILD_OUTPUT/{spl/sunxi-spl.bin,u-boot.itb} > $OUT/pc2/uboot.bin

Get kernel from this repository and checkout the latest orange-pi-5.13 branch.

Build the kernel for 64-bit boards:

    export ARCH=arm64
    export CROSS_COMPILE=aarch64-linux-gnu-
    export KBUILD_OUTPUT=$OUT/.tmp/linux-arm64
    mkdir -p $KBUILD_OUTPUT $OUT/pc2

    make -C linux orangepi_defconfig
    # or make -C linux pocketbook_touch_lux_3_defconfig
    # or make -C linux tbs_a711_defconfig
    make -C linux -j5 clean
    make -C linux -j5 Image dtbs

    cp -f $KBUILD_OUTPUT/arch/arm64/boot/Image $OUT/pc2/
    cp -f $KBUILD_OUTPUT/.config $OUT/pc2/linux.config
    cp -f $KBUILD_OUTPUT/arch/arm64/boot/dts/allwinner/sun50i-h5-orangepi-pc2.dtb $OUT/pc2/board.dtb

Build the kernel for 32-bit boards:

    export ARCH=arm
    export CROSS_COMPILE=arm-linux-gnueabihf-
    export KBUILD_OUTPUT=$OUT/.tmp/linux-arm
    mkdir -p $KBUILD_OUTPUT $OUT/pc

    make orangepi_defconfig
    # or make pinephone_defconfig
    make -C linux orangepi_defconfig
    make -C linux -j5 clean
    make -C linux -j5 zImage dtbs
    
    cp -f $KBUILD_OUTPUT/arch/arm/boot/zImage $OUT/pc/
    cp -f $KBUILD_OUTPUT/.config $OUT/pc/linux.config
    cp -f $KBUILD_OUTPUT/arch/arm/boot/dts/sun8i-h3-orangepi-pc.dtb $OUT/pc/board.dtb
    # Or use sun8i-h3-orangepi-one.dtb for Orange Pi One


PinePhone
---------

I don't run u-boot on PinePhone, so my pre-built kernel packages don't come
with u-boot built for PinePhone.


Kernel lockup issues
--------------------

*If you're getting lockups on boot or later during thermal regulation,
you're missing an u-boot patch.*

This patch is necessary to run this kernel!

These lockups are caused by improper NKMP clock factors selection
in u-boot for PLL_CPUX. (M divider should not be used. P divider
should be used only for frequencies below 240MHz.)

This patch for u-boot fixes it:

  0001-sunxi-h3-Fix-PLL1-setup-to-never-use-dividers.patch

Kernel side is already fixed in this kernel tree.

----------------------------
# How do I submit patches to Android Common Kernels

1. BEST: Make all of your changes to upstream Linux. If appropriate, backport to the stable releases.
   These patches will be merged automatically in the corresponding common kernels. If the patch is already
   in upstream Linux, post a backport of the patch that conforms to the patch requirements below.
   - Do not send patches upstream that contain only symbol exports. To be considered for upstream Linux,
additions of `EXPORT_SYMBOL_GPL()` require an in-tree modular driver that uses the symbol -- so include
the new driver or changes to an existing driver in the same patchset as the export.
   - When sending patches upstream, the commit message must contain a clear case for why the patch
is needed and beneficial to the community. Enabling out-of-tree drivers or functionality is not
not a persuasive case.

2. LESS GOOD: Develop your patches out-of-tree (from an upstream Linux point-of-view). Unless these are
   fixing an Android-specific bug, these are very unlikely to be accepted unless they have been
   coordinated with kernel-team@android.com. If you want to proceed, post a patch that conforms to the
   patch requirements below.

# Common Kernel patch requirements

- All patches must conform to the Linux kernel coding standards and pass `scripts/checkpatch.pl`
- Patches shall not break gki_defconfig or allmodconfig builds for arm, arm64, x86, x86_64 architectures
(see  https://source.android.com/setup/build/building-kernels)
- If the patch is not merged from an upstream branch, the subject must be tagged with the type of patch:
`UPSTREAM:`, `BACKPORT:`, `FROMGIT:`, `FROMLIST:`, or `ANDROID:`.
- All patches must have a `Change-Id:` tag (see https://gerrit-review.googlesource.com/Documentation/user-changeid.html)
- If an Android bug has been assigned, there must be a `Bug:` tag.
- All patches must have a `Signed-off-by:` tag by the author and the submitter

Additional requirements are listed below based on patch type

## Requirements for backports from mainline Linux: `UPSTREAM:`, `BACKPORT:`

- If the patch is a cherry-pick from Linux mainline with no changes at all
    - tag the patch subject with `UPSTREAM:`.
    - add upstream commit information with a `(cherry picked from commit ...)` line
    - Example:
        - if the upstream commit message is
```
        important patch from upstream

        This is the detailed description of the important patch

        Signed-off-by: Fred Jones <fred.jones@foo.org>
```
>- then Joe Smith would upload the patch for the common kernel as
```
        UPSTREAM: important patch from upstream

        This is the detailed description of the important patch

        Signed-off-by: Fred Jones <fred.jones@foo.org>

        Bug: 135791357
        Change-Id: I4caaaa566ea080fa148c5e768bb1a0b6f7201c01
        (cherry picked from commit c31e73121f4c1ec41143423ac6ce3ce6dafdcec1)
        Signed-off-by: Joe Smith <joe.smith@foo.org>
```

- If the patch requires any changes from the upstream version, tag the patch with `BACKPORT:`
instead of `UPSTREAM:`.
    - use the same tags as `UPSTREAM:`
    - add comments about the changes under the `(cherry picked from commit ...)` line
    - Example:
```
        BACKPORT: important patch from upstream

        This is the detailed description of the important patch

        Signed-off-by: Fred Jones <fred.jones@foo.org>

        Bug: 135791357
        Change-Id: I4caaaa566ea080fa148c5e768bb1a0b6f7201c01
        (cherry picked from commit c31e73121f4c1ec41143423ac6ce3ce6dafdcec1)
        [joe: Resolved minor conflict in drivers/foo/bar.c ]
        Signed-off-by: Joe Smith <joe.smith@foo.org>
```

## Requirements for other backports: `FROMGIT:`, `FROMLIST:`,

- If the patch has been merged into an upstream maintainer tree, but has not yet
been merged into Linux mainline
    - tag the patch subject with `FROMGIT:`
    - add info on where the patch came from as `(cherry picked from commit <sha1> <repo> <branch>)`. This
must be a stable maintainer branch (not rebased, so don't use `linux-next` for example).
    - if changes were required, use `BACKPORT: FROMGIT:`
    - Example:
        - if the commit message in the maintainer tree is
```
        important patch from upstream

        This is the detailed description of the important patch

        Signed-off-by: Fred Jones <fred.jones@foo.org>
```
>- then Joe Smith would upload the patch for the common kernel as
```
        FROMGIT: important patch from upstream

        This is the detailed description of the important patch

        Signed-off-by: Fred Jones <fred.jones@foo.org>

        Bug: 135791357
        (cherry picked from commit 878a2fd9de10b03d11d2f622250285c7e63deace
         https://git.kernel.org/pub/scm/linux/kernel/git/foo/bar.git test-branch)
        Change-Id: I4caaaa566ea080fa148c5e768bb1a0b6f7201c01
        Signed-off-by: Joe Smith <joe.smith@foo.org>
```


- If the patch has been submitted to LKML, but not accepted into any maintainer tree
    - tag the patch subject with `FROMLIST:`
    - add a `Link:` tag with a link to the submittal on lore.kernel.org
    - add a `Bug:` tag with the Android bug (required for patches not accepted into
a maintainer tree)
    - if changes were required, use `BACKPORT: FROMLIST:`
    - Example:
```
        FROMLIST: important patch from upstream

        This is the detailed description of the important patch

        Signed-off-by: Fred Jones <fred.jones@foo.org>

        Bug: 135791357
        Link: https://lore.kernel.org/lkml/20190619171517.GA17557@someone.com/
        Change-Id: I4caaaa566ea080fa148c5e768bb1a0b6f7201c01
        Signed-off-by: Joe Smith <joe.smith@foo.org>
```

## Requirements for Android-specific patches: `ANDROID:`

- If the patch is fixing a bug to Android-specific code
    - tag the patch subject with `ANDROID:`
    - add a `Fixes:` tag that cites the patch with the bug
    - Example:
```
        ANDROID: fix android-specific bug in foobar.c

        This is the detailed description of the important fix

        Fixes: 1234abcd2468 ("foobar: add cool feature")
        Change-Id: I4caaaa566ea080fa148c5e768bb1a0b6f7201c01
        Signed-off-by: Joe Smith <joe.smith@foo.org>
```

- If the patch is a new feature
    - tag the patch subject with `ANDROID:`
    - add a `Bug:` tag with the Android bug (required for android-specific features)


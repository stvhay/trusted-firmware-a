#!/bin/bash
#
# Copyright (c) 2016-2020, ARM Limited and Contributors. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#


set -e
set +x

# Define known paths
BASE_DIR=$(pwd)
MODEL_DIR=$BASE_DIR/fvp/Base_RevC_AEMv8A_pkg/models/Linux64_GCC-6.4/
OUT=$BASE_DIR/out
TOOL_CHAINS_DIR=$BASE_DIR/toolchains

PREBUILTS_DIR=${BASE_DIR}/prebuilts

USAGE="$(basename "$0") - A build script to compile and run OP-TEE xtests on FVP prototype using FF-A.

The following parameters are valid:
    -e Rebuild the entire OP-TEE checkout, used to update the ramdisk.
    -d Specify the FVP should wait for a debugger to connect before booting
    -h Show this help
    -j <num> Specify the number of threads to uses. Defaults to max available on system.
    -k Specify to rebuild the linux kernel. Not performed by default to reduce time overhead.
    -s Single core platform simulation (default uses 8 core declared in the dtb).
    -r Do not recompile, only run FVP with existing files

    # Directory Parameters
    -A <ATF_dir> Specify the path to your ATF directory. Defaults to '$BASE_DIR/arm-trusted-firmware'
    -H <*.bin|directory> BL33 image to use. Can be either a prebuilt bin image for BL33 or a hafnium directory to build. Defaults to '$BASE_DIR/hafnium'
    -O <optee_os_dir> Specify the path to your optee_os directory. Defaults to '$BASE_DIR/optee_os'
    -E <optee_checkout> Specify a path to a full optee checkout, used for rebuilding the ramdisk. Defaults to '$BASE_DIR/optee_full'
    -L <linux_dir> Specify the path to your linux directory. Defaults to '$BASE_DIR/linux_kernel'
    -T <toolchains_dir> Specify path to OP-TEE toolchains directory. Defauls to '$BASE_DIR/toolchains'

    # Development Options
    -B Build only mode, do not run the FVP after compilation
    -C Enable cache state modeling
    -c Clean each repository before building.
    -v Verbose mode for build script
    -x Disable the xterm display of uart outputs, allowing for use via telnet connections.
    -X Use tmux to auto connect to telnet connections for uart outputs.
"

MULTI_CORE_DTB=1
RUN_ONLY=0
BUILD_ONLY=0
DEBUG=0
RESET=
CLEAN=0
CACHE_STATE=0
NO_CPUS=$(nproc)
KERNEL_ARGS=0
VERBOSE=0
OPTEE_FULL=0
BUILD_HAFNIUM=1
BUILD_S_HAFNIUM=1
BUILD_LINUX_KERNEL=0

BL33_BIN=0
HAFNIUM_DIR=0
ATF_DIR=0
OPTEE_DIR=0
OPTEE_OS_DIR=0
OPTEE_LINUX_KERNEL_DIR=0

XTERM=1
TMUX=0

while getopts cdehij:krsuvxy:A:BE:H:L:O:ST:XY: option; do
    case "${option}" in

    h)
        echo "$USAGE"
        exit 0
        ;;
    A) ATF_DIR=${OPTARG} ;;
    B) BUILD_ONLY=1 ;;
    E) OPTEE_DIR=${OPTARG} ;;
    H) BL33_BIN=${OPTARG} ;;
    L) OPTEE_LINUX_KERNEL_DIR=${OPTARG} ;;
    O) OPTEE_OS_DIR=${OPTARG} ;;
    S) CACHE_STATE=1 ;;
    T) TOOL_CHAINS_DIR=${OPTARG} ;;
    X)
        TMUX=1
        XTERM=0
        ;;
    Y) BUILD_S_HAFNIUM=${OPTARG} ;;
    c) CLEAN=1 ;;
    d) DEBUG=1 ;;
    e) OPTEE_FULL=1 ;;
    k) BUILD_LINUX_KERNEL=1 ;;
    j) NO_CPUS=${OPTARG} ;;
    r) RUN_ONLY=1 ;;
    s) MULTI_CORE_DTB=0 ;;
    v) VERBOSE=1 ;;
    x) XTERM=0 ;;
    y) BUILD_HAFNIUM=${OPTARG} ;;
    esac
done

# Default for hafnium directory
if [ $HAFNIUM_DIR == 0 ]; then
    HAFNIUM_DIR=$BASE_DIR/hafnium
fi
# Default for atf directory
if [ $ATF_DIR == 0 ]; then
    ATF_DIR=$BASE_DIR/arm-trusted-firmware
fi

# Set DT dir
DTS_DIR=${ATF_DIR}/fdts/

# Default for optee_os directory
if [ $OPTEE_OS_DIR == 0 ]; then
    OPTEE_OS_DIR=$BASE_DIR/optee_os
fi
# Default for linux directory
if [ $OPTEE_LINUX_KERNEL_DIR == 0 ]; then
    OPTEE_LINUX_KERNEL_DIR=$BASE_DIR/linux_kernel
fi

# Default for full optee checkout directory
if [ $OPTEE_DIR == 0 ]; then
    OPTEE_DIR=$BASE_DIR/optee_full
fi

if [ $MULTI_CORE_DTB == 1 ]; then
    NORMAL_WORLD_DT="normal_world_multi"
else
    NORMAL_WORLD_DT="normal_world_single"
fi

# Provide default for not using Hyp in normal world.
if [ $BUILD_HAFNIUM == 0 ]; then
    BL33_BIN="$PREBUILTS_DIR/FVP_AARCH64_EFI.fd"
fi

# Get absolute paths
ATF_DIR=$(realpath -m $ATF_DIR)
HAFNIUM_DIR=$(realpath -m $HAFNIUM_DIR)
OPTEE_OS_DIR=$(realpath -m $OPTEE_OS_DIR)
OPTEE_LINUX_KERNEL_DIR=$(realpath -m $OPTEE_LINUX_KERNEL_DIR)
PREBUILTS_DIR=$(realpath -m $PREBUILTS_DIR)
BL33_BIN=$(realpath -m $BL33_BIN)
TOOL_CHAINS_DIR=$(realpath -m $TOOL_CHAINS_DIR)

function env_check() {

    # Check if prebuilts directory is present
    if [ ! -d $PREBUILTS_DIR ]; then
        clone_prebuilts
    fi
    # Check if toolchains directory is present
    if [ ! -d $TOOL_CHAINS_DIR ]; then
        check_toolchains
    fi

    # Check FVP is present
    if [ ! -d $MODEL_DIR ]; then
        check_fvp
    fi

    # Check if hafnium directory is present
    if [ ! -d $HAFNIUM_DIR ]; then
        echo "$HAFNIUM_DIR does not exist!"
        exit 1
    fi
    # Check if ATF directory is present
    if [ ! -d $ATF_DIR ]; then
        echo "$ATF_DIR does not exist!"
        exit 1
    fi
    # Check if optee_os directory is present
    if [ ! -d $OPTEE_OS_DIR ]; then
        echo "$OPTEE_OS_DIR does not exist!"
        exit 1
    fi
    # Check if full optee directory is present
    if [ $OPTEE_FULL == 1 ] && [ ! -d $OPTEE_DIR ]; then
        echo "$OPTEE_DIR does not exist!"
        exit 1
    fi
    # Check if linux kernel directory is present
    if [[ ! -d $OPTEE_LINUX_KERNEL_DIR ]]; then
        echo "$OPTEE_LINUX_KERNEL_DIR does not exist!"
        exit 1
    fi
    # Check if dts directory is present
    if [ ! -d $DTS_DIR ]; then
        echo "$DTS_DIR does not exist!"
        exit 1
    fi

    # Create output directory
    mkdir -p $OUT

    if [ ! $(which cpio) ]; then
        echo "cpio" command not found. Please install "cpio".
        exit 1
    fi

    if [ ! $(which dtc) ]; then
        echo "dtc" command not found. Please install "device-tree-compiler".
        exit 1
    fi
}

function check_fvp() {
    printf "Could not find fvp at $MODEL_DIR\nPlease download the FVPBaseRevC Model.\n"
    exit 1
}

function check_toolchains() {

    # Check if prebuilts directory is present
    if [ ! -d $TOOL_CHAINS_DIR ]; then

        if [ -d $OPTEE_DIR/toolchains/aarch32 ]; then
            mkdir $TOOL_CHAINS_DIR || true
            echo $TOOL_CHAINS_DIR
            cp -r $OPTEE_DIR/toolchains/aarch32 $OPTEE_DIR/toolchains/aarch64 $TOOL_CHAINS_DIR/
        else
            printf "Could not find toolchains at $TOOL_CHAINS_DIR\nPlease do a full build of OP-TEE with and ensure the paths are correct.\n"
            exit 1
        fi
    fi
}

function clone_prebuilts() {
    # Check if prebuilts directory is present
    if [ ! -d $PREBUILTS_DIR ]; then
        if [ -f $BASE_DIR/prebuilts.tar.gz ]; then
            mkdir -p $PREBUILTS_DIR
            tar -xvf prebuilts.tar.gz -C $PREBUILTS_DIR
        elif [ -f $OPTEE_DIR/out-br/images/rootfs.cpio.gz ] &&
            [ -f $OPTEE_DIR/edk2-platforms/Build/ArmVExpress-FVP-AArch64/RELEASE_GCC49/FV/FVP_AARCH64_EFI.fd ]; then
            mkdir -p $PREBUILTS_DIR
            cp $OPTEE_DIR/out-br/images/rootfs.cpio.gz $PREBUILTS_DIR/optee_rd.img
            cp $OPTEE_DIR/edk2-platforms/Build/ArmVExpress-FVP-AArch64/RELEASE_GCC49/FV/FVP_AARCH64_EFI.fd $PREBUILTS_DIR/FVP_AARCH64_EFI.fd
            cp $OPTEE_DIR/out/boot-fat.uefi.img $PREBUILTS_DIR/boot-fat.uefi.img
        fi
    else
        printf "Could not find prebuilts!\nPlease do a full build of OP-TEE with and ensure the paths are correct.\n"
        exit 1
    fi
}

function clone_optee_full() {
    # Checkout and Build
    mkdir -p $OPTEE_DIR
    cd $OPTEE_DIR
    mkdir $OPTEE_DIR/Foundation_Platformpkg || true
    repo init -u https://github.com/OP-TEE/manifest.git -m fvp.xml
    repo sync -j4 --no-clone-bundle
    cd $OPTEE_DIR/build
    make -j2 toolchains
    make -j$NO_CPUS
}

function build_linux_kernel() {
    cd $OPTEE_LINUX_KERNEL_DIR

    ARCH=arm64

    make ARCH=$ARCH CROSS_COMPILE="${TOOL_CHAINS_DIR}/aarch64/bin/aarch64-linux-gnu-" defconfig -j$NO_CPUS
    make ARCH=$ARCH CROSS_COMPILE="${TOOL_CHAINS_DIR}/aarch64/bin/aarch64-linux-gnu-" -j$NO_CPUS

    cp arch/arm64/boot/Image $OUT/
}

# Rebuild op-tee os with prototype flags
function build_optee_os() {
    cd $BASE_DIR/optee_os
    if [ $CLEAN == 1 ]; then
        make clean
        rm -rf out
    fi

    # Build under S-EL2 or S-EL1
    if [ $BUILD_S_HAFNIUM == 1 ]; then
        SEL2=y
        SEL1=n
    else
        SEL2=n
        SEL1=y
    fi

    CROSS_COMPILE="${TOOL_CHAINS_DIR}/aarch64/bin/aarch64-linux-gnu-" \
        make CFG_ARM64_core=y CROSS_COMPILE="${TOOL_CHAINS_DIR}/aarch64/bin/aarch64-linux-gnu-" \
        CROSS_COMPILE_core="${TOOL_CHAINS_DIR}/aarch64/bin/aarch64-linux-gnu-" \
        CROSS_COMPILE_ta_arm64=${TOOL_CHAINS_DIR}/aarch64/bin/aarch64-linux-gnu- \
        CROSS_COMPILE_ta_arm32=${TOOL_CHAINS_DIR}/aarch32/bin/arm-linux-gnueabihf- \
        CFG_TEE_CORE_LOG_LEVEL=3 DEBUG=1 CFG_TEE_BENCHMARK=n PLATFORM=vexpress-fvp CFG_ARM_GICV3=y \
        CFG_CORE_BGET_BESTFIT=y \
        CFG_ARM64_core=y CFG_CORE_SEL2_SPMC=$SEL2 CFG_CORE_SEL1_SPMC=$SEL1
}

# Rebuild ATF FIP
function build_ATF-FIP() {
    cd $ATF_DIR
    # Always fully clean before rebuild.
    make realclean

    # Specify a SP layout file if we're building with SPM in S-EL2
    SP_LAYOUT=""
    if [ $BUILD_S_HAFNIUM == 1 ]; then
        SP_LAYOUT="SP_LAYOUT_FILE=sp_layout.json"
    fi

    make -j$NO_CPUS CROSS_COMPILE="${TOOL_CHAINS_DIR}/aarch64/bin/aarch64-linux-gnu-" \
        SPD=spmd \
        CTX_INCLUDE_EL2_REGS=$BUILD_S_HAFNIUM \
        SPMD_SPM_AT_SEL2=$BUILD_S_HAFNIUM \
        PLAT=fvp \
        BL33=$BL33_BIN \
        DEBUG=1 \
        BL32=$BL32_BIN \
        ARM_ARCH_MINOR=4 \
        FVP_NT_FW_CONFIG=$OUT/${NORMAL_WORLD_DT}.dtb \
        $SP_LAYOUT all fip

    cp $ATF_DIR/build/fvp/debug/fip.bin $OUT
    cp $ATF_DIR/build/fvp/debug/bl1.bin $OUT
}

function build_hafnium() {
    cd $HAFNIUM_DIR
    if [ $CLEAN == 1 ]; then
        make clean || true
        cd third_party/linux
        make mrproper
    fi
    cd $HAFNIUM_DIR
    CROSS_COMPILE="${TOOL_CHAINS_DIR}/aarch64/bin/aarch64-linux-gnu-" make
    cp out/reference/aem_v8a_fvp_clang/hafnium.bin $OUT
    cp out/reference/secure_aem_v8a_fvp_clang/hafnium.bin $OUT/secure_hafnium.bin

    # Choose appropriate BL33 image depending if we want N-Hyp.
    if [ $BUILD_HAFNIUM == 1 ]; then
        BL33_BIN=$OUT/hafnium.bin
    fi

    # Choose appropriate BL32 image depending if we want S-Hyp.
    if [ $BUILD_S_HAFNIUM == 1 ]; then
        BL32_BIN="$OUT/secure_hafnium.bin"
    else
        BL32_BIN="$OPTEE_OS_DIR/out/arm-plat-vexpress/core/tee-pager_v2.bin"
    fi

}

function build_optee_ramdisk() {
    # Copy from OP-TEE or use prebuilt.
    cd $OUT
    if [ $OPTEE_FULL == 1 ]; then
        cp $OPTEE_DIR/out-br/images/rootfs.cpio.gz $PREBUILTS_DIR/optee_rd.img
    fi

    cp $PREBUILTS_DIR/optee_rd.img $OUT/optee_rd.img
}

function build_hafnium_ramdisk() {
    cd $OUT
    if [ -d "initrd" ]; then
        rm -rf initrd
    fi
    mkdir initrd
    cd initrd

    # Using dtb for vm description.
    build_dtb $DTS_DIR/inner_initrd.dts

    cp $OUT/inner_initrd.dtb manifest.dtb
    cp $OUT/optee_rd.img ./initrd.img
    cp $OUT/Image vmlinuz

    # The manifest file must be passed first to prevent alignment issues
    # until we move to libfdt.
    printf "manifest.dtb\ninitrd.img\nvmlinuz" | cpio -o >../initrd.img
    cd ..
    rm -rf initrd
}

function build_secure_hafnium_ramdisk() {
    cd $OUT
    if [ -d "secure_initrd" ]; then
        rm -rf secure_initrd
    fi

    mkdir secure_initrd
    cd secure_initrd

    cp $DTS_DIR/secure_world_dt.dts manifest.dts
    dtc -I dts -O dtb manifest.dts > manifest.dtb

    cp $PREBUILTS_DIR/optee_rd.img ./initrd.img

    # Copy OP-TEE and baremetal SP binaries.
    # According to the Secure world manifest, the baremetal SP is a secondary SP and OPTEE is the "primary"
    cp $HAFNIUM_DIR/out/reference/secure_aem_v8a_fvp_vm_clang/obj/test/secure_world_baremetal/secure_world_sp.bin bare_metal_sp
    cp ${OPTEE_OS_DIR}/out/arm-plat-vexpress/core/tee-pager_v2.bin optee

    # The manifest file must be passed first to prevent alignment issues
    # until we move to libfdt.
    printf "manifest.dtb\ninitrd.img\noptee\nbare_metal_sp" | cpio -o >../secure_initrd.img
    cd ..
    rm -rf secure_initrd
}

function build_dtb() {

    SOURCE_DTS=$DTS_DIR/${NORMAL_WORLD_DT}.dts

    if [[ -n $1 ]]; then
        SOURCE_DTS=$1
    fi
    # Ouput file in $OUT/<source>.dtb
    dtc -O dtb -o $OUT/"$(basename $SOURCE_DTS .dts).dtb" $SOURCE_DTS
}

function create_dtb() {
    cd $OUT
    cp $DTS_DIR/${NORMAL_WORLD_DT}.dts ${NORMAL_WORLD_DT}.dts

    START_ADDR=0x84000000
    FILESIZE=$(stat -c%s "$OUT/initrd.img")
    END_ADDR=$(python -c "print(hex($START_ADDR+$FILESIZE))")

    if [ $KERNEL_ARGS == 0 ]; then
        KERNEL_ARGS="rdinit=/sbin/init"
    fi

    echo """
    / {

        spci {
           compatible = \"arm,ffa\";
           conduit = \"smc\";
           /* \"tx\" or \"allocate\" */
           mem_share_buffer = \"tx\";
        };
        chosen {
           linux,initrd-start = <$START_ADDR>;
           linux,initrd-end = <$END_ADDR>;
           stdout-path = \"serial0:115200n8\";
           bootargs =  \"cpuidle.off=1\";

       };
       hypervisor {
           compatible = \"hafnium,hafnium\";
           vm1 {
               debug_name = \"linux_test\";
               kernel_filename = \"vmlinuz\";
               ramdisk_filename = \"initrd.img\";
               uuid = <0x0000 0x0 0x0 0x1>;
               messaging_method = <0x2>;
               smc_whitelist_permissive;
           };
       };
    };""" >> $OUT/${NORMAL_WORLD_DT}.dts
    cd $OUT

    build_dtb $OUT/${NORMAL_WORLD_DT}.dts
}

function modify_eufi_image() {
    cd $OUT
    cp $PREBUILTS_DIR/boot-fat.uefi.img $OUT/boot-fat.uefi.img

    # Modify the the grub config to supply the expected device tree path and ensure cpu idle is disabled.
    echo """
set prefix='/EFI/BOOT'

set default="0"
set timeout=5

menuentry 'GNU/Linux (OP-TEE)' {
    linux /Image console=tty0 console=ttyAMA0,115200 earlycon=pl011,0x1c090000 root=/dev/disk/by-partlabel/system rootwait rw ignore_loglevel efi=noruntime cpuidle.off=1
    initrd /initrd.img
    devicetree /fvp-base-gicv3-psci.dtb
}""" > $OUT/grub.cfg

    cp $OUT/${NORMAL_WORLD_DT}.dtb fvp-base-gicv3-psci.dtb
    cp fvp-base-gicv3-psci.dtb foundation-v8-gicv3-psci.dtb
    mcopy -o -i boot-fat.uefi.img Image ::
    mcopy -o -i boot-fat.uefi.img fvp-base-gicv3-psci.dtb ::
    mcopy -o -i boot-fat.uefi.img foundation-v8-gicv3-psci.dtb ::
    mcopy -o -i boot-fat.uefi.img grub.cfg ::EFI/BOOT/
}

# Main compilation workflow
function compile() {

    if [ $BUILD_LINUX_KERNEL == 1 ] || [ ! -f $OUT/Image ]; then
        printf "\nBuild Linux Kernel...\n"
        build_linux_kernel
    fi

    # Build Hafnium & linux and generate ramdisks
    printf "\nBuild Hafnium...\n"
    build_hafnium
    printf "\nBuild OP-TEE Ramdisk...\n"
    build_optee_ramdisk
    printf "\nBuild Hafnium Ramdisk...\n"
    build_hafnium_ramdisk
    printf "\nCreate DTB...\n"
    create_dtb

    # Update eufi image
    if [ $BUILD_HAFNIUM == 0 ]; then
        modify_eufi_image
    fi
    printf "\nBuild OP-TEE OS...\n"
    build_optee_os
    printf "\nBuild Secure-Hafnium Ramdisk...\n"
    build_secure_hafnium_ramdisk
    printf "\nBuild ATF FIP...\n"
    build_ATF-FIP
}

# Run the FVP
function run() {
    # Allow for specifying additional arguments
    ARGS=""
    if [ $DEBUG == 1 ]; then
        ARGS="$ARGS -S"
    fi

    DTB=''
    if [[ $BUILD_HAFNIUM == 1 ]]; then
        BOOT_IMAGES="--data cluster0.cpu0=$OUT/initrd.img@0x84000000"
        DTB="--data cluster0.cpu0=$OUT/${NORMAL_WORLD_DT}.dtb@0x80000000"
    else
        BOOT_IMAGES="-C bp.virtioblockdevice.image_path=$OUT/boot-fat.uefi.img"
    fi

    if [ $BUILD_S_HAFNIUM == 1 ]; then
        BOOT_IMAGES="${BOOT_IMAGES} --data cluster0.cpu0=$OUT/secure_initrd.img@0xA000000"
    fi

    cd $BASE_DIR
    FVP_INVOCATION="""${MODEL_DIR}/FVP_Base_RevC-2xAEMv8A \
    -C pctl.startup=0.0.0.0 \
    -C bp.secure_memory=0 \
    -C cluster0.NUM_CORES=4 \
    -C cluster1.NUM_CORES=4 \
    -C cache_state_modelled=$CACHE_STATE \
    -C bp.pl011_uart0.untimed_fifos=1 \
    -C bp.pl011_uart0.unbuffered_output=1 \
    -C bp.pl011_uart1.untimed_fifos=1 \
    -C bp.pl011_uart1.unbuffered_output=1 \
    -C bp.pl011_uart0.out_file=$OUT/uart0.log \
    -C bp.pl011_uart1.out_file=$OUT/uart1.log \
    -C bp.terminal_0.start_telnet=$XTERM \
    -C bp.terminal_1.start_telnet=$XTERM \
    -C bp.vis.disable_visualisation=$((1 - XTERM)) \
    -C bp.secureflashloader.fname=$OUT/bl1.bin \
    -C bp.flashloader0.fname=$OUT/fip.bin \
    -C bp.ve_sysregs.mmbSiteDefault=0 \
    -C bp.ve_sysregs.exit_on_shutdown=1 \
    -C cluster0.has_arm_v8-4=1 \
    -C cluster1.has_arm_v8-4=1 \
    ${BOOT_IMAGES} ${DTB} ${ARGS} """

    if [ $TMUX == 0 ]; then
        $FVP_INVOCATION
    else
        DELAY=2
        export TMUX=''
        tmux new-session \; \
            send-keys """trap 'tmux kill-session' SIGINT
            ${FVP_INVOCATION}""" C-m \; \
            split-window -v \; \
            send-keys "sleep $DELAY && telnet 0 5000" C-m \; \
            split-window -h \; \
            send-keys "sleep $DELAY && telnet 0 5001" C-m \; \
            select-pane -t 1\;
    fi
}

#################################################################################################
#                                               Main                                            #
#################################################################################################

# Run only mode
if [ $RUN_ONLY == 1 ]; then
    printf "Running FVP...\n"
    run
    exit 0
fi

# Perform full build of OP-TEE
if [ $OPTEE_FULL == 1 ]; then
    printf "Building OP-TEE...\n"
    clone_optee_full
fi

printf "Checking the Environment...\n"
env_check

# Enable verbose mode
if [ $VERBOSE == 1 ]; then
    set -x
fi

printf "Compiling...\n"
compile
printf "\nBuild Completed!\n\n"
if [ $BUILD_ONLY == 0 ]; then
    printf "Running FVP...\n"
    run
fi

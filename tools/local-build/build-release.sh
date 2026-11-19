#!/bin/bash
# X6100 Accessibility 1.2.0.N: in-radio updates (python3 ssl, CA certificates, openssl CLI), full clean image,
# U-Boot patch, signed update bundle. Usage: main_build.sh VERSION (e.g. 1.2.0.1)
set -o pipefail
V=${1:?version}
B=~/x6100/AetherX6100Buildroot
D=$B/br2_external/configs/X6100_defconfig
G=/mnt/c/claude/x6100_testing/gui
OUT=~/x6100/release-$V
mkdir -p $OUT
for o in PYTHON3_SSL PYTHON3_ZLIB CA_CERTIFICATES LIBOPENSSL_BIN; do
    grep -q "^BR2_PACKAGE_$o=y" $D || echo "BR2_PACKAGE_$o=y" >> $D
done
grep -n "PYTHON3_SSL\|PYTHON3_ZLIB\|CA_CERT\|LIBOPENSSL_BIN" $D
cd $B && bash ./br_config.sh > /dev/null || exit 1
grep -E "^BR2_PACKAGE_(PYTHON3_SSL|PYTHON3_ZLIB|CA_CERTIFICATES|LIBOPENSSL_BIN)=" build/.config
cd $B/build
grep -q "$V" $G/src/main.h || { echo "main.h is not at $V"; exit 1; }
make python3-dirclean x6100-gui-dirclean
rm -rf build/buildroot-fs images/rootfs.ext2 images/rootfs.ext4 images/sdcard.img images/boot.vfat
make lame mpg123 && make libsndfile-dirclean && make
echo "MAKE_EXIT=$?"
dd if=$G/u-boot/u-boot-sunxi-with-spl.bin of=images/sdcard.img seek=8K bs=1 conv=notrunc status=none
echo "== checks"
T=$B/build/target
strings images/sdcard.img | grep -m1 "U-Boot 20"
cat $T/usr/share/x6100/app-version
ls -la $T/usr/sbin/x6100-update $T/usr/bin/openssl $T/etc/ssl/certs/ca-certificates.crt
ls $T/usr/lib/python3.11/lib-dynload | grep -E "_ssl|zlib"
head -c 12 $T/usr/sbin/x6100_daemon | od -c | head -1
ls $T/usr/share/RHVoice/voices
M=$(ls -d $B/build/build/x6100-gui-*/install_manifest.txt | head -1)
python3 $G/tools/update/mkbundle.py --target $T --manifest $M --version $V \
    --out $OUT/X6100-Accessibility-v$V-update.tar.gz --key ~/.config/openx6x00/update-key.pem
cp images/sdcard.img $OUT/sdcard.img
cp $B/build/build/x6100-gui-*/x6100_gui $OUT/x6100_gui.unstripped 2>/dev/null
cd $OUT && rm -f sdcard.img.zip && zip -q sdcard.img.zip sdcard.img && rm sdcard.img
sha256sum $OUT/*
echo "ALL_DONE"

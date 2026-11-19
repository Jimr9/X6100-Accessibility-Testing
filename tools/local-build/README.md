# Building X6100 Accessibility releases on a PC

Releases of this build are made on a PC (WSL2, Ubuntu 22.04), then published on GitHub. The radios update from the
GitHub release (Settings, F5 Update), whatever machine built it. Everything needed to set the PC up again is here.

## What is where

- This repository: the GUI's source, `tools/update/mkbundle.py` (the update bundle), `tools/local-build/`.
- `buildroot-local.patch`: the changes to the buildroot tree. The six speech voices, Python's SSL and zlib, the CA
  certificates and the `openssl` command (the in-radio updater needs the last four), and the GUI package built from
  this checkout instead of GitHub.
- `build-release.sh VERSION`: the whole release build (clean image, U-Boot patch, signed update bundle).
- **The signing key is NOT in any repository.** Its private half is `update-key.pem`; keep backups of it somewhere
  safe and private. On the build PC it is at `~/.config/openx6x00/update-key.pem` in WSL, and a copy at
  `C:\Users\Jim\.config\openx6x00-speech\update-key.pem`. Its public half is `rootfs/usr/share/x6100/update.pub`. The
  same key signs OpenX6X00 Speech Access. Without it no update can be made that existing radios accept: a new key
  would mean every radio writes a new card image once.

## Setting the PC up again

1. Windows: `wsl --install -d Ubuntu-22.04` (22.04 exactly: newer Ubuntu breaks this buildroot). User `jim`.
2. In WSL, `/etc/wsl.conf`:

       [interop]
       appendWindowsPath = false

   then `wsl --shutdown` (buildroot refuses a PATH with spaces).
3. Packages:

       sudo apt install -y build-essential wget cpio unzip rsync bc libncurses5-dev libncursesw5-dev screen file \
           cmake python3 python3-dev python3-setuptools python3-pip sqlite3 zip
       sudo pip3 install 'cmake==3.28.3'      # aether-x6100-control needs CMake >= 3.23

4. The buildroot tree, in WSL's own filesystem (much faster than /mnt/c):

       mkdir -p ~/x6100 && cd ~/x6100
       git clone --recurse-submodules https://github.com/OpenX6100/AetherX6100Buildroot.git
       cd AetherX6100Buildroot && git checkout e7d12dc && git submodule update --init
       git apply /mnt/c/claude/x6100_testing/gui/tools/local-build/buildroot-local.patch
       bash ./br_config.sh

   (e7d12dc is the commit the 1.2.0.1 release was built on. The patch's GUI path is this checkout's on the PC:
   `/mnt/c/claude/x6100_testing/gui`.)
5. The signing key back to `~/.config/openx6x00/update-key.pem` (`chmod 600`).
6. This repository checked out at `C:\claude\x6100_testing\gui`. `.gitattributes` keeps `rootfs/` LF: a CRLF shell
   script there stops the radio from starting its program.

## Making a release

1. Raise the version in `src/main.h` (`#define VERSION "v1.2.0.2"`): radios compare these numbers.
2. Commit and push to `testing` (this repository on GitHub).
3. In WSL: `bash /mnt/c/claude/x6100_testing/gui/tools/local-build/build-release.sh 1.2.0.2`. It ends with
   `ALL_DONE`; the files are in `~/x6100/release-1.2.0.2/`: `sdcard.img.zip` and
   `X6100-Accessibility-v1.2.0.2-update.tar.gz`. Check its "== checks" lines: U-Boot 2025.01-x6100, the version,
   `openssl`, `x6100-update`, the certificates, six voices, the bundle "signed".
   (A build option that changed needs that package rebuilt: `make <package>-dirclean` first.)
4. Publish: `gh release create accessibility-v1.2.0.2 --latest ... sdcard.img.zip X6100-Accessibility-v1.2.0.2-update.tar.gz`.
   It has to be the **Latest** release (not a pre-release): the radios ask GitHub for the latest one.
5. The tag starts the repository's GitHub Actions "Build image", which would put its own `sdcard.img.zip` (built
   without the patch above) over the release's: cancel it (`gh run list`, `gh run cancel <id>`).

## Building on GitHub instead, some day

The same release from GitHub Actions needs: the patch's defconfig lines added to the workflow's buildroot step, the
signing key as a repository secret (as RF.Guru's OpenX6X00 does: `UPDATE_SIGNING_KEY`, written to a file for the
build and deleted after), and the bundle step (`tools/update/mkbundle.py`) after the image. The version numbers,
the key and the radios stay the same.

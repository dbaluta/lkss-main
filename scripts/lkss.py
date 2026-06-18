#!/usr/bin/env python3

# Copyright 2026 NXP
#
# SPDX-License-Identifier: Apache-2.0

import os
import subprocess
import shutil
import sys
import shlex
import click

import lkss_util

from lkss_manifest import LKSSManifest
from lkss_env import env as lkss_env

from lkss_runner import lkss_get_runner

@click.group(context_settings={"help_option_names": ["-h", "--help"], "show_default": True})
def cli():
	"""LKSS utility tool"""
	if lkss_util.platform_name() == "WINDOWS":
		print("Native Windows not supported - please run in WSL")
		sys.exit(1)

@cli.command()
@click.option("--clean-config", is_flag=True, help="Set the configuration options to the defconfig values.")
def menuconfig(clean_config: bool):
	"""Open the menuconfig interface"""
	if not lkss_env.is_cached():
		print("Environment not initialized")
		sys.exit(1)

	kernel = os.path.join(lkss_env.data["REPOS_DIR"], lkss_env.data["LINUX_DIR"])
	command = f"make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -C {kernel} "

	if clean_config:
		commands = [command + lkss_env.data["DEFCONFIG_NAME"], command + "menuconfig"]
	else:
		commands = [command + "menuconfig"]

	lkss_get_runner(lkss_env.data["RUNNER"]).run_batch(commands)

@cli.command()
@click.option("-j", "--jobs", default=1, help="Number of threads to use.")
@click.option("--install-modules", is_flag=True, help="Copy the kernel modules to the rootfs.")
@click.option("--clean-config", is_flag=True, help="Set the configuration options to the defconfig values.")
def compile(jobs: int, install_modules: bool, clean_config: bool):
	"""Compile the Linux kernel"""
	if not lkss_env.is_cached():
		print("Environment not initialized")
		sys.exit(1)

	kernel = os.path.join(lkss_env.data["REPOS_DIR"], lkss_env.data["LINUX_DIR"])
	image = os.path.join(kernel, "arch/arm64/boot/Image")
	dtb = os.path.join(kernel, "arch/arm64/boot/dts/freescale", lkss_env.data["DTB_NAME"])

	command = f"make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -C {kernel} -j{jobs} "

	if clean_config:
		commands = [command + lkss_env.data["DEFCONFIG_NAME"], command]
	else:
		# olddefconfig fills in any new Kconfig symbols with their default
		# value non-interactively, so a stale .config never drops into the
		# "Restart config..." prompt loop that make would otherwise show.
		commands = [command + "olddefconfig", command]

	lkss_get_runner(lkss_env.data["RUNNER"]).run_batch(commands)

	# done, create output directory and copy Image/DTB
	if not os.path.isdir(lkss_env.data["OUTPUT_DIR"]):
		os.makedirs(lkss_env.data["OUTPUT_DIR"])

	shutil.copy(image, lkss_env.data["OUTPUT_DIR"])
	shutil.copy(dtb, lkss_env.data["OUTPUT_DIR"])

	if install_modules:
		do_modules_install()

@cli.command()
def boot():
	"""Boot the board"""
	if not lkss_env.is_cached():
		print("Environment not initialized")
		sys.exit(1)

	bin_path = os.path.join(os.getcwd(), lkss_env.data["BINARIES_DIR"])
	output_path = os.path.join(os.getcwd(), lkss_env.data["OUTPUT_DIR"])

	# assemble paths for all required binaries
	rootfs = os.path.join(bin_path, lkss_env.data["ROOTFS_NAME"])
	image = os.path.join(output_path, "Image")
	dtb = os.path.join(output_path, lkss_env.data["DTB_NAME"])
	container = os.path.join(bin_path, lkss_env.data["BOOT_CONTAINER_NAME"])
	script = lkss_env.data["BOOT_SCRIPT_PATH"]

	if lkss_util.platform_name() == "WSL":
		uuu = os.path.join(bin_path, lkss_env.data["WINDOWS_UUU_NAME"])
	else:
		uuu = os.path.join(bin_path, lkss_env.data["UNIX_UUU_NAME"])

	binaries = [rootfs, image, dtb, container, script, uuu]

	for binary in binaries:
		if not os.path.isfile(binary):
			print(f"{binary} not present - have you ran init?")
			return

	# uuu binary might not have X bit set, do it now
	lkss_util.set_executable(uuu)

	print(f"Booting the board using {script}")

	command = [uuu, "-b", script, container, rootfs, image, dtb]

	proc = subprocess.run(command)
	if proc.returncode != 0:
		print("Failed to boot the board")
		sys.exit(1)

@cli.command()
@click.argument("src")
@click.argument("dst")
def copy(src: str, dst: str):
	"""
	Copy file/directory (recursively) found at SRC to rootfs DST
	"""
	if not lkss_env.is_cached():
		print("Environment not initialized")
		sys.exit(1)

	rootfs = os.path.join(os.getcwd(), lkss_env.data["BINARIES_DIR"], lkss_env.data["ROOTFS_NAME"])
	mount = os.path.join(os.getcwd(), lkss_env.data["ROOTFS_MOUNT_DIR"])
	lkss_util.copy_to_rootfs(mount, rootfs, src, dst)

def do_modules_install():
	kernel = os.path.join(os.getcwd(), lkss_env.data["REPOS_DIR"], lkss_env.data["LINUX_DIR"])
	rootfs = os.path.join(os.getcwd(), lkss_env.data["BINARIES_DIR"], lkss_env.data["ROOTFS_NAME"])
	mount = os.path.join(os.getcwd(), lkss_env.data["ROOTFS_MOUNT_DIR"])

	if not lkss_util.mount_rootfs(rootfs, mount):
		print("Failed to mount rootfs")
		sys.exit(1)

	command = ["sudo", f"INSTALL_MOD_PATH={mount}", "make", "modules_install"]

	proc = subprocess.run(command, cwd=kernel)
	if proc.returncode != 0:
		print("Failed to install modules")
		lkss_util.unmount_rootfs(mount)
		sys.exit(1)

	lkss_util.unmount_rootfs(mount)

@cli.command()
@click.option("-f", "--force", is_flag=True, help="Force the initialization.")
@click.option("--config", help="Configuration file to use.")
@click.option(
	"--runner",
	type=click.Choice(["native", "docker"]),
	default="native",
	help="environment to use for development"
)
def init(runner: str, force: bool, config: str):
	"""Initialize the development environment"""
	if lkss_env.is_cached() and not force:
		print("Environment already initialized!")
		sys.exit(1)

	# needs to be done before anything else
	lkss_env.load_from_config(config)
	lkss_env.data["RUNNER"] = runner
	lkss_env.store()

	LKSSManifest().init()

	lkss_get_runner(runner).setup()

if __name__ == '__main__':
    cli()

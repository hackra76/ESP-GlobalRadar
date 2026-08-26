# type: ignore
# pylint: disable=undefined-variable
Import("env")
import os

def merge_binaries(source, target, env):
    print("Generujem spojený binárny súbor podľa vlastnej tabuľky partícií (partitions.csv)...")
    
    build_dir = env.subst("$BUILD_DIR")
    firmware_path = target[0].get_abspath()
    bootloader_path = os.path.join(build_dir, "bootloader.bin")
    partitions_path = os.path.join(build_dir, "partitions.bin")
    
    framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    boot_app0_path = os.path.join(framework_dir, "tools", "partitions", "boot_app0.bin")
    
    output_path = os.path.join(env.subst("$PROJECT_DIR"), "merged-firmware.bin")
    
    board_config = env.BoardConfig()
    flash_size = board_config.get("upload.flash_size", "4MB")
    flash_mode = board_config.get("build.flash_mode", "dio")
    flash_freq = board_config.get("build.flash_freq", "40m")
    
    # Adresy sú nastavené presne podľa stĺpca "Offset" v tvojom partitions.csv
    cmd = [
        "$PYTHONEXE",
        env.PioPlatform().get_package_dir("tool-esptoolpy") + "/esptool.py",
        "--chip", "esp32c3",
        "merge_bin",
        "-o", output_path,
        "--flash_mode", flash_mode,
        "--flash_freq", flash_freq,
        "--flash_size", flash_size,
        "0x0", bootloader_path,          # Bootloader
        "0x8000", partitions_path,       # Vygenerovaná tabuľka partícií
        "0xe000", boot_app0_path,        # otadata (z CSV: offset 0xe000)
        "0x10000", firmware_path         # app0 (z CSV: offset 0x10000)
    ]
    
    print("Spúšťam esptool s mapovaním...")
    env.Execute(env.VerboseAction(" ".join(cmd), "Merging binaries..."))
    print(f"Hotovo! Spojený súbor je uložený ako: {output_path}")

env.AddPostAction("$BUILD_DIR/firmware.bin", merge_binaries)
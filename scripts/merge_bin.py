# type: ignore
# pylint: disable=undefined-variable
Import("env")
import os
import shutil

def copy_merged_bin(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    factory_bin = os.path.join(build_dir, "firmware.factory.bin")
    output_path = os.path.join(env.subst("$PROJECT_DIR"), "merged-firmware.bin")
    if os.path.exists(factory_bin):
        try:
            shutil.copyfile(factory_bin, output_path)
            print(f"[MERGE] Merged binary copied to: {output_path}")
        except Exception as e:
            print(f"[MERGE] Error copying merged binary: {e}")

env.AddPostAction("$BUILD_DIR/firmware.factory.bin", copy_merged_bin)
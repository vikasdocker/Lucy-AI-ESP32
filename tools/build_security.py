# PlatformIO pre-build guard: never build credential-bearing public CI artifacts.
from pathlib import Path
import os
Import("env")
root = Path(env.subst("$PROJECT_DIR"))
if root.name == "offline":
    root = root.parent
header = root / "offline/src/lucy_device_config.h"
if os.environ.get("CI") and header.exists():
    raise RuntimeError("Refusing a public CI build with a private device credential header")

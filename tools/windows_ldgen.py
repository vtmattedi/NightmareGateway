"""Run ESP-IDF's long ldgen invocation without cmd.exe's 8191-character limit."""

import os
import subprocess

Import("env")

if os.name == "nt":
    original_spawn = env["SPAWN"]

    def spawn(sh, escape, cmd, args, process_env):
        # SCons has already quoted these arguments for Windows. Only ldgen is a
        # plain Python invocation here; keep normal shell handling for all other
        # build commands, including those that use redirection or response files.
        if any(os.path.basename(arg.strip('"')).lower() == "ldgen.py" for arg in args[1:3]):
            return subprocess.call(" ".join(args), env=process_env, shell=False)
        return original_spawn(sh, escape, cmd, args, process_env)

    env.Replace(SPAWN=spawn)

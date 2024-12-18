import etwtrace
import sys
from pathlib import Path

PTH_TEMPLATE = "import etwtrace; etwtrace.enable_if({!r}, {!r})"

HELP_TEXT = """Copyright (c) Microsoft Corporation. All rights reserved.

    Usage: python -m etwtrace [options] -- script.py ...

    Launches a script with tracing enabled.
    --stack             Select ETW stack sampling (default)
    --instrument        Select ETW instrumentation
    --capture <FILE>    Capture ETW events to specified file
                        (Requires elevation; will overwrite FILE)

    Usage: python -m etwtrace --enable [ENABLE_VAR] [TYPE_VAR]

    Configures tracing to automatically start when Python is launched.
    ENABLE_VAR          Environment variable to check (default: none)
    TYPE_VAR            Environment variable specifying trace type
                        (Valid types: stack, instrument)

    Usage: python -m etwtrace --disable

    Disables automatic start when Python is launched.

    Other options:
    -?, -h              Display help information
    --info              Display technical info for bug reports
    --profile           Display path to WPR profile file
    --stacktags         Display path to WPA stacktags file
"""

def main(args=sys.argv[1:]):
    args = ["-?"] if not args else list(args)
    unused_args = []
    tracer = None
    capture = None
    show_info = False

    while args:
        orig_arg = args.pop(0)
        arg = orig_arg.lower()

        if arg == "--":
            import runpy
            # Use the remainder as the real argv and run the specified script or module
            sys.argv[:] = args
            with (capture or NullContext()):
                with (tracer or etwtrace.StackSamplingTracer()):
                    if sys.argv[0] == "-m" and len(sys.argv) >= 2:
                        runpy.run_module(sys.argv.pop(1), run_name="__main__")
                    else:
                        runpy.run_path(sys.argv[0], run_name="__main__")
            break

        if arg in ("-h", "-?", "/h", "/?"):
            print("Microsoft etwtrace for Python Version", etwtrace.__version__)
            print(HELP_TEXT)
            unused_args.extend(args)
            break

        if arg in ("--stack", "/stack"):
            tracer = etwtrace.StackSamplingTracer()
        elif arg in ("--instrument", "--instrumented", "/instrument", "/instrumented"):
            tracer = etwtrace.InstrumentedTracer()
        elif arg in ("--diaghub", "/diaghub"):
            tracer = etwtrace.DiagnosticsHubTracer()
        elif arg in ("--diaghubtest", "/diaghubtest"):
            tracer = etwtrace.DiagnosticsHubTracer(stub=True)

        elif arg in ("--capture", "/capture") or arg.startswith(("--capture:", "/capture:")):
            try:
                file = orig_arg.partition(":")[-1] or args.pop(0)
            except IndexError:
                file = None
            if not file or file.startswith(("-", "/")):
                print("FILE argument required with --capture", file=sys.stderr)
                return 1
            try:
                capture = Wpr(file)
            except FileNotFoundError:
                print("Unable to locate wpr.exe. Set WPR_EXE to override.", file=sys.stderr)
                return 2

        elif arg in ("--profile", "/profile"):
            try:
                print(etwtrace.get_profile_path())
            except FileNotFoundError:
                print("Unable to locate WPR profile", file=sys.stderr)
                return 2
        elif arg in ("--stacktags", "/stacktags"):
            try:
                print(etwtrace.get_stacktags_path())
            except FileNotFoundError:
                print("Unable to locate WPA stacktags file", file=sys.stderr)
                return 2
        elif arg in ("--info", "/info"):
            show_info = True

        elif arg in ("--enable", "/enable"):
            import site
            v1 = ""
            v2 = None
            if args and not args[0].startswith(("-", "/")):
                v1 = args.pop(0)
                if args and not args[0].startswith(("-", "/")):
                    v2 = args.pop(0)
            v2 = v2 or (f"{v1}_TYPE" if v1 else "ETWTRACE_TYPE")
            pth_file = Path(site.getsitepackages()[0]) / "etwtrace.pth"
            with open(pth_file, "w", encoding="utf-8") as f_out:
                print(Path(etwtrace.__spec__.submodule_search_locations[0]).parent, file=f_out)
                print(PTH_TEMPLATE.format(v1, v2), file=f_out)
            print("Created", pth_file)
            if v1:
                print(f"Set %{v1}% to activate")
            print(f"Set %{v2}% to to 'instrumented' to use instrumented events rather than stacks")
            unused_args.extend(args)
            break

        elif arg in ("--disable", "/disable"):
            for p in map(Path, sys.path):
                pth_file = p / "etwtrace.pth"
                try:
                    pth_file.unlink()
                    print("Removed", pth_file)
                except FileNotFoundError:
                    pass
            unused_args.extend(args)
            break

        else:
            unused_args.append(orig_arg)

    if show_info:
        if not tracer:
            tracer = etwtrace.StackSamplingTracer()
        print(tracer._get_technical_info())

    if unused_args:
        print("WARNING: The following arguments were not used:", end=" ", flush=False, file=sys.stderr)
        print(*(f'"{a}"' if " " in a else a for a in unused_args), sep=" ", file=sys.stderr)
        return 1

    return 0


class NullContext:
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        pass


class Wpr:
    def __init__(self, file):
        import base64
        import os
        from pathlib import Path
        if os.getenv("WPR_EXE"):
            self.wpr = Path(os.getenv("WPR_EXE"))
        else:
            self.wpr = Path(os.getenv("SystemRoot")) / "System32" / "wpr.exe"
        if not self.wpr.is_file():
            raise FileNotFoundError(self.wpr)
        self.file = file
        self.profile = etwtrace.get_profile_path()
        self.profile_name = "Default"
        self.instance = base64.urlsafe_b64encode(os.urandom(16)).decode()
        if sys.winver.endswith("-arm64"):
            self.profile_name = "ARM64"
        try:
            os.unlink(self.file)
        except FileNotFoundError:
            pass

    def __enter__(self):
        import subprocess
        import time
        subprocess.check_call([
            self.wpr,
            "-start",
            f"{self.profile}!{self.profile_name}",
            "-instancename",
            self.instance,
        ])
        time.sleep(0.5)

    def __exit__(self, *exc):
        import subprocess
        import time
        time.sleep(0.5)
        subprocess.check_call([
            self.wpr,
            "-stop",
            self.file,
            "-compress",
            "-instancename",
            self.instance,
        ])
        print("Trace saved to", self.file)

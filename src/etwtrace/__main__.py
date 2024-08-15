import etwtrace
import sys
from pathlib import Path

from ._helpers import *

TRACER = None
CAPTURE = None
TEMP_PROFILE = None
SHOW_INFO = False

PTH_TEMPLATE = "import etwtrace; etwtrace.enable_if({!r}, {!r})"


args = sys.argv[1:]
unused_args = []

if not args:
    args = ['-?']

while args:
    orig_arg = args.pop(0)
    arg = orig_arg.lower()
    if arg == "--":
        # Use the remainder as the real argv and run the specified script or module
        sys.argv[:] = args
        tracer = TRACER or etwtrace.StackSamplingTracer()
        capture = CAPTURE or NullContext()
        import runpy
        with capture:
            with tracer:
                if sys.argv[0] == "-m" and len(sys.argv) >= 2:
                    runpy.run_module(sys.argv.pop(1), run_name="__main__")
                else:
                    runpy.run_path(sys.argv[0], run_name="__main__")
        break

    if arg in ("-h", "-?", "/h", "/?"):
        print("Microsoft etwtrace for Python Version", etwtrace.__version__)
        print("Copyright (c) Microsoft Corporation. All rights reserved.")
        print()
        print("    Usage: python -m etwtrace [options] -- script.py ...")
        print()
        print("    Launches a script with tracing enabled.")
        print("    --stack          Select ETW stack sampling (default)")
        print("    --instrument     Select ETW instrumentation")
        print("    --diaghub        Select Visual Studio integration")
        print("    --capture <FILE> Capture ETW events to specified file")
        print("                     (Requires elevation; will overwrite FILE)")
        print()
        print("    Usage: python -m etwtrace --enable [ENABLE_VAR] [TYPE_VAR]")
        print()
        print("    Configures tracing to automatically start when Python is launched.")
        print("    ENABLE_VAR       Environment variable to check (default: none)")
        print("    TYPE_VAR         Environment variable specifying trace type")
        print("                     (Valid types: stack, instrument, diaghub)")
        print()
        print("    Usage: python -m etwtrace --disable")
        print()
        print("    Disables automatic start when Python is launched.")
        print()
        print("    Other options:")
        print("    -?, -h           Display help information")
        print("    --info           Display technical info for bug reports")
        print("    --profile        Display path to WPR profile file")
        print("    --stacktags      Display path to WPA stacktags file")
        print()
        unused_args.extend(args)
        break

    if arg in ("--stack", "/stack"):
        TRACER = etwtrace.StackSamplingTracer()
    elif arg in ("--instrument", "--instrumented", "/instrument", "/instrumented"):
        TRACER = etwtrace.InstrumentedTracer()
    elif arg in ("--diaghub", "/diaghub"):
        TRACER = etwtrace.DiagnosticsHubTracer()
    elif arg in ("--diaghubtest", "/diaghubtest"):
        TRACER = etwtrace.DiagnosticsHubTracer(stub=True)

    elif arg in ("--capture", "/capture") or arg.startswith(("--capture:", "/capture:")):
        try:
            file = orig_arg.partition(":")[-1] or args.pop(0)
        except IndexError:
            file = None
        if not file or file.startswith(("-", "/")):
            print("FILE argument required with --capture", file=sys.stderr)
            sys.exit(1)
        try:
            CAPTURE = Wpr(file)
        except FileNotFoundError:
            print("Unable to locate wpr.exe. Set WPR_EXE to override.", file=sys.stderr)
            sys.exit(2)

    elif arg in ("--profile", "/profile"):
        try:
            print(get_profile_path())
        except FileNotFoundError:
            print("Unable to locate WPR profile", file=sys.stderr)
            sys.exit(2)
    elif arg in ("--stacktags", "/stacktags"):
        try:
            print(get_stacktags_path())
        except FileNotFoundError:
            print("Unable to locate WPA stacktags file", file=sys.stderr)
            sys.exit(2)
    elif arg in ("--info", "/info"):
        SHOW_INFO = True

    elif arg in ("--enable", "/enable"):
        v1 = ""
        v2 = None
        if args and not args[0].startswith(("-", "/")):
            v1 = args.pop(0)
            if args and not args[0].startswith(("-", "/")):
                v2 = args.pop(0)
        v2 = v2 or (f"{v1}_TYPE" if v1 else "ETWTRACE_TYPE")
        import site
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
        unused_args.append(arg)


if SHOW_INFO:
    if not TRACER:
        TRACER = etwtrace.StackSamplingTracer()
    print(TRACER._get_technical_info())


if unused_args:
    print("WARNING: The following arguments were not used:", end=" ", flush=False, file=sys.stderr)
    print(*(f'"{a}"' if " " in a else a for a in unused_args), sep=" ", file=sys.stderr)
    sys.exit(1)

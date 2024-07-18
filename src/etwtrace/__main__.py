import sys

if "--" in sys.argv:
    import etwtrace
    import runpy

    i = sys.argv.index("--")
    old_argv, sys.argv[:] = sys.argv[:i], sys.argv[i + 1:]

    print("Tracing", *sys.argv)

    def _pop(lst, value):
        try:
            i = lst.index(value)
        except ValueError:
            return False
        del lst[i]
        return True

    if _pop(old_argv, "--instrumented"):
        tracer = etwtrace.InstrumentedTracer()
    elif _pop(old_argv, "--diaghub"):
        tracer = etwtrace.DiagnosticsHubTracer()
    elif _pop(old_argv, "--diaghubtest"):
        tracer = etwtrace.DiagnosticsHubTracer(stub=True)
    else:
        tracer = etwtrace.StackSamplingTracer()
    if old_argv[1:]:
        print("WARNING: Unrecognized arguments", *old_argv[1:])
    with tracer:
        runpy.run_path(sys.argv[0], run_name="__main__")

    sys.exit(0)


if "--info" in sys.argv:
    import etwtrace
    if "--instrumented" in sys.argv:
        tracer = etwtrace.InstrumentedTracer()
    elif "--diaghub" in sys.argv:
        tracer = etwtrace.DiagnosticsHubTracer()
    elif "--diaghubtest" in sys.argv:
        tracer = etwtrace.DiagnosticsHubTracer(stub=True)
    else:
        tracer = etwtrace.StackSamplingTracer()
    print(tracer._get_technical_info())


import pathlib
import site

PTH_TEMPLATE_VARIABLE = """{}
import etwtrace; etwtrace.enable_if({!r}, {!r})"""

PTH_FILE = pathlib.Path(site.getsitepackages()[0]) / "etwtrace.pth"

MODULE_PATH = pathlib.Path(__file__).absolute().parent.parent

if "--disable" in sys.argv:
    if PTH_FILE.is_file():
        PTH_FILE.unlink()
        print("Removed", PTH_FILE)
    sys.exit(0)


if "--enable" in sys.argv:
    v1, v2, *_ = sys.argv[sys.argv.index("--enable") + 1:] + [None, None]
    v1 = v1 or ""
    v2 = v2 or (f"{v1}_TYPE" if v1 else "ETWTRACE_TYPE")
    content = PTH_TEMPLATE_VARIABLE.format(MODULE_PATH, v1, v2)
    PTH_FILE.write_text(content, encoding="utf-8")
    print("Created", PTH_FILE)
    if v1:
        print(f"Set %{v1}% to activate")
    print(f"Set %{v2}% to to 'instrumented' to use instrumented events rather than stacks")
    sys.exit(0)

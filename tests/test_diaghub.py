import ast
import io
import os
import subprocess
import sys

from pathlib import Path, PurePath

def _get_test_root():
    root = os.getenv("ETWTRACE_TEST_ROOT")
    if root:
        return Path(root)
    return Path(__file__).absolute().parent


def _get_scripts_dir():
    scripts = os.getenv("ETWTRACE_TEST_SCRIPTS")
    if scripts:
        return Path(scripts)
    return _get_test_root() / "scripts"


ROOT = _get_test_root()
SCRIPTS = _get_scripts_dir()

def _get_arm64():
    if getattr(sys, 'winver', '').endswith('-arm64'):
        return True
    import platform
    return platform.machine() == 'ARM64'


ARM64 = _get_arm64()


try:
    import etwtrace
except ImportError:
    sys.path.append(str(ROOT.parent / "src"))

import etwtrace


def trace_events(script, *args):
    with subprocess.Popen(
        [sys.executable, "-X", "utf8", "-m", "etwtrace", "--diaghubtest", "--", SCRIPTS / script, *args],
        cwd=SCRIPTS,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    ) as p:
        out, err = p.communicate(None)
        if p.returncode:
            print(out.decode('utf-8', 'ignore'))
            print(err.decode('utf-8', 'ignore'))
        assert p.returncode == 0
    out_reader = io.TextIOWrapper(io.BytesIO(out), encoding='utf-8', errors='replace')
    for line in out_reader:
        # Expect one tuple per line
        if line.startswith('('):
            yield ast.literal_eval(line)


def test_but_do_we_diaghub():
    # We should fail, because we're not launching under Diagnostics Hub
    with subprocess.Popen(
        [sys.executable, "-m", "etwtrace", "--diaghub", "--", SCRIPTS / "no_events.py"],
        cwd=SCRIPTS,
        env={
            **os.environ,
            "DIAGHUB_INSTR_COLLECTOR_ROOT": "",
            "DIAGHUB_INSTR_RUNTIME_NAME": "",
        },
    ) as p:
        p.wait()
        assert p.returncode

    # We should succeed, but can't tell what's happened
    with subprocess.Popen(
        [sys.executable, "-m", "etwtrace", "--diaghub", "--", SCRIPTS / "no_events.py"],
        cwd=SCRIPTS,
        env={
            **os.environ,
            "DIAGHUB_INSTR_COLLECTOR_ROOT": str(Path(etwtrace.__file__).parent / "test"),
            "DIAGHUB_INSTR_RUNTIME_NAME": "DiagnosticsHubStub.dll",
        },
    ) as p:
        p.wait()
        assert not p.returncode


def test_but_do_we_diaghubtest():
    subprocess.check_call(
        [sys.executable, "-m", "etwtrace", "--diaghubtest", "--", SCRIPTS / "no_events.py"],
        cwd=SCRIPTS,
    )


def test_basic():
    mods = set()
    funcs = set()
    for e in trace_events("basic.py"):
        if e[0] == 'Cap_Define_Script_Module':
            if e[3] and (SCRIPTS / "basic.py").match(e[3]):
                mods.add(e[1])
        if e[0] == 'Cap_Define_Script_Function':
            if e[2] in mods:
                funcs.add(e[4])
    assert funcs == {'<module>', 'a', 'b'}


def find_test_stacks(trace, source_file):
    source_file = PurePath(source_file)
    mods = {}
    funcs = {}
    threads = {}
    seen = set()
    for e in trace:
        stack = threads.setdefault(e[-1], [])
        if e[0] == 'Cap_Define_Script_Module':
            if e[3] and source_file.match(e[3]):
                mods[e[1]] = e[3]
        elif e[0] == 'Cap_Define_Script_Function':
            if e[2] in mods:
                funcs[e[1]] = e[4]
        elif e[0] == 'Cap_Enter_Function_Script':
            stack.append(e[1])
        elif e[0] == 'Cap_Pop_Function_Script':
            left = stack.pop()
            #assert left == e['FunctionID'].value
        elif e[0] == 'Stub_Write_Mark' and e[1] == 3:
            yield [funcs[i]
                   for i in reversed(stack)
                   if i in funcs]


def test_trace_by_arg_a():
    trace = trace_events("by_arg.py", "a")
    samples = list(find_test_stacks(trace, SCRIPTS / "by_arg.py"))
    assert samples == [["a", "<module>"]]

def test_trace_by_arg_b():
    trace = trace_events("by_arg.py", "a", "b")
    samples = list(find_test_stacks(trace, SCRIPTS / "by_arg.py"))
    assert samples == [["a", "<module>"], ["b", "a", "<module>"]]

def test_trace_by_arg_c():
    trace = trace_events("by_arg.py", "a", "b", "c")
    samples = list(find_test_stacks(trace, SCRIPTS / "by_arg.py"))
    assert samples == [["a", "<module>"], ["b", "a", "<module>"], ["c", "b", "a", "<module>"]]


def test_threaded():
    trace = trace_events("threaded.py")
    samples = list(find_test_stacks(trace, SCRIPTS / "threaded.py"))
    assert set(map(tuple, samples)) == {
        ("a", ),
        ("b", "a"),
        ("c", "b", "a"),
    }

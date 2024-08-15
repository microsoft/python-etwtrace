import functools
import importlib.resources
import os
import pytest
import subprocess
import sys

from pathlib import Path, PurePath
from uuid import UUID

ETWTRACE = UUID('99a10640-320d-4b37-9e26-c311d86da7ab')
SYSTRACE = UUID('9e814aad-3204-11d2-9a82-006008a86939')

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

def _get_wpr():
    wpr = os.getenv("ETWTRACE_TEST_WPR")
    if wpr:
        return Path(wpr)
    root = os.getenv("SystemRoot")
    if root:
        wpr = Path(root) / "System32" / "wpr.exe"
        if wpr.is_file():
            return wpr
    for p in os.getenv("PATH", "").split(";"):
        if p:
            wpr = Path(p) / "wpr.exe"
            if wpr.is_file():
                return wpr
    raise RuntimeError("Unable to locate wpr.exe. Please set %ETWTRACE_TEST_WPR%")


WPR_EXE = _get_wpr()


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
from etwtrace.test._decoder import open as etlopen

def _get_wprp():
    wprp = os.getenv("ETWTRACE_TEST_WPRP")
    if wprp:
        return Path(wprp)
    wprp = importlib.resources.files(etwtrace) / "profiles" / "python.wprp"
    if wprp.is_file():
        return wprp
    wprp = ROOT.parent / "python.wprp"
    if wprp.is_file():
        return wprp
    raise RuntimeError("Unable to locate python.wprp. Please set %ETWTRACE_TEST_WPRP%")


WPR_PROFILE = _get_wprp()


TEST_ENV = os.environ.copy()
TEST_ENV.update(dict(
    PYTHONIOENCODING="utf-8:replace",
    PYTHONUTF8="1",
    PYTHONPATH="{};{}".format(Path(etwtrace.__file__).parent.parent, TEST_ENV.get('PYTHONPATH')).rstrip(";"),
))


def _stddecode(s):
    if isinstance(s, bytes):
        return s.decode(sys.stdout.encoding, 'replace')
    return str(s).encode(sys.stdout.encoding, 'replace').decode(sys.stdout.encoding, 'replace')


class _TracedEvents:
    def __init__(
        self,
        tmp_path,
        script,
        *script_args,
        full=True,
        etlfile=None,
        providers=(),
        event_names=(),
        count=1,
        timeout=60,
        instrumented=False,
    ):
        self.script = script
        self.script_args = script_args
        self.profile = 'Minimal' if not full else 'ARM64' if ARM64 else 'Default'
        if providers:
            self.provider_names = frozenset(p for p in providers if isinstance(p, str))
            self.provider_uuids = frozenset(p for p in providers if isinstance(p, UUID))
        else:
            self.provider_names = self.provider_uuids = None
        self.event_names = event_names
        if etlfile is None:
            self.etlfile = tmp_path / Path(script).with_suffix(".etl").name
        else:
            self.etlfile = etlfile
        self._file = None
        self.count = count
        self.timeout = timeout
        self.instrumented = instrumented

    def _start_wpr(self):
        try:
            subprocess.check_call(
                [WPR_EXE, "-start", f"{WPR_PROFILE}!{self.profile}"],
                timeout=30,
            )
        except subprocess.CalledProcessError as ex:
            if ex.returncode == 0xC5583001:
                self._stop_wpr()
                self.etlfile.unlink()
                self._start_wpr()
            else:
                raise

    def _stop_wpr(self):
        subprocess.check_output(
            [WPR_EXE, "-stop", self.etlfile],
            timeout=30,
            creationflags=subprocess.CREATE_NEW_CONSOLE,
        )

    def _cancel_wpr(self):
        subprocess.check_output(
            [WPR_EXE, "-cancel"],
            timeout=30,
        )

    def __enter__(self):
        cmd = [sys.executable, "-m", "etwtrace"]
        if self.instrumented:
            cmd.append("--instrumented")
        if self.script:
            try:
                self._start_wpr()
                try:
                    for _ in range(self.count):
                        with subprocess.Popen(
                            [*cmd, "--", SCRIPTS / self.script, *self.script_args],
                            cwd=SCRIPTS,
                            env=TEST_ENV,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,
                            encoding='utf-8',
                            errors='replace',
                        ) as p:
                            pid = p.pid
                            p.wait(self.timeout)
                            if p.returncode:
                                raise subprocess.CalledProcessError(
                                    p.returncode,
                                    p.args,
                                    p.stdout.read().rstrip(),
                                    p.stderr.read().rstrip()
                                )
                except:
                    self._cancel_wpr()
                    raise
                else:
                    self._stop_wpr()
                    print("ETL:", self.etlfile)
            except subprocess.CalledProcessError as ex:
                if ex.stdout:
                    print(_stddecode(ex.stdout), file=sys.stdout)
                if ex.stderr:
                    print(_stddecode(ex.stderr), file=sys.stderr)
                raise

        self._file = etlopen(
            self.etlfile,
            providers=self.provider_uuids,
            provider_names=self.provider_names,
            event_names=self.event_names,
            process_ids=[pid],
        )
        return self._file

    def __exit__(self, exc_type, exc_value, exc_tb):
        self._file.close()


@pytest.fixture
def trace_events(tmp_path):
    yield functools.partial(_TracedEvents, tmp_path)


def test_but_do_we_run():
    subprocess.check_call(
        [sys.executable, SCRIPTS / "no_events.py"],
        cwd=SCRIPTS,
    )


def test_but_do_we_trace():
    subprocess.check_call(
        [sys.executable, "-m", "etwtrace", "--", SCRIPTS / "no_events.py"],
        cwd=SCRIPTS,
    )


def test_but_do_we_instrument():
    subprocess.check_call(
        [sys.executable, "-m", "etwtrace", "--instrumented", "--", SCRIPTS / "no_events.py"],
        cwd=SCRIPTS,
    )


def test_basic(trace_events):
    funcs = set()
    with trace_events("basic.py", providers=['Python']) as etl:
        for e in etl:
            if e.event_name == 'PythonFunction':
                if e['SourceFile'].value and (SCRIPTS / "basic.py").match(e['SourceFile'].value):
                    funcs.add(e['Name'].value)
    assert funcs == {'<module>', 'a', 'b'}


def test_stacks(trace_events):
    addresses = set()
    funcs = {}

    with trace_events("basic.py", providers=['Python']) as etl:
        for e in etl:
            if e.event_name == 'PythonFunction':
                if (SCRIPTS / "basic.py").match(e['SourceFile'].value):
                    funcs[e['Name'].value] = e
            elif e.event_name == 'PythonStackSample':
                addresses.update(e.stack)

    assert addresses
    assert funcs

    for n, f in funcs.items():
        print('Finding', n, f['BeginAddress'].value, 'in sampled stacks')
        for ip in addresses:
            if f['BeginAddress'].value <= ip <= f['EndAddress'].value:
                break
        else:
            assert False


def find_test_stacks(etl, source_file):
    """Returns Python functions leading to PythonStackSample events"""
    source_file = PurePath(source_file)
    funcs = []
    for e in etl:
        if e.event_name == 'PythonFunction':
            if e['SourceFile'].value and source_file.match(e['SourceFile'].value):
                funcs.append(e)
        elif e.event_name == 'PythonStackSample':
            stack = []
            for ip in e.stack:
                for f in funcs:
                    if f['BeginAddress'].value <= ip <= f['EndAddress'].value:
                        stack.append(f['Name'].value)
            if stack:
                yield stack


def test_by_arg_a(trace_events):
    with trace_events("by_arg.py", "a") as etl:
        samples = list(find_test_stacks(etl, SCRIPTS / "by_arg.py"))
    assert samples == [["a", "<module>"]]


def test_by_arg_b(trace_events):
    with trace_events("by_arg.py", "a", "b") as etl:
        samples = list(find_test_stacks(etl, SCRIPTS / "by_arg.py"))
    assert samples == [["a", "<module>"], ["b", "a", "<module>"]]


def test_by_arg_c(trace_events):
    with trace_events("by_arg.py", "a", "b", "c") as etl:
        samples = list(find_test_stacks(etl, SCRIPTS / "by_arg.py"))
    assert samples == [["a", "<module>"], ["b", "a", "<module>"], ["c", "b", "a", "<module>"]]


def test_threaded(trace_events):
    with trace_events("threaded.py") as etl:
        samples = list(find_test_stacks(etl, SCRIPTS / "threaded.py"))
    assert set(map(tuple, samples)) == {
        ("a", ),
        ("b", "a"),
        ("c", "b", "a"),
    }


def find_instrumented_test_stacks(etl, source_file):
    source_file = PurePath(source_file)
    funcs = {}
    stacks = {}
    for e in etl:
        if e.event_name == 'PythonFunction':
            if e['SourceFile'].value and source_file.match(e['SourceFile'].value):
                funcs[e['FunctionID'].value] = e
        elif e.event_name == 'PythonFunctionPush':
            stack = stacks.setdefault(e.thread_id, [])
            #if stack:
            #    assert stack[-1] == e['Caller'].value
            stack.append(e['FunctionID'].value)
        elif e.event_name == 'PythonFunctionPop':
            assert e.thread_id in set(stacks)
            stack = stacks[e.thread_id]
            left = stack.pop()
            assert left == e['FunctionID'].value
        elif e.event_name == 'PythonStackSample':
            yield [funcs[i]['Name'].value
                   for i in reversed(stacks[e.thread_id])
                   if i in funcs]


def test_trace(trace_events):
    with trace_events("basic.py", providers=['Python'], instrumented=True) as etl:
        samples = list(find_instrumented_test_stacks(etl, SCRIPTS / "basic.py"))
    assert samples == [["b", "a", "<module>"]]


def test_trace_by_arg_a(trace_events):
    with trace_events("by_arg.py", "a", instrumented=True) as etl:
        samples = list(find_instrumented_test_stacks(etl, SCRIPTS / "by_arg.py"))
    assert samples == [["a", "<module>"]]

def test_trace_by_arg_b(trace_events):
    with trace_events("by_arg.py", "a", "b", instrumented=True) as etl:
        samples = list(find_instrumented_test_stacks(etl, SCRIPTS / "by_arg.py"))
    assert samples == [["a", "<module>"], ["b", "a", "<module>"]]

def test_trace_by_arg_c(trace_events):
    with trace_events("by_arg.py", "a", "b", "c", instrumented=True) as etl:
        samples = list(find_instrumented_test_stacks(etl, SCRIPTS / "by_arg.py"))
    assert samples == [["a", "<module>"], ["b", "a", "<module>"], ["c", "b", "a", "<module>"]]

def test_trace_threaded(trace_events):
    with trace_events("threaded.py", instrumented=True) as etl:
        samples = list(find_instrumented_test_stacks(etl, SCRIPTS / "threaded.py"))
    assert set(map(tuple, samples)) == {
        ("a", ),
        ("b", "a"),
        ("c", "b", "a"),
    }

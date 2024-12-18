import etwtrace
import etwtrace._cli as CLI
import pytest
import sys
from pathlib import Path

# Note that we don't test the "--" form here.
# That is used in most other tests, so we can be confident it works.
# Here we test the other options, and some general behaviours of _cli.


@pytest.mark.parametrize("help_opt", [["-?"], ["/?"], ["-H"], ["-h"], ["/h"], ["/H"], []])
def test_cli_help(capsys, help_opt):
    assert 0 == CLI.main(help_opt)
    out, err = capsys.readouterr()
    assert etwtrace.__version__ in out
    assert CLI.HELP_TEXT.replace("\r\n", "\n") in out.replace("\r\n", "\n")
    assert not err


@pytest.mark.parametrize(
    "trace_type, expected_attr",
    [
        ("--stack", "StackSamplingTracer"),
        ("--STACK", "StackSamplingTracer"),
        ("/stack", "StackSamplingTracer"),
        ("/Stack", "StackSamplingTracer"),
        ("--instrument", "InstrumentedTracer"),
        ("--instrumented", "InstrumentedTracer"),
        ("/Instrument", "InstrumentedTracer"),
        ("/instrumentED", "InstrumentedTracer"),
        ("--diaghub", "DiagnosticsHubTracer"),
        ("/diaghub", "DiagnosticsHubTracer"),
        ("--diaghubtest", "DiagnosticsHubTracer"),
        ("/diaghubtest", "DiagnosticsHubTracer"),
    ]
)
def test_cli_trace_type(trace_type, expected_attr, monkeypatch):
    accessed = []
    class MockEtwtrace:
        def __getattr__(self, attr):
            accessed.append(attr)
            return lambda *a, **kw: None

    monkeypatch.setattr(CLI, "etwtrace", MockEtwtrace())
    assert 0 == CLI.main([trace_type])
    assert accessed == [expected_attr]


@pytest.mark.parametrize(
    "trace_type, expected_module",
    [
        ("--stack", "_etwtrace"),
        ("--instrument", "_etwinstrument"),
        ("--diaghubtest", "_vsinstrument"),
    ]
)
def test_cli_trace_info(trace_type, expected_module, capsys):
    import ast
    try:
        assert 0 == CLI.main(["--info", trace_type])
    except RuntimeError as ex:
        # Only diaghubtest is allowed to raise here,
        # when we omit the test binaries from wheel tests
        assert "Diagnostics hub" in str(ex) and trace_type == "--diaghubtest"
        return
    out, err = capsys.readouterr()
    # Should be able to parse as a tuple
    v = ast.literal_eval(out.strip())
    assert isinstance(v, tuple)
    # Should contain the module name first
    assert v[0] == expected_module


@pytest.mark.parametrize(
    "args, expected_file",
    [
        (["--capture", "FILE"], "FILE"),
        (["/capture:FILE"], "FILE"),
    ]
)
def test_cli_capture(args, expected_file, monkeypatch):
    class MockWpr:
        files = []
        def __init__(self, file):
            self.files.append(file)

    monkeypatch.setattr(CLI, "Wpr", MockWpr)
    assert 0 == CLI.main(args)
    assert MockWpr.files == [expected_file]


@pytest.mark.parametrize("arg", ["--profile", "/profile", "--stacktags", "/stacktags"])
def test_cli_profile(arg, capsys):
    assert 0 == CLI.main([arg])
    out, err = capsys.readouterr()
    assert Path(out.strip()).is_file()


def test_cli_profile_indirect():
    path1 = Path(etwtrace._get_content_path("python.wprp", allow_direct=False))
    path2 = Path(etwtrace._get_content_path("python.stacktags", allow_direct=False))
    assert path1.parent == path2.parent
    assert path1.name == "python.wprp"
    assert path2.name == "python.stacktags"
    assert path1.parent.parent != Path(etwtrace.__spec__.origin).parent


def test_cli_enable_default(capsys, monkeypatch, tmp_path):
    import site
    monkeypatch.setattr(site, "getsitepackages", lambda: [str(tmp_path)])
    assert 0 == CLI.main(["--enable"])
    out, err = capsys.readouterr()
    assert f"Created {tmp_path}" in out
    pth_file = tmp_path / "etwtrace.pth"
    assert pth_file.is_file()
    pth = pth_file.read_text()
    assert "import etwtrace; etwtrace.enable_if('', 'ETWTRACE_TYPE')" in pth


def test_cli_enable_custom_var(capsys, monkeypatch, tmp_path):
    import site
    monkeypatch.setattr(site, "getsitepackages", lambda: [str(tmp_path)])
    assert 0 == CLI.main(["--enable", "TEST_ON"])
    out, err = capsys.readouterr()
    assert f"Created {tmp_path}" in out
    pth_file = tmp_path / "etwtrace.pth"
    assert pth_file.is_file()
    pth = pth_file.read_text()
    assert "import etwtrace; etwtrace.enable_if('TEST_ON', 'TEST_ON_TYPE')" in pth


def test_cli_enable_two_custom_var(capsys, monkeypatch, tmp_path):
    import site
    monkeypatch.setattr(site, "getsitepackages", lambda: [str(tmp_path)])
    assert 0 == CLI.main(["/enable", "TEST_ON", "TEST_TYPE"])
    out, err = capsys.readouterr()
    assert f"Created {tmp_path}" in out
    pth_file = tmp_path / "etwtrace.pth"
    assert pth_file.is_file()
    pth = pth_file.read_text()
    assert "import etwtrace; etwtrace.enable_if('TEST_ON', 'TEST_TYPE')" in pth


def test_cli_disable(capsys, monkeypatch, tmp_path):
    (tmp_path / "1").mkdir()
    (tmp_path / "1" / "etwtrace.pth").write_text("")
    (tmp_path / "1" / "not-etwtrace.pth").write_text("")
    (tmp_path / "2").mkdir()
    (tmp_path / "2" / "etwtrace.pth").write_text("")
    files = set(p.relative_to(tmp_path) for p in tmp_path.rglob("**/*.*"))
    assert {Path("1/etwtrace.pth"), Path("2/etwtrace.pth"), Path("1/not-etwtrace.pth")} == files
    monkeypatch.setattr(sys, "path", [str(tmp_path / "1"), str(tmp_path / "2")])
    assert 0 == CLI.main(["/disable"])
    files = set(p.relative_to(tmp_path) for p in tmp_path.rglob("**/*.*"))
    assert {Path("1/not-etwtrace.pth")} == files


def test_cli_extra_args(capsys):
    assert 1 == CLI.main(["/EXTRA", "/?", "with a space"])
    out, err = capsys.readouterr()
    # We still get the help message
    assert "Microsoft etwtrace for Python" in out
    # We also get the unused args (original casing)
    assert "arguments were not used: /EXTRA \"with a space\"" in err


def test_cli_init_wpr(tmp_path):
    # Very few things we can test, but at least make sure the initializer
    # doesn't fail
    CLI.Wpr(tmp_path / "file.etl")

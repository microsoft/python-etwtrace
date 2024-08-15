import etwtrace

def _get_content(name):
    global TEMP_PROFILE
    import importlib.resources
    import os
    from pathlib import Path
    path = importlib.resources.files(etwtrace) / "profiles" / name
    # Return real files directly
    if Path(str(path)).is_file():
        return str(path)
    try:
        # Read contents (from embedded file?)
        data = path.read_bytes()
    except FileNotFoundError:
        # Really doesn't exist, check if this is a dev layout
        path = Path(__file__).absolute().parent
        if path.parent.name == "src" and path.name == "etwtrace":
            path = path.parent.parent / name
            if path.is_file():
                return str(path)
        raise
    if TEMP_PROFILE is None:
        import tempfile
        TEMP_PROFILE = tempfile.mkdtemp()
    dest = os.path.join(TEMP_PROFILE, name)
    with open(dest, "wb") as f_out:
        f_out.write(data)
    return dest


def get_profile_path():
    return _get_content("python.wprp")


def get_stacktags_path():
    return _get_content("python.stacktags")


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
        self.profile = get_profile_path()
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

_tracer = None


class _range_mark:
    def __init__(self, mark, module):
        self.mark = mark
        self._module = module

    def __enter__(self):
        self._module.write_mark(self.mark, 1)
        return self

    def __exit__(self, exc_type, exc_value, exc_tb):
        self._module.write_mark(self.mark, 2)


class _TracingMixin:
    def __init__(self):
        self.__context = None

    def __enter__(self):
        self.enable()
        return self

    def __exit__(self, *args):
        self.disable()

    def enable(self):
        global _tracer
        _tracer = self
        self.ignore(__file__)
        self.ignore(self._module.__file__)
        self.ignore(self._module.enable.__module__)
        self.__context = self._module.enable(True)

    def disable(self):
        global _tracer
        _tracer = None
        self._module.disable(self.__context)

    def ignore(self, *files):
        self._module.get_ignored_files().update(files)

    def include(self, *prefixes):
        self._module.get_include_prefixes().extend(prefixes)

    def mark(self, mark):
        self._module.write_mark(mark, 0)

    def mark_range(self, mark):
        return _range_mark(mark, self._module)

    def _mark_stack(self, mark):
        self._module.write_mark(mark, 3)

    def _get_technical_info(self):
        return self._module._get_technical_info()


class StackSamplingTracer(_TracingMixin):
    def __init__(self):
        super().__init__()
        from . import _etwtrace as mod
        self._module = mod


class InstrumentedTracer(_TracingMixin):
    def __init__(self):
        super().__init__()
        from . import _etwinstrument as mod
        self._module = mod


class DiagnosticsHubTracer(_TracingMixin):
    def __init__(self, stub=False):
        if stub:
            from ctypes import PyDLL, py_object
            from pathlib import Path
            dll = Path(__file__).parent / "test" / "DiagnosticsHub.InstrumentationCollector.dll"
            if not dll.is_file():
                raise RuntimeError("Diagnostics hub stub requires test files")
            self._stub = PyDLL(str(dll))
            self._stub.OnEvent.argtypes = [py_object]
            self._stub.OnEvent(lambda *a: print(a))
        super().__init__()
        from . import _vsinstrument as mod
        self._module = mod


def enable_if(enable_var, type_var):
    from os import getenv as getenv

    if enable_var and getenv(enable_var, "0").lower()[:1] in ("0", "n", "f"):
        return
    trace_type = getenv(type_var, "stack").lower() if type_var else ""
    if trace_type in ("stack",):
        tracer = StackSamplingTracer()
    elif trace_type in ("diaghub",):
        tracer = DiagnosticsHubTracer()
    else:
        tracer = InstrumentedTracer()
    tracer.enable()


def mark(name):
    if not _tracer:
        raise RuntimeError("unable to mark when global tracer is not enabled")
    _tracer.mark(name)


def mark_range(name):
    if not _tracer:
        raise RuntimeError("unable to mark when global tracer is not enabled")
    return _tracer.mark_range(name)


def _mark_stack(mark):
    if not _tracer:
        raise RuntimeError("unable to mark when global tracer is not enabled")
    return _tracer._mark_stack(mark)

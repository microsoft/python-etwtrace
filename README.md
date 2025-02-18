# python-etwtrace

Enables ETW tracing events to help with profiling using the
[Windows Performance Toolkit](https://learn.microsoft.com/windows-hardware/test/wpt/).
It supports Python 3.9 and later on Windows 64-bit and Windows ARM64.

![Windows Performance Analyzer with a mixed Python/native flame graph](https://github.com/microsoft/python-etwtrace/raw/main/WPA-Python.png)

(Note that the WPA integration shown above requires the [current preview release](https://www.microsoft.com/store/productId/9N58QRW40DFW?ocid=pdpshare).)

Two forms of profiling are supported:

* stack sampling, where regular CPU sampling will include Python calls
* instrumentation, where events are raised on entry/exit of Python functions

If you will inspect results using [Windows Performance Analyzer](https://www.microsoft.com/store/productId/9N58QRW40DFW?ocid=pdpshare)
(WPA), then you will prefer stack sampling (the default).
This method inserts additional native function calls in place of pure-Python calls,
and provides WPA with the metadata necessary to display the function.
Configure the provided [stack tags](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/stack-tags)
file (`python -m etwtrace --stacktags`) in WPA and view the "Stack (Frame Tags)"
column to filter out internal Python calls.
You will need Python symbols for the best results;
these are an optional component in the installer from python.org.

If you are capturing ETW events for some other form of analysis,
you may prefer more traditional instrumentation.
This method generates ETW events on entry and exit of each function,
which can be reconstructed into call stacks at any point.
It also provides more accurate call count data than stack sampling.

![Windows Performance Analyzer with a call count, function info, and sequence views over instrumented data](https://github.com/microsoft/python-etwtrace/raw/main/WPA-Instrumented.png)

## Capturing events

See [Windows Performance Recorder](https://learn.microsoft.com/windows-hardware/test/wpt/windows-performance-recorder)
for detailed information on recording events,
including installation of the Windows Performance Toolkit.
Here we cover only the basics to capture a trace of some Python code.

The `wpr` tool is used to start and stop recording a trace,
and to export the results to an `.etl` file.
The trace must be started and stopped as Administrator, however,
the code under test may be run as a regular user.

For basic capture, use the `--capture` argument to have `etwtrace` launch and
stop `wpr`:

```
> python -m etwtrace --capture output.etl -- .\my-script.py
```

A [recording profile](https://learn.microsoft.com/windows-hardware/test/wpt/recording-profiles)
is used to select the event sources that will be recorded. We include a profile
configured for Python as [python.wprp](https://github.com/microsoft/python-etwtrace/blob/main/src/python.wprp).
We recommend downloading this file from here,
or finding it in the `etwtrace` package's install directory
by running `python -m etwtrace --profile`.

To record a Python trace:

```
# Ensure the output file does not exist, or tracing will fail
> del output.etl

> wpr -start python.wprp!Default

# run your code as shown below ...

> wpr -stop output.etl
```

You can pass `!Minimal` instead of `!Default` to reduce the size of the trace by
omitting some other system providers.

When running on Windows ARM64, use `!ARM64` instead of `!Default` to avoid some
providers that are currently absent.

To collect additional information, we suggest copying the configuration from
`python.wprp` into your own recording profile.
The WPR docs above provide more information.

## Launching with tracing

To enable for a single command, launch with `-m etwtrace -- <script>` or
`-m etwtrace -- -m <module>`:

```
> python -m etwtrace -- .\my-script.py arg1 arg2
```

Pass `--instrumented` before the `--` to select that mode.

```
> python -m etwtrace --instrumented -- -m test_module
```

Pass `--capture FILE` before the `--` to automatically start and stop `wpr`.

```
> python -m etwtrace --capture output.etl -- .\my-script.py arg1 arg2
```

To enable permanently for an environment, run the module with `--enable`:

```
> python -m etwtrace --enable
Created etwtrace.pth
Set %ETWTRACE_TYPE% to 'instrumented' to use instrumented events rather than stacks
```

To enable permanently but only when an environment variable is set, also provide
the variable name. A second variable name may be provided to specify the kind
of tracing ('instrumented' or 'stack' (default)); if omitted, the variable name
will be derived from the first.

```
> python -m etwtrace --enable PYTHON_ETW_TRACE TRACE_TYPE
Created etwtrace.pth
Set %PYTHON_ETW_TRACE% to activate
Set %TRACE_TYPE% to 'instrumented' to use instrumented events rather than stacks

> $env:PYTHON_ETW_TRACE = "1"
> python .\my-script.py arg1 arg2
```

To disable, run with `--disable` or delete the created `.pth` file.

```
> python -m etwtrace --disable
Removed etwtrace.pth
```

## Visual Studio integration

This module is also used for Visual Studio profiling of Python code, however,
those interfaces are not supported for use outside of Visual Studio,
and so are not documented here.

Please see the Visual Studio documentation for information about profiling
Python code.

# Building from Source

To build the sources:

```
> python -m pip install pymsbuild

# Builds into the source directory (-g for a debug configuration)
> python -m pymsbuild -g

# Ensure source directory is importable
> $env:PYTHONPATH = ".\src"
> python ...
```

# Events

When enabled, ETW events are raised on thread creation, ending, and when a
function is first executed. Other events may be raised explicitly by Python code
that calls `etwtrace.custom_mark`, or the internal `etwtrace._stack_mark`
function.

The `PythonFunction` event provides the range of memory that will appear in
stack samples when the specified function is called. It can be used to map
sampled frames back to the source file and line number of the function being
executed. The `FunctionID` argument is a unique value for the lifetime of the
process representing the function.

The `PythonThread` event typically comes as a range (using start and stop
opcodes) and is intended to highlight a region of interest. Similarly, the
`PythonMark` event may be a range highlighting a particular region of interest.

The `PythonStackSample` event is primarily used by tests to force a stack sample
to be collected at a particular point in execution. When used for this, it
should be configured in the collection profile to include the stack, as there is
nothing inherent to the event that causes collection. This event is raised by
the private and undocumented `_mark_stack` function.

The `PythonFunctionPush` and `PythonFunctionPop` events are raised in
instrumentation mode on entry and exit of a function. The `FunctionID` and
`Caller` (another function ID) will have previously appeared in `PythonFunction`
events.

The Python events provider GUID is `99a10640-320d-4b37-9e26-c311d86da7ab`.

| Event | Keyword | Args |
|-------|---------|------|
| `PythonThread` |  `0x0100` | ThreadId |
| `PythonFunction` | `0x0400` | FunctionID, BeginAddress, EndAddress, LineNumber, SourceFile, Name |
| `PythonMark` | `0x0800` | Mark |
| `PythonStackSample` | `0x0200` | Mark |
| `PythonFunctionPush` | `0x1000` | FunctionID, Caller, CallerLine |
| `PythonFunctionPop` | `0x2000` | FunctionID |

## Contributing

This project welcomes contributions and suggestions.  Most contributions require you to agree to a
Contributor License Agreement (CLA) declaring that you have the right to, and actually do, grant us
the rights to use your contribution. For details, visit https://cla.opensource.microsoft.com.

When you submit a pull request, a CLA bot will automatically determine whether you need to provide
a CLA and decorate the PR appropriately (e.g., status check, comment). Simply follow the instructions
provided by the bot. You will only need to do this once across all repos using our CLA.

This project has adopted the [Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/).
For more information see the [Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/) or
contact [opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional questions or comments.

## Trademarks

This project may contain trademarks or logos for projects, products, or services. Authorized use of Microsoft 
trademarks or logos is subject to and must follow 
[Microsoft's Trademark & Brand Guidelines](https://www.microsoft.com/en-us/legal/intellectualproperty/trademarks/usage/general).
Use of Microsoft trademarks or logos in modified versions of this project must not cause confusion or imply Microsoft sponsorship.
Any use of third-party trademarks or logos are subject to those third-party's policies.

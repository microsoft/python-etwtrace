import os
from pymsbuild import *
from pymsbuild.cython import *


# See https://packaging.python.org/en/latest/specifications/core-metadata/ for fields
METADATA = {
    "Metadata-Version": "2.1",
    "Name": "etwtrace",
    "Version": os.getenv("BUILD_BUILDNUMBER", "0.1"),
    "Author": "Microsoft Corporation",
    "Author-email": "python@microsoft.com",
    "Home-page": "https://github.com/microsoft/python-etwtrace/",
    "Project-url": [
        "Bug Tracker, https://github.com/microsoft/python-etwtrace/issues",
    ],
    "Summary": "Generates ETW events for tracing Python apps with the Windows Performance Toolkit",
    "Description": File("README.md"),
    "Description-Content-Type": "text/markdown",
    "Keywords": "windows,performance,toolkit,analyzer,recorder,wpa,wpr,tracing,profiling",
    "Classifier": [
        # See https://pypi.org/classifiers/ for the full list
        "Development Status :: 4 - Beta",
        "Environment :: Win32 (MS Windows)",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: MIT License",
        "Operating System :: Microsoft :: Windows",
        "Programming Language :: Python",
        "Programming Language :: Python :: 3 :: Only",
        "Programming Language :: Python :: Implementation :: CPython",
        "Topic :: Software Development :: Quality Assurance",
        "Topic :: Software Development :: Testing",
        "Topic :: System :: Monitoring",
    ],
    "Requires-Dist": [
        # https://packaging.python.org/en/latest/specifications/dependency-specifiers/
    ],
}

PYD_OPTS = [
    Property("SpectreMitigation", "Spectre"),
    ItemDefinition(
        "ClCompile",
        PreprocessorDefinitions=Prepend("CYTHON_FAST_THREAD_STATE=0;"),
        ControlFlowGuard="Guard",
        SDLCheck="true",
        WarningLevel="Level3",
        OmitFramePointers="false",
    ),
    ItemDefinition(
        "Link",
        AdditionalDependencies=Prepend("ntdll.lib;tdh.lib;"),
        CETCompat=ConditionalValue("true", condition="$(Platform) == 'Win32' or $(Platform) == 'x64'"),
    ),
]


PACKAGE = Package(
    'etwtrace',
    PyFile("etwtrace/__main__.py"),
    PyFile("etwtrace/__init__.py"),
    PyFile("etwtrace/_cli.py"),
    # _version-ver is generated at build with the correct version number
    PyFile("etwtrace/_version-ver.py", name="_version.py"),

    Package(
        'profiles',
        File("../python.wprp"),
        File("../python.stacktags"),
    ),

    PydFile(
        '_etwtrace',
        *PYD_OPTS,
        ItemDefinition("ClCompile", PreprocessorDefinitions=Prepend("WITH_TRACELOGGING;")),
        # Disable incremental linking because _etwtrace.c needs
        # a real pointer to its _thunk function
        ItemDefinition('Link', LinkIncremental='false'),
        CSourceFile('etwtrace/_etwtrace.c', ControlFlowGuard=""),
        CSourceFile('etwtrace/_etwcommon.c'),
        IncludeFile('etwtrace/_etwcommon.h'),
        CSourceFile('etwtrace/_trace.cpp'),
        IncludeFile('etwtrace/_trace.h'),
        IncludeFile('etwtrace/_func_id.h'),
    ),
    PydFile(
        '_etwinstrument',
        *PYD_OPTS,
        ItemDefinition("ClCompile", PreprocessorDefinitions=Prepend("WITH_TRACELOGGING;")),
        CSourceFile('etwtrace/_etwinstrument.c'),
        CSourceFile('etwtrace/_etwcommon.c'),
        IncludeFile('etwtrace/_etwcommon.h'),
        CSourceFile('etwtrace/_trace.cpp'),
        IncludeFile('etwtrace/_trace.h'),
        IncludeFile('etwtrace/_func_id.h'),
    ),
    PydFile(
        '_vsinstrument',
        *PYD_OPTS,
        CSourceFile('etwtrace/_vsinstrument.c'),
        CSourceFile('etwtrace/_etwcommon.c'),
        IncludeFile('etwtrace/_etwcommon.h'),
        IncludeFile('etwtrace/_func_id.h'),
    ),
    Package('test',
        CythonPydFile(
            '_decoder',
            *PYD_OPTS,
            ItemDefinition("ClCompile", DisableSpecificWarnings=Prepend("4267;4244;4018;")),
            PyxFile('etwtrace/_decoder.pyx'),
            IncludeFile('etwtrace/_windows.pxd'),
            CSourceFile('etwtrace/_tdhreader.cpp'),
            IncludeFile('etwtrace/_tdhreader.h'),
        ),
        PydFile(
            'DiagnosticsHub.InstrumentationCollector',
            *PYD_OPTS,
            CSourceFile('etwtrace/_diaghubstub.c'),
            TargetExt='.dll',
        ),
    ),
    source='src',
)


def init_METADATA():
    import os, re
    _, sep, version = os.getenv("GITHUB_REF", os.getenv("BUILD_SOURCEBRANCH", "")).rpartition("/")
    if sep and re.match(r"\d+(\.\d+)+((a|b|rc)\d+)?$", version):
        # Looks like a version tag
        METADATA["Version"] = version


def init_PACKAGE(tag=None):
    # Create _version-ver.py with replaced %VERSION% from METADATA
    VER_PY = PACKAGE.find("_version.py")
    try:
        f = open("src/etwtrace/_version.py", "r", encoding="utf-8")
    except FileNotFoundError:
        return
    with f:
        ver_py = f.read()
    ver_py = ver_py.replace("%VERSION%", METADATA["Version"])
    with open(f"src/etwtrace/_version-ver.py", "w", encoding="utf-8") as f:
        f.write(ver_py)

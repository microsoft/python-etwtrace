import os
from pymsbuild import *
from pymsbuild.cython import *


# See https://packaging.python.org/en/latest/specifications/core-metadata/ for fields
METADATA = {
    "Metadata-Version": "2.1",
    "Name": "etwtrace-test",
    "Version": os.getenv("BUILD_BUILDNUMBER", "0.1"),
    "Author": "Microsoft Corporation",
    "Author-email": "python@microsoft.com",
    "Home-page": "https://github.com/microsoft/python-etwtrace/",
    "Project-url": [
        "Bug Tracker, https://github.com/microsoft/python-etwtrace/issues",
    ],
    "Summary": "Test files",
    "Classifier": ["Private :: Do Not Upload"],
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
        # This package will be renamed in init_PACKAGE
        Package('arch',
            CProject(
                'DiagnosticsHubStub',
                *PYD_OPTS,
                CSourceFile('etwtrace/_diaghubstub.c'),
            ),
        )
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
    if not tag:
        return
    if tag.endswith("-win32"):
        PACKAGE.find('test/arch').name = 'x86'
    elif tag.endswith("-win_amd64"):
        PACKAGE.find('test/arch').name = 'amd64'
    elif tag.endswith("-win_arm64"):
        PACKAGE.find('test/arch').name = 'arm64'

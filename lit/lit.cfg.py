# -*- Python -*-

import os
import re
import shutil
import site
import sys

import lit.formats
from lit.llvm import llvm_config
from lit.llvm.subst import FindTool
from lit.llvm.subst import ToolSubst

site.addsitedir(os.path.dirname(__file__))
from helper import toolchain

# name: The name of this test suite.
config.name = 'LLDB'

# testFormat: The test format to use to interpret tests.
config.test_format = lit.formats.ShTest(not llvm_config.use_lit_shell)

# suffixes: A list of file extensions to treat as test files. This is overriden
# by individual lit.local.cfg files in the test subdirectories.
config.suffixes = ['.test', '.cpp', '.s']

# excludes: A list of directories to exclude from the testsuite. The 'Inputs'
# subdirectories contain auxiliary inputs for various tests in their parent
# directories.
config.excludes = ['Inputs', 'CMakeLists.txt', 'README.txt', 'LICENSE.txt']

# test_source_root: The root path where tests are located.
config.test_source_root = os.path.dirname(__file__)

# test_exec_root: The root path where tests should be run.
config.test_exec_root = os.path.join(config.lldb_obj_root, 'lit')

# Begin Swift mod.
# Swift's libReflection builds without ASAN, which causes a known
# false positive in std::vector. If sanitizers are off, this is just
# a no-op
config.environment['ASAN_OPTIONS'] = 'detect_container_overflow=0'
# End Swift mod.

llvm_config.use_default_substitutions()

toolchain.use_lldb_substitutions(config)

toolchain.use_support_substitutions(config)


if re.match(r'^arm(hf.*-linux)|(.*-linux-gnuabihf)', config.target_triple):
    config.available_features.add("armhf-linux")

def calculate_arch_features(arch_string):
    # This will add a feature such as x86, arm, mips, etc for each built
    # target
    features = []
    for arch in arch_string.split():
        features.append(arch.lower())
    return features

# Run llvm-config and add automatically add features for whether we have
# assertions enabled, whether we are in debug mode, and what targets we
# are built for.
llvm_config.feature_config(
    [('--assertion-mode', {'ON': 'asserts'}),
     ('--build-mode', {'DEBUG': 'debug'}),
     ('--targets-built', calculate_arch_features)
     ])

# Clean the module caches in the test build directory.  This is
# necessary in an incremental build whenever clang changes underneath,
# so doing it once per lit.py invocation is close enough.

for i in ['module-cache-clang', 'module-cache-lldb']:
    cachedir = os.path.join(os.path.dirname(config.lldb_libs_dir),
                            'lldb-test-build.noindex', i)
    if os.path.isdir(cachedir):
        print("Deleting module cache at %s."%cachedir)
        shutil.rmtree(cachedir)

# Set a default per-test timeout of 10 minutes. Setting a timeout per test
# requires the psutil module and lit complains if the value is set but the
# module can't be found.
try:
    import psutil  # noqa: F401
    lit_config.maxIndividualTestTime = 900
except ImportError:
    pass

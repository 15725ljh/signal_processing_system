from setuptools import setup, Extension
from Cython.Build import cythonize
import numpy as np
import os

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))  # 04_GUI_signal_processing root

extensions = [
    Extension(
        "core.config_manager",
        ["core/config_manager.py"],
        include_dirs=[np.get_include()],
    ),
    Extension(
        "core.signal_utils",
        ["core/signal_utils.py"],
        include_dirs=[np.get_include()],
    ),
]

setup(
    name="radar_signal_processing_core",
    ext_modules=cythonize(
        extensions,
        compiler_directives={
            "language_level": "3",
            "boundscheck": False,
            "cdivision": True,
        },
    ),
)

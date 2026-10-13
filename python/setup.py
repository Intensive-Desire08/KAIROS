"""
KAIROS ML Threat Detection Service package setup.
See Technical Specification — Module 3, Section 14.2.
"""

from setuptools import find_packages, setup

setup(
    name="kairos-ml-service",
    version="1.0.0",
    description="KAIROS ML Threat Detection Service",
    packages=find_packages(),
    python_requires=">=3.9",
    install_requires=[
        "fastapi>=0.100.0",
        "uvicorn[standard]>=0.23.0",
        "scikit-learn>=1.3.0",
        "pandas>=2.0.0",
        "numpy>=1.24.0",
        "joblib>=1.3.0",
        "pydantic>=2.0.0",
        "loguru>=0.7.0",
    ],
    extras_require={
        "dev": [
            "pytest>=7.4.0",
            "httpx>=0.24.0",
        ],
    },
)

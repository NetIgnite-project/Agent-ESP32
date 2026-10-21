"""
PlatformIO pre-build script: generates data/cert/x509_crt_bundle.bin, the bundle of trusted root
certificates that the firmware embeds to verify the server's TLS certificate.

The bundle is built from cacrt_all.pem (Mozilla's root CAs) plus every .pem/.der file in
data/cert/custom/ (for servers with a certificate from a private CA). The file is only rewritten
when its content changes, so it doesn't trigger a rebuild on every run.
"""

import os
import shutil
import subprocess
import sys
import tempfile

Import("env")  # noqa: F821 - provided by PlatformIO (SCons)

# The interpreter running this script: the cryptography check below and the generator must use the
# same one (pioarduino runs the build in its own environment, separate from $PYTHONEXE).
PYTHON = sys.executable
CERT_DIR = os.path.join(env.subst("$PROJECT_DIR"), "data", "cert")  # noqa: F821
CUSTOM_DIR = os.path.join(CERT_DIR, "custom")
BUNDLE_PATH = os.path.join(CERT_DIR, "x509_crt_bundle.bin")


def ensure_cryptography():
    try:
        import cryptography  # noqa: F401
    except ImportError:
        print("Installing the 'cryptography' package for the certificate bundle generator...")
        subprocess.check_call([PYTHON, "-m", "pip", "install", "cryptography"])


def certificate_inputs():
    inputs = [os.path.join(CERT_DIR, "cacrt_all.pem")]
    if os.path.isdir(CUSTOM_DIR):
        inputs += sorted(
            os.path.join(CUSTOM_DIR, name)
            for name in os.listdir(CUSTOM_DIR)
            if name.endswith((".pem", ".der"))
        )
    return inputs


def generate_bundle():
    ensure_cryptography()

    work_dir = tempfile.mkdtemp()
    try:
        # gen_crt_bundle.py writes "x509_crt_bundle" into its working directory. Python warnings are
        # silenced: cryptography warns about a root in Mozilla's list on every run.
        subprocess.check_call(
            [PYTHON, os.path.join(CERT_DIR, "gen_crt_bundle.py"), "--quiet", "--input", *certificate_inputs()],
            cwd=work_dir,
            # The same import path as this process (pioarduino adds its own packages to it).
            env=dict(os.environ, PYTHONWARNINGS="ignore", PYTHONPATH=os.pathsep.join(p for p in sys.path if p)),
        )
        with open(os.path.join(work_dir, "x509_crt_bundle"), "rb") as f:
            bundle = f.read()
    finally:
        shutil.rmtree(work_dir, ignore_errors=True)

    if os.path.exists(BUNDLE_PATH):
        with open(BUNDLE_PATH, "rb") as f:
            if f.read() == bundle:
                return

    with open(BUNDLE_PATH, "wb") as f:
        f.write(bundle)
    print("Generated %s" % os.path.relpath(BUNDLE_PATH, env.subst("$PROJECT_DIR")))  # noqa: F821


generate_bundle()

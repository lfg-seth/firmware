"""Compile the production controller and responder with deterministic hardware boundaries."""
import pathlib
import subprocess
import tempfile

root=pathlib.Path(__file__).resolve().parents[2]
suite=pathlib.Path(__file__).resolve().parent
headers=("configuration.h","mesh/mesh-pb-constants.h","Channels.h","FSCommon.h","NodeDB.h","RTC.h",
         "Router.h","airtime.h","main.h","Curve25519.h","SHA256.h")
with tempfile.TemporaryDirectory() as directory:
    tmp=pathlib.Path(directory)
    for header in headers:
        dest=tmp/header
        dest.parent.mkdir(exist_ok=True)
        dest.write_text('#include "platform.h"\n')
    for client in (False,True):
        exe=tmp/("client" if client else "responder")
        subprocess.run(["g++","-std=c++17","-fsanitize=undefined,address","-fno-omit-frame-pointer","-no-pie",
                        "-DSNORR_DIRECT_SURVEY=1",*(["-DSNORR_SURVEY_CLIENT=1"] if client else []),
                        "-I"+str(tmp),"-I"+str(suite),"-I"+str(root/"src/modules"),str(suite/"controller.cpp"),
                        "-o",str(exe)], check=True)
        subprocess.run([exe],check=True)

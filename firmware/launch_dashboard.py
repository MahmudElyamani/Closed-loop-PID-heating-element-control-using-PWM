Import("env")

import subprocess

def after_upload(source, target, env):

    subprocess.Popen(
        ["python", "heater_dashboard.py"]
    )

env.AddPostAction(
    "upload",
    after_upload
)